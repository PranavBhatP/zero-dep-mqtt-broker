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

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static client_conn_t *create_client(int fd, const char *ip, uint16_t port) {
    client_conn_t *client = (client_conn_t *)calloc(1, sizeof(client_conn_t));
    if (!client) return NULL;

    client->fd = fd;
    snprintf(client->ip, sizeof(client->ip), "%s", ip);
    client->port = port;
    client->state = CONN_STATE_NEW;
    client->last_active_time = time(NULL);

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

static void close_client(mqtt_server_t *srv, client_conn_t *client) {
    if (!srv || !client) return;

    pthread_mutex_lock(&srv->lock);

    log_info("Closing client [%s:%u] (ClientID: '%s')",
             client->ip, client->port, 
             client->client_id[0] ? client->client_id : "unauthenticated");

    /* Deregister from epoll */
    if (client->fd >= 0 && srv->epoll_fd >= 0) {
        epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    }

    /* Remove from client tracking list */
    for (size_t i = 0; i < SERVER_MAX_CLIENTS; i++) {
        if (srv->clients[i] == client) {
            srv->clients[i] = NULL;
            if (srv->active_client_count > 0) {
                srv->active_client_count--;
            }
            break;
        }
    }

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

    log_info("Non-blocking TCP server initialized on %s:%u (epoll_fd: %d, listen_fd: %d)",
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
        close_client(srv, client);
    }
}

static void process_mqtt_packet(mqtt_server_t *srv, client_conn_t *client, const mqtt_packet_t *pkt) {
    srv->total_packets_received++;
    client->last_active_time = time(NULL);

    log_info("[MQTT RECV] %s packet (size: %zu bytes) from [%s:%u]",
             mqtt_packet_type_str(pkt->fixed_header.type),
             pkt->fixed_header.total_packet_size,
             client->ip, client->port);

    switch (pkt->fixed_header.type) {
        case MQTT_PKT_CONNECT: {
            if (client->state == CONN_STATE_CONNECTED) {
                log_warn("Protocol violation: Client [%s:%u] sent duplicate CONNECT packet", client->ip, client->port);
                close_client(srv, client);
                return;
            }

            snprintf(client->client_id, sizeof(client->client_id), "%s", pkt->connect.client_id);
            client->clean_session = pkt->connect.clean_session;
            client->keep_alive_sec = pkt->connect.keep_alive;
            client->state = CONN_STATE_CONNECTED;

            log_info("[HANDSHAKE] CONNECT accepted for ClientID: '%s' | CleanSession: %u | KeepAlive: %u s | Protocol: %s v%u",
                     client->client_id, client->clean_session, client->keep_alive_sec,
                     pkt->connect.protocol_name, pkt->connect.protocol_level);

            /* Send CONNACK (Return Code 0: Accepted) */
            uint8_t connack_buf[4];
            size_t enc_len = mqtt_encode_connack(connack_buf, sizeof(connack_buf), 0, MQTT_CONNACK_ACCEPTED);
            server_send_bytes(srv, client, connack_buf, enc_len);
            srv->total_packets_sent++;
            log_info("[MQTT SEND] CONNACK sent to [%s:%u]", client->ip, client->port);
            break;
        }

        case MQTT_PKT_PINGREQ: {
            log_debug("[HEARTBEAT] PINGREQ received from [%s:%u]", client->ip, client->port);
            uint8_t pingresp_buf[2];
            size_t enc_len = mqtt_encode_pingresp(pingresp_buf, sizeof(pingresp_buf));
            server_send_bytes(srv, client, pingresp_buf, enc_len);
            srv->total_packets_sent++;
            log_debug("[MQTT SEND] PINGRESP sent to [%s:%u]", client->ip, client->port);
            break;
        }

        case MQTT_PKT_PUBLISH: {
            log_info("[PUBLISH] Topic: '%s' | QoS: %u | Retain: %u | Dup: %u | PayloadSize: %zu bytes",
                     pkt->publish.topic, pkt->publish.qos, pkt->publish.retain, pkt->publish.dup,
                     pkt->publish.payload_len);

            if (logger_get_level() == LOG_LEVEL_DEBUG && pkt->publish.payload_len > 0) {
                log_hex_dump("PUBLISH Payload", pkt->publish.payload, pkt->publish.payload_len);
            }

            /* If QoS 1, respond with PUBACK */
            if (pkt->publish.qos == MQTT_QOS_1) {
                uint8_t puback_buf[4];
                size_t enc_len = mqtt_encode_puback(puback_buf, sizeof(puback_buf), pkt->publish.packet_id);
                server_send_bytes(srv, client, puback_buf, enc_len);
                srv->total_packets_sent++;
                log_info("[MQTT SEND] PUBACK (PacketID: %u) sent to [%s:%u]", pkt->publish.packet_id, client->ip, client->port);
            }
            break;
        }

        case MQTT_PKT_SUBSCRIBE: {
            log_info("[SUBSCRIBE] PacketID: %u | Topics count: %zu", pkt->subscribe.packet_id, pkt->subscribe.topic_count);
            uint8_t granted_qos[MQTT_MAX_SUB_TOPICS];
            for (size_t i = 0; i < pkt->subscribe.topic_count; i++) {
                log_info("  -> Subscribed filter: '%s' (Requested QoS: %u)",
                         pkt->subscribe.topics[i].topic_filter, pkt->subscribe.topics[i].qos);
                granted_qos[i] = pkt->subscribe.topics[i].qos; /* Grant requested QoS */
            }

            /* Send SUBACK */
            uint8_t suback_buf[64];
            size_t enc_len = mqtt_encode_suback(suback_buf, sizeof(suback_buf), pkt->subscribe.packet_id, granted_qos, pkt->subscribe.topic_count);
            server_send_bytes(srv, client, suback_buf, enc_len);
            srv->total_packets_sent++;
            log_info("[MQTT SEND] SUBACK (PacketID: %u) sent to [%s:%u]", pkt->subscribe.packet_id, client->ip, client->port);
            break;
        }

        case MQTT_PKT_DISCONNECT: {
            log_info("[DISCONNECT] Clean disconnect received from ClientID: '%s' [%s:%u]",
                     client->client_id, client->ip, client->port);
            close_client(srv, client);
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
            srv->total_bytes_received += (size_t)n;
            client->last_active_time = time(NULL);

            if (!byte_buffer_append(&client->rx_buffer, read_chunk, (size_t)n)) {
                log_warn("RX buffer overflow for client %s:%u (exceeded max %zu bytes)",
                         client->ip, client->port, client->rx_buffer.max_capacity);
                close_client(srv, client);
                return;
            }

            if (logger_get_level() == LOG_LEVEL_DEBUG) {
                log_hex_dump("Received TCP stream chunk", read_chunk, (size_t)n);
            }
        } else if (n == 0) {
            /* Client cleanly closed connection (EOF) */
            log_info("TCP connection closed by peer [%s:%u]", client->ip, client->port);
            close_client(srv, client);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Drained all data on non-blocking socket */
                break;
            } else {
                log_warn("TCP read error on [%s:%u]: %s", client->ip, client->port, strerror(errno));
                close_client(srv, client);
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
            close_client(srv, client);
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

        client_conn_t *client = create_client(client_fd, ip_str, port);
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
            close_client(srv, client);
            pthread_mutex_unlock(&srv->lock);
            continue;
        }

        log_info("New TCP connection accepted from [%s:%u] (fd: %d, total active: %zu)",
                 ip_str, port, client_fd, srv->active_client_count);
        pthread_mutex_unlock(&srv->lock);
    }
}

void server_run(mqtt_server_t *srv) {
    if (!srv || srv->epoll_fd < 0) return;

    srv->running = true;
    struct epoll_event events[SERVER_EPOLL_MAX_EVENTS];

    log_info("Entering epoll event loop. Ready to accept MQTT connections...");

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
                close_client(srv, client);
                continue;
            }

            if (ev & EPOLLIN) {
                handle_client_read(srv, client);
            }

            if (ev & EPOLLOUT) {
                handle_client_write(srv, client);
            }
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

    pthread_mutex_unlock(&srv->lock);
    pthread_mutex_destroy(&srv->lock);

    log_info("Server cleanup complete.");
}
