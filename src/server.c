#define _GNU_SOURCE
#include "server.h"
#include "mqtt_parser.h"
#include "mqtt_encoder.h"
#include "logger.h"

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static client_conn_t *create_client(int fd, const char *ip, uint16_t port, const global_throttle_config_t *th_cfg) {
    client_conn_t *client = (client_conn_t *)calloc(1, sizeof(client_conn_t));
    if (!client) return NULL;

    client->fd = fd;
    snprintf(client->ip, sizeof(client->ip), "%s", ip);
    client->port = port;
    client->state = CONN_STATE_NEW;
    client->last_active_time = time(NULL);

    if (th_cfg && th_cfg->enabled) {
        client_throttle_init(&client->throttle, th_cfg->max_packets_per_sec, th_cfg->burst_packets,
                             th_cfg->max_bytes_per_sec, th_cfg->burst_bytes);
    } else {
        client_throttle_init(&client->throttle, 0, 0, 0, 0); /* Unlimited */
    }

    if (!byte_buffer_init(&client->rx_buffer, MQTT_DEFAULT_BUFFER_SIZE, MQTT_MAX_BUFFER_SIZE)) {
        free(client);
        return NULL;
    }
    if (!byte_buffer_init(&client->tx_buffer, MQTT_DEFAULT_BUFFER_SIZE, MQTT_MAX_BUFFER_SIZE)) {
        byte_buffer_free(&client->rx_buffer);
        free(client);
        return NULL;
    }

    return client;
}

static void destroy_client(client_conn_t *client) {
    if (!client) return;
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
    byte_buffer_free(&client->rx_buffer);
    byte_buffer_free(&client->tx_buffer);
    free(client);
}

static client_conn_t *find_client_by_id(mqtt_server_t *srv, const char *client_id) {
    if (!srv || !client_id || !*client_id) return NULL;
    for (size_t i = 0; i < SERVER_MAX_CLIENTS; i++) {
        if (srv->clients[i] && strcmp(srv->clients[i]->client_id, client_id) == 0) {
            return srv->clients[i];
        }
    }
    return NULL;
}

void server_disconnect_client(mqtt_server_t *srv, client_conn_t *client, bool graceful) {
    if (!srv || !client) return;

    pthread_mutex_lock(&srv->lock);

    log_info("Disconnecting client [%s:%u] (ClientID: '%s', graceful: %s)",
             client->ip, client->port, 
             client->client_id[0] ? client->client_id : "unauthenticated",
             graceful ? "true" : "false");

    /* Deregister from epoll */
    if (client->fd >= 0 && srv->epoll_fd >= 0) {
        epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    }

    /* Check for Last Will and Testament if ungraceful */
    if (!graceful && client->client_id[0]) {
        mqtt_session_t *sess = session_find(&srv->session_registry, client->client_id);
        if (sess && sess->will.present) {
            log_warn("[LWT] Triggering Last Will & Testament for client '%s' on topic '%s' (%zu bytes)",
                     client->client_id, sess->will.topic, sess->will.payload_len);
            srv->telemetry.total_lwt_published++;
            
            /* Publish Will message across Trie */
            pthread_mutex_unlock(&srv->lock);
            server_route_publish(srv, sess->will.topic, sess->will.payload,
                                 sess->will.payload_len, sess->will.qos,
                                 sess->will.retain, 1, client->client_id);
            pthread_mutex_lock(&srv->lock);
        }
    }

    /* Update Session Registry */
    if (client->client_id[0]) {
        session_disconnect(&srv->session_registry, client->client_id, graceful);
        if (client->clean_session) {
            /* If clean session, remove all subscriptions from Topic Trie */
            trie_unsubscribe_client(srv->topic_trie, client->client_id);
        } else {
            /* Persistent session: update socket_fd to -1 in subscriptions */
            trie_update_client_socket(srv->topic_trie, client->client_id, -1);
        }
    }

    /* Remove from client tracking array */
    for (size_t i = 0; i < SERVER_MAX_CLIENTS; i++) {
        if (srv->clients[i] == client) {
            srv->clients[i] = NULL;
            if (srv->active_client_count > 0) {
                srv->active_client_count--;
            }
            break;
        }
    }
    srv->telemetry.current_active_clients = srv->active_client_count;

    destroy_client(client);
    pthread_mutex_unlock(&srv->lock);
}

bool server_init(mqtt_server_t *srv, const char *host, uint16_t port) {
    if (!srv) return false;

    memset(srv, 0, sizeof(mqtt_server_t));
    srv->port = port ? port : MQTT_DEFAULT_PORT;
    snprintf(srv->host, sizeof(srv->host), "%s", host ? host : "0.0.0.0");
    srv->listen_fd = -1;
    srv->epoll_fd = -1;
    srv->running = false;

    if (pthread_mutex_init(&srv->lock, NULL) != 0) {
        log_error("Failed to initialize server mutex");
        return false;
    }

    /* Initialize Topic Trie, Session Registry, Retained Store, Telemetry */
    srv->topic_trie = trie_node_create("");
    if (!srv->topic_trie) {
        log_error("Failed to allocate topic radix tree");
        return false;
    }

    if (!session_registry_init(&srv->session_registry)) {
        log_error("Failed to initialize session registry");
        return false;
    }

    if (!retained_store_init(&srv->retained_store)) {
        log_error("Failed to initialize retained message store");
        return false;
    }

    if (!telemetry_init(&srv->telemetry)) {
        log_error("Failed to initialize telemetry engine");
        return false;
    }

    /* Ignore SIGPIPE so broken client sockets do not terminate broker */
    signal(SIGPIPE, SIG_IGN);

    /* 1. Create master TCP socket */
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        log_error("Failed to create TCP socket: %s", strerror(errno));
        return false;
    }

    /* 2. Set socket options (SO_REUSEADDR and SO_REUSEPORT) */
    int opt = 1;
    if (setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_warn("Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
#ifdef SO_REUSEPORT
    if (setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        log_warn("Failed to set SO_REUSEPORT: %s", strerror(errno));
    }
#endif

    /* 3. Set non-blocking socket flag */
    set_nonblocking(srv->listen_fd);

    /* 4. Bind socket to host:port */
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(srv->port);
    if (inet_pton(AF_INET, srv->host, &bind_addr.sin_addr) <= 0) {
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(srv->listen_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        log_error("Failed to bind socket to %s:%u: %s", srv->host, srv->port, strerror(errno));
        close(srv->listen_fd);
        srv->listen_fd = -1;
        return false;
    }

    /* 5. Start listening */
    if (listen(srv->listen_fd, SOMAXCONN) < 0) {
        log_error("Failed to listen on socket: %s", strerror(errno));
        close(srv->listen_fd);
        srv->listen_fd = -1;
        return false;
    }

    /* 6. Create Linux epoll instance */
    srv->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (srv->epoll_fd < 0) {
        log_error("Failed to create epoll instance: %s", strerror(errno));
        close(srv->listen_fd);
        srv->listen_fd = -1;
        return false;
    }

    /* 7. Register listening socket in epoll */
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.ptr = srv; /* Special pointer indicating listening socket */
    if (epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, srv->listen_fd, &ev) < 0) {
        log_error("Failed to add listen_fd to epoll: %s", strerror(errno));
        close(srv->listen_fd);
        close(srv->epoll_fd);
        srv->listen_fd = -1;
        srv->epoll_fd = -1;
        return false;
    }

    log_info("Zero-Dependency MQTT Broker initialized on %s:%u (epoll_fd: %d, listen_fd: %d)",
             srv->host, srv->port, srv->epoll_fd, srv->listen_fd);
    return true;
}

bool server_send_bytes(mqtt_server_t *srv, client_conn_t *client, const uint8_t *data, size_t len) {
    if (!srv || !client || !data || len == 0 || client->fd < 0) return false;

    /* If write buffer has pending data, append to queue */
    if (client->tx_buffer.size > 0) {
        if (!byte_buffer_append(&client->tx_buffer, data, len)) {
            log_error("TX buffer overflow for client %s:%u", client->ip, client->port);
            return false;
        }
        return true;
    }

    /* Attempt immediate non-blocking write */
    ssize_t n = send(client->fd, data, len, MSG_NOSIGNAL);
    if (n > 0) {
        srv->total_bytes_sent += (size_t)n;
        srv->telemetry.total_bytes_tx += (size_t)n;
        if ((size_t)n == len) {
            return true;
        }
        /* Partial write: queue remaining bytes */
        data += n;
        len -= (size_t)n;
    } else if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        log_warn("Socket write error for client %s:%u: %s", client->ip, client->port, strerror(errno));
        return false;
    }

    /* Queue unwritten bytes into tx_buffer and enable EPOLLOUT */
    if (!byte_buffer_append(&client->tx_buffer, data, len)) {
        log_error("TX buffer overflow for client %s:%u", client->ip, client->port);
        return false;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
    ev.data.ptr = client;
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_MOD, client->fd, &ev);
    return true;
}

static void handle_client_write(mqtt_server_t *srv, client_conn_t *client) {
    if (!srv || !client || client->fd < 0) return;

    if (client->tx_buffer.size == 0) {
        /* Nothing to write: disable EPOLLOUT */
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        ev.data.ptr = client;
        epoll_ctl(srv->epoll_fd, EPOLL_CTL_MOD, client->fd, &ev);
        return;
    }

    ssize_t n = send(client->fd, client->tx_buffer.data, client->tx_buffer.size, MSG_NOSIGNAL);
    if (n > 0) {
        srv->total_bytes_sent += (size_t)n;
        srv->telemetry.total_bytes_tx += (size_t)n;
        byte_buffer_drain(&client->tx_buffer, (size_t)n);

        if (client->tx_buffer.size == 0) {
            /* Flushed all pending bytes: revert to EPOLLIN */
            struct epoll_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
            ev.data.ptr = client;
            epoll_ctl(srv->epoll_fd, EPOLL_CTL_MOD, client->fd, &ev);
        }
    } else if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        log_warn("Write flush failed on client %s:%u: %s", client->ip, client->port, strerror(errno));
        server_disconnect_client(srv, client, false);
    }
}

/* Delivery of a single PUBLISH packet to a subscriber */
static void deliver_publish_to_client(mqtt_server_t *srv, client_conn_t *target,
                                      const char *topic, const uint8_t *payload,
                                      size_t payload_len, uint8_t qos, uint8_t retain,
                                      uint16_t packet_id) {
    uint8_t pub_buf[MQTT_MAX_BUFFER_SIZE];
    size_t enc_len = mqtt_encode_publish(pub_buf, sizeof(pub_buf), topic, packet_id,
                                         qos, retain, 0, payload, payload_len);
    if (enc_len > 0) {
        server_send_bytes(srv, target, pub_buf, enc_len);
        srv->total_packets_sent++;
        srv->telemetry.total_packets_tx++;
    }
}

void server_route_publish(mqtt_server_t *srv, const char *topic,
                          const uint8_t *payload, size_t payload_len,
                          uint8_t qos, uint8_t retain, uint16_t packet_id,
                          const char *sender_client_id) {
    if (!srv || !topic) return;

    srv->telemetry.total_publishes++;

    /* 1. Handle Retained Store */
    if (retain) {
        retained_store_put(&srv->retained_store, topic, payload, payload_len, qos);
        srv->telemetry.total_retained_messages = srv->retained_store.count;
    }

    /* 2. Resolve subscribers in Topic Radix Tree (Trie) */
    match_result_t matches;
    size_t matched_count = trie_match_subscribers(srv->topic_trie, topic, &matches);

    log_info("[ROUTER] Topic '%s' matched %zu subscribers (sender: '%s')",
             topic, matched_count, sender_client_id ? sender_client_id : "broker");

    for (size_t i = 0; i < matched_count; i++) {
        const char *sub_cid = matches.subscribers[i].client_id;
        uint8_t sub_qos = matches.subscribers[i].qos;
        uint8_t delivery_qos = (qos < sub_qos) ? qos : sub_qos; /* Min(pub_qos, sub_qos) */

        pthread_mutex_lock(&srv->lock);
        client_conn_t *target = find_client_by_id(srv, sub_cid);

        if (target && target->fd >= 0) {
            /* Client is currently active and connected: deliver immediately */
            deliver_publish_to_client(srv, target, topic, payload, payload_len, delivery_qos, 0, packet_id);
            pthread_mutex_unlock(&srv->lock);
        } else {
            /* Client is offline: check if persistent session exists to queue QoS 1 message */
            pthread_mutex_unlock(&srv->lock);
            if (delivery_qos > 0) {
                mqtt_session_t *sess = session_find(&srv->session_registry, sub_cid);
                if (sess && !sess->clean_session) {
                    session_queue_offline_message(sess, topic, payload, payload_len, delivery_qos, 0, packet_id);
                    log_info("[ROUTER] Queued QoS %u message for offline persistent client '%s'", delivery_qos, sub_cid);
                }
            }
        }
    }
}

/* Deliver any retained messages that match newly subscribed filter */
static void check_and_deliver_retained(mqtt_server_t *srv, client_conn_t *client, const char *sub_filter, uint8_t granted_qos) {
    if (!srv || !client || !sub_filter) return;

    pthread_mutex_lock(&srv->retained_store.lock);
    retained_msg_t *curr = srv->retained_store.head;
    while (curr) {
        /* Check if this retained message topic matches the filter */
        match_result_t m;
        trie_node_t *temp_root = trie_node_create("");
        trie_subscribe(temp_root, sub_filter, client->client_id, client->fd, granted_qos);
        size_t matches = trie_match_subscribers(temp_root, curr->topic, &m);
        trie_node_free(temp_root);

        if (matches > 0) {
            log_info("[RETAIN] Delivering retained message on topic '%s' to client '%s'", curr->topic, client->client_id);
            uint8_t delivery_qos = (curr->qos < granted_qos) ? curr->qos : granted_qos;
            deliver_publish_to_client(srv, client, curr->topic, curr->payload, curr->payload_len, delivery_qos, 1, 1);
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&srv->retained_store.lock);
}

static void process_mqtt_packet(mqtt_server_t *srv, client_conn_t *client, const mqtt_packet_t *pkt) {
    srv->total_packets_received++;
    srv->telemetry.total_packets_rx++;
    client->last_active_time = time(NULL);

    log_info("[MQTT RECV] %s packet (size: %zu bytes) from [%s:%u]",
             mqtt_packet_type_str(pkt->fixed_header.type),
             pkt->fixed_header.total_packet_size,
             client->ip, client->port);

    switch (pkt->fixed_header.type) {
        case MQTT_PKT_CONNECT: {
            if (client->state == CONN_STATE_CONNECTED) {
                log_warn("Protocol violation: Client [%s:%u] sent duplicate CONNECT packet", client->ip, client->port);
                server_disconnect_client(srv, client, false);
                return;
            }

            char effective_cid[MQTT_MAX_CLIENT_ID_LEN];
            if (pkt->connect.client_id[0] != '\0') {
                snprintf(effective_cid, sizeof(effective_cid), "%s", pkt->connect.client_id);
            } else {
                /* Anonymous client with clean_session = 1 */
                if (!pkt->connect.clean_session) {
                    log_warn("Protocol violation: 0-length client_id requires clean_session=1");
                    uint8_t connack_buf[4];
                    size_t enc_len = mqtt_encode_connack(connack_buf, sizeof(connack_buf), 0, MQTT_CONNACK_REFUSED_IDENTIFIER_REJECTED);
                    server_send_bytes(srv, client, connack_buf, enc_len);
                    server_disconnect_client(srv, client, false);
                    return;
                }
                snprintf(effective_cid, sizeof(effective_cid), "anon-%d-%lu", client->fd, (unsigned long)time(NULL));
            }

            /* Duplicate ClientID handling per MQTT-3.1.4-2: disconnect existing active socket */
            pthread_mutex_lock(&srv->lock);
            client_conn_t *existing_client = find_client_by_id(srv, effective_cid);
            if (existing_client && existing_client != client) {
                log_warn("[SESSION] Disconnecting older active connection for duplicate ClientID '%s'", effective_cid);
                pthread_mutex_unlock(&srv->lock);
                server_disconnect_client(srv, existing_client, true);
                pthread_mutex_lock(&srv->lock);
            }
            pthread_mutex_unlock(&srv->lock);

            snprintf(client->client_id, sizeof(client->client_id), "%s", effective_cid);
            client->clean_session = pkt->connect.clean_session;
            client->keep_alive_sec = pkt->connect.keep_alive;
            client->state = CONN_STATE_CONNECTED;

            /* Session Registry: Connect or Resume */
            uint8_t session_present = 0;
            mqtt_session_t *sess = session_connect(&srv->session_registry, client->client_id,
                                                   client->fd, client->clean_session,
                                                   client->keep_alive_sec, &session_present);

            if (!sess) {
                log_error("Failed to establish session for '%s'", client->client_id);
                server_disconnect_client(srv, client, false);
                return;
            }

            /* Store Last Will & Testament if requested */
            if (pkt->connect.will_flag) {
                session_set_will(sess, pkt->connect.will_topic,
                                 pkt->connect.will_message, pkt->connect.will_message_len,
                                 pkt->connect.will_qos, pkt->connect.will_retain);
                log_info("[SESSION] Configured Last Will for client '%s' on topic '%s'",
                         client->client_id, pkt->connect.will_topic);
            }

            /* If persistent session resumed, restore its subscriptions into the Topic Trie */
            if (session_present && sess->sub_count > 0) {
                for (size_t i = 0; i < sess->sub_count; i++) {
                    trie_subscribe(srv->topic_trie, sess->subscriptions[i].topic_filter,
                                   client->client_id, client->fd, sess->subscriptions[i].qos);
                }
                log_info("[SESSION] Restored %zu subscriptions for resumed client '%s'",
                         sess->sub_count, client->client_id);
            }

            log_info("[HANDSHAKE] CONNECT accepted for ClientID: '%s' | CleanSession: %u | KeepAlive: %u s | Present: %u",
                     client->client_id, client->clean_session, client->keep_alive_sec, session_present);

            /* Send CONNACK */
            uint8_t connack_buf[4];
            size_t enc_len = mqtt_encode_connack(connack_buf, sizeof(connack_buf), session_present, MQTT_CONNACK_ACCEPTED);
            server_send_bytes(srv, client, connack_buf, enc_len);
            srv->total_packets_sent++;
            srv->telemetry.total_packets_tx++;

            /* If resumed session has queued offline messages, flush them now */
            if (session_present && sess->queue_count > 0) {
                log_info("[SESSION] Flushing %zu queued offline messages to reconnected client '%s'",
                         sess->queue_count, client->client_id);
                while (sess->queue_count > 0) {
                    queued_message_t *msg = &sess->offline_queue[sess->queue_head];
                    deliver_publish_to_client(srv, client, msg->topic, msg->payload,
                                              msg->payload_len, msg->qos, msg->retain, msg->packet_id);
                    if (msg->payload) {
                        free(msg->payload);
                        msg->payload = NULL;
                    }
                    sess->queue_head = (sess->queue_head + 1) % SESSION_MAX_OFFLINE_QUEUE;
                    sess->queue_count--;
                }
            }
            break;
        }

        case MQTT_PKT_PINGREQ: {
            log_debug("[HEARTBEAT] PINGREQ received from [%s:%u]", client->ip, client->port);
            uint8_t pingresp_buf[2];
            size_t enc_len = mqtt_encode_pingresp(pingresp_buf, sizeof(pingresp_buf));
            server_send_bytes(srv, client, pingresp_buf, enc_len);
            srv->total_packets_sent++;
            srv->telemetry.total_packets_tx++;
            break;
        }

        case MQTT_PKT_PUBLISH: {
            log_info("[PUBLISH] Topic: '%s' | QoS: %u | Retain: %u | Dup: %u | PayloadSize: %zu bytes",
                     pkt->publish.topic, pkt->publish.qos, pkt->publish.retain, pkt->publish.dup,
                     pkt->publish.payload_len);

            /* Optional Decryption of Payload if crypto enabled */
            const uint8_t *routed_payload = pkt->publish.payload;
            size_t routed_payload_len = pkt->publish.payload_len;
            uint8_t dec_payload[MQTT_MAX_BUFFER_SIZE];

            if (srv->crypto_enabled && pkt->publish.payload_len > 28) {
                size_t dlen = crypto_decrypt_payload(srv->crypto_key, pkt->publish.payload,
                                                    pkt->publish.payload_len, dec_payload, sizeof(dec_payload));
                if (dlen > 0) {
                    log_info("[CRYPTO] Authenticated & Decrypted payload (%zu -> %zu bytes)",
                             pkt->publish.payload_len, dlen);
                    routed_payload = dec_payload;
                    routed_payload_len = dlen;
                } else {
                    log_warn("[CRYPTO] Payload decryption/MAC verification failed! Dropping message.");
                    return;
                }
            }

            /* Route message to Topic Trie subscribers (Payload Fan-out) */
            server_route_publish(srv, pkt->publish.topic, routed_payload, routed_payload_len,
                                 pkt->publish.qos, pkt->publish.retain, pkt->publish.packet_id,
                                 client->client_id);

            /* If QoS 1, respond with PUBACK */
            if (pkt->publish.qos == MQTT_QOS_1) {
                uint8_t puback_buf[4];
                size_t enc_len = mqtt_encode_puback(puback_buf, sizeof(puback_buf), pkt->publish.packet_id);
                server_send_bytes(srv, client, puback_buf, enc_len);
                srv->total_packets_sent++;
                srv->telemetry.total_packets_tx++;
                log_info("[MQTT SEND] PUBACK (PacketID: %u) sent to [%s:%u]", pkt->publish.packet_id, client->ip, client->port);
            }
            break;
        }

        case MQTT_PKT_SUBSCRIBE: {
            log_info("[SUBSCRIBE] PacketID: %u | Topics count: %zu", pkt->subscribe.packet_id, pkt->subscribe.topic_count);
            uint8_t granted_qos[MQTT_MAX_SUB_TOPICS];

            for (size_t i = 0; i < pkt->subscribe.topic_count; i++) {
                const char *filter = pkt->subscribe.topics[i].topic_filter;
                uint8_t qos = pkt->subscribe.topics[i].qos;
                granted_qos[i] = qos;

                /* Register into Topic Trie */
                trie_subscribe(srv->topic_trie, filter, client->client_id, client->fd, qos);

                /* Register into client persistent session if active */
                mqtt_session_t *sess = session_find(&srv->session_registry, client->client_id);
                if (sess) {
                    session_add_subscription(sess, filter, qos);
                }

                srv->telemetry.total_subscribes++;
                log_info("  -> Subscribed filter: '%s' (Granted QoS: %u)", filter, qos);
            }

            /* Send SUBACK first */
            uint8_t suback_buf[64];
            size_t enc_len = mqtt_encode_suback(suback_buf, sizeof(suback_buf), pkt->subscribe.packet_id, granted_qos, pkt->subscribe.topic_count);
            server_send_bytes(srv, client, suback_buf, enc_len);
            srv->total_packets_sent++;
            srv->telemetry.total_packets_tx++;
            log_info("[MQTT SEND] SUBACK (PacketID: %u) sent to [%s:%u]", pkt->subscribe.packet_id, client->ip, client->port);

            /* Check for matching Retained messages and deliver after SUBACK */
            for (size_t i = 0; i < pkt->subscribe.topic_count; i++) {
                check_and_deliver_retained(srv, client, pkt->subscribe.topics[i].topic_filter, granted_qos[i]);
            }
            break;
        }

        case MQTT_PKT_UNSUBSCRIBE: {
            log_info("[UNSUBSCRIBE] PacketID: %u | Topics count: %zu", pkt->unsubscribe.packet_id, pkt->unsubscribe.topic_count);
            for (size_t i = 0; i < pkt->unsubscribe.topic_count; i++) {
                const char *filter = pkt->unsubscribe.topics[i];
                trie_unsubscribe(srv->topic_trie, filter, client->client_id);

                mqtt_session_t *sess = session_find(&srv->session_registry, client->client_id);
                if (sess) {
                    session_remove_subscription(sess, filter);
                }
                srv->telemetry.total_unsubscribes++;
                log_info("  -> Unsubscribed from: '%s'", filter);
            }

            /* Send UNSUBACK (Type 11, 2-byte remaining length with Packet ID) */
            uint8_t unsuback_buf[4];
            unsuback_buf[0] = 0xB0;
            unsuback_buf[1] = 0x02;
            unsuback_buf[2] = (uint8_t)((pkt->unsubscribe.packet_id >> 8) & 0xFF);
            unsuback_buf[3] = (uint8_t)(pkt->unsubscribe.packet_id & 0xFF);
            server_send_bytes(srv, client, unsuback_buf, 4);
            srv->total_packets_sent++;
            srv->telemetry.total_packets_tx++;
            log_info("[MQTT SEND] UNSUBACK (PacketID: %u) sent to [%s:%u]", pkt->unsubscribe.packet_id, client->ip, client->port);
            break;
        }

        case MQTT_PKT_DISCONNECT: {
            log_info("[DISCONNECT] Clean disconnect received from ClientID: '%s' [%s:%u]",
                     client->client_id, client->ip, client->port);
            server_disconnect_client(srv, client, true);
            break;
        }

        case MQTT_PKT_PUBACK: {
            log_info("[PUBACK] Acknowledgment received for PacketID: %u from [%s:%u]",
                     pkt->ack.packet_id, client->ip, client->port);
            break;
        }

        default:
            log_info("Received control packet type %u from [%s:%u]", pkt->fixed_header.type, client->ip, client->port);
            break;
    }
}

static void handle_client_read(mqtt_server_t *srv, client_conn_t *client) {
    if (!srv || !client || client->fd < 0) return;

    uint8_t read_chunk[SERVER_READ_CHUNK_SIZE];

    /* Non-blocking read loop: read all available bytes until EAGAIN/EWOULDBLOCK */
    while (true) {
        ssize_t n = recv(client->fd, read_chunk, sizeof(read_chunk), 0);
        if (n > 0) {
            /* Token Bucket Traffic Shaping / Throttling */
            if (!client_throttle_consume(&client->throttle, (size_t)n)) {
                log_warn("[THROTTLE] Rate limit exceeded for client [%s:%u] (dropping chunk of %zd bytes)",
                         client->ip, client->port, n);
                srv->telemetry.total_throttled_packets++;
                continue;
            }

            srv->total_bytes_received += (size_t)n;
            srv->telemetry.total_bytes_rx += (size_t)n;
            client->last_active_time = time(NULL);

            if (!byte_buffer_append(&client->rx_buffer, read_chunk, (size_t)n)) {
                log_warn("RX buffer overflow for client %s:%u (exceeded max %zu bytes)",
                         client->ip, client->port, client->rx_buffer.max_capacity);
                server_disconnect_client(srv, client, false);
                return;
            }
        } else if (n == 0) {
            /* Client cleanly closed connection (EOF) */
            log_info("TCP connection closed by peer [%s:%u]", client->ip, client->port);
            server_disconnect_client(srv, client, false);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Drained all data on non-blocking socket */
                break;
            } else {
                log_warn("TCP read error on [%s:%u]: %s", client->ip, client->port, strerror(errno));
                server_disconnect_client(srv, client, false);
                return;
            }
        }
    }

    /* TCP Stream Deframing & Packet Parser Loop */
    while (client->rx_buffer.size > 0) {
        mqtt_packet_t pkt;
        int rc = mqtt_decode_packet(client->rx_buffer.data, client->rx_buffer.size, &pkt);

        if (rc == MQTT_NEED_MORE_DATA) {
            /* Stream fragment: wait for more bytes to arrive in subsequent TCP reads */
            log_debug("Partial packet in stream buffer (%zu bytes accumulated), awaiting more data", client->rx_buffer.size);
            break;
        } else if (rc == MQTT_SUCCESS) {
            /* Complete packet framed! Process it */
            size_t pkt_len = pkt.fixed_header.total_packet_size;
            process_mqtt_packet(srv, client, &pkt);

            /* Drain packet from stream accumulator */
            byte_buffer_drain(&client->rx_buffer, pkt_len);
        } else {
            /* Malformed packet or protocol violation */
            log_warn("MQTT protocol decode error (%d) from client %s:%u. Terminating connection per spec.",
                     rc, client->ip, client->port);
            server_disconnect_client(srv, client, false);
            return;
        }
    }
}

static void handle_new_connection(mqtt_server_t *srv) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    /* Accept all pending connections in a loop */
    while (true) {
        int client_fd = accept4(srv->listen_fd, (struct sockaddr *)&client_addr, &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; /* All incoming connections accepted */
            }
            log_warn("Failed to accept connection: %s", strerror(errno));
            break;
        }

        /* Disable Nagle's algorithm for low-latency MQTT delivery */
        int flag = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        uint16_t port = ntohs(client_addr.sin_port);

        pthread_mutex_lock(&srv->lock);
        if (srv->active_client_count >= SERVER_MAX_CLIENTS) {
            log_warn("Server max connection limit (%d) reached. Rejecting client %s:%u",
                     SERVER_MAX_CLIENTS, ip_str, port);
            close(client_fd);
            pthread_mutex_unlock(&srv->lock);
            continue;
        }

        client_conn_t *client = create_client(client_fd, ip_str, port, &srv->throttle_cfg);
        if (!client) {
            log_error("Failed to allocate client context for %s:%u", ip_str, port);
            close(client_fd);
            pthread_mutex_unlock(&srv->lock);
            continue;
        }

        /* Insert client into registry */
        for (size_t i = 0; i < SERVER_MAX_CLIENTS; i++) {
            if (srv->clients[i] == NULL) {
                srv->clients[i] = client;
                srv->active_client_count++;
                srv->total_connections_accepted++;
                srv->telemetry.total_connections++;
                srv->telemetry.current_active_clients = srv->active_client_count;
                break;
            }
        }

        /* Register in epoll with level-triggered I/O */
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        ev.data.ptr = client;

        if (epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            log_error("Failed to add client fd %d to epoll: %s", client_fd, strerror(errno));
            destroy_client(client);
            pthread_mutex_unlock(&srv->lock);
            continue;
        }

        log_info("New TCP connection accepted from [%s:%u] (fd: %d, total active: %zu)",
                 ip_str, port, client_fd, srv->active_client_count);
        pthread_mutex_unlock(&srv->lock);
    }
}

/* Check keep-alive watchdog timers for all active clients */
static void check_keepalive_watchdogs(mqtt_server_t *srv) {
    if (!srv) return;
    time_t now = time(NULL);

    for (size_t i = 0; i < SERVER_MAX_CLIENTS; i++) {
        client_conn_t *c = srv->clients[i];
        if (!c || c->fd < 0 || c->keep_alive_sec == 0) continue;

        /* MQTT-3.1.2-10: Client must be disconnected if inactive for 1.5 * keep_alive */
        time_t timeout_limit = (time_t)(c->keep_alive_sec * 1.5);
        if (now - c->last_active_time > timeout_limit) {
            log_warn("[WATCHDOG] Keep-alive timeout (%ld s > %ld s) for client '%s' [%s:%u]",
                     (long)(now - c->last_active_time), (long)timeout_limit,
                     c->client_id, c->ip, c->port);
            server_disconnect_client(srv, c, false); /* Ungraceful drop -> triggers LWT! */
        }
    }
}

void server_run(mqtt_server_t *srv) {
    if (!srv || srv->epoll_fd < 0) return;

    srv->running = true;
    struct epoll_event events[SERVER_EPOLL_MAX_EVENTS];

    log_info("Entering epoll event loop. Ready to accept MQTT connections...");

    time_t last_watchdog_check = time(NULL);
    time_t last_telemetry_dump = time(NULL);

    while (srv->running) {
        int nready = epoll_wait(srv->epoll_fd, events, SERVER_EPOLL_MAX_EVENTS, 100);
        if (nready < 0) {
            if (errno == EINTR) {
                continue; /* Interrupted by signal (e.g., SIGINT) */
            }
            log_error("epoll_wait failure: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < nready; i++) {
            void *ptr = events[i].data.ptr;

            /* Check if this event is on the master listening socket */
            if (ptr == srv) {
                handle_new_connection(srv);
                continue;
            }

            /* Event on client connection */
            client_conn_t *client = (client_conn_t *)ptr;
            if (!client) continue;

            uint32_t ev = events[i].events;

            if (ev & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
                log_info("Client [%s:%u] disconnected or error event (0x%04X)", client->ip, client->port, ev);
                server_disconnect_client(srv, client, false);
                continue;
            }

            if (ev & EPOLLIN) {
                handle_client_read(srv, client);
            }

            if (ev & EPOLLOUT) {
                handle_client_write(srv, client);
            }
        }

        /* Periodic 1-second background maintenance */
        time_t now = time(NULL);
        if (now - last_watchdog_check >= 1) {
            check_keepalive_watchdogs(srv);
            last_watchdog_check = now;
        }

        /* Periodic 10-second telemetry dump */
        if (now - last_telemetry_dump >= 10) {
            if (srv->active_client_count > 0 || srv->telemetry.total_packets_rx > 0) {
                telemetry_dump_cli(&srv->telemetry);
            }
            last_telemetry_dump = now;
        }
    }

    log_info("Exited epoll event loop.");
}

void server_stop(mqtt_server_t *srv) {
    if (srv) {
        srv->running = false;
    }
}

void server_cleanup(mqtt_server_t *srv) {
    if (!srv) return;

    log_info("Cleaning up server resources...");
    srv->running = false;

    pthread_mutex_lock(&srv->lock);

    /* Close all active client connections */
    for (size_t i = 0; i < SERVER_MAX_CLIENTS; i++) {
        if (srv->clients[i]) {
            destroy_client(srv->clients[i]);
            srv->clients[i] = NULL;
        }
    }
    srv->active_client_count = 0;

    if (srv->listen_fd >= 0) {
        close(srv->listen_fd);
        srv->listen_fd = -1;
    }

    if (srv->epoll_fd >= 0) {
        close(srv->epoll_fd);
        srv->epoll_fd = -1;
    }

    /* Cleanup Trie, Sessions, Retained, Telemetry */
    if (srv->topic_trie) {
        trie_node_free(srv->topic_trie);
        srv->topic_trie = NULL;
    }
    session_registry_cleanup(&srv->session_registry);
    retained_store_cleanup(&srv->retained_store);
    telemetry_cleanup(&srv->telemetry);

    pthread_mutex_unlock(&srv->lock);
    pthread_mutex_destroy(&srv->lock);

    log_info("Server cleanup complete.");
}
