#define _GNU_SOURCE
#include "session.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

static uint32_t hash_client_id(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash % SESSION_HASH_SIZE;
}

bool session_registry_init(session_registry_t *reg) {
    if (!reg) return false;
    memset(reg, 0, sizeof(session_registry_t));
    if (pthread_mutex_init(&reg->lock, NULL) != 0) {
        log_error("Failed to initialize session registry mutex");
        return false;
    }
    return true;
}

static void free_session(mqtt_session_t *sess) {
    if (!sess) return;
    if (sess->will.payload) {
        free(sess->will.payload);
        sess->will.payload = NULL;
    }
    for (size_t i = 0; i < SESSION_MAX_OFFLINE_QUEUE; i++) {
        if (sess->offline_queue[i].payload) {
            free(sess->offline_queue[i].payload);
            sess->offline_queue[i].payload = NULL;
        }
    }
    free(sess);
}

void session_registry_cleanup(session_registry_t *reg) {
    if (!reg) return;

    pthread_mutex_lock(&reg->lock);
    for (size_t i = 0; i < SESSION_HASH_SIZE; i++) {
        mqtt_session_t *curr = reg->buckets[i];
        while (curr) {
            mqtt_session_t *next = curr->next;
            free_session(curr);
            curr = next;
        }
        reg->buckets[i] = NULL;
    }
    reg->total_sessions = 0;
    reg->active_connected_sessions = 0;
    pthread_mutex_unlock(&reg->lock);
    pthread_mutex_destroy(&reg->lock);
}

mqtt_session_t *session_find(session_registry_t *reg, const char *client_id) {
    if (!reg || !client_id) return NULL;
    uint32_t idx = hash_client_id(client_id);

    pthread_mutex_lock(&reg->lock);
    mqtt_session_t *curr = reg->buckets[idx];
    while (curr) {
        if (strcmp(curr->client_id, client_id) == 0) {
            pthread_mutex_unlock(&reg->lock);
            return curr;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&reg->lock);
    return NULL;
}

mqtt_session_t *session_connect(session_registry_t *reg, const char *client_id,
                                int socket_fd, uint8_t clean_session,
                                uint16_t keep_alive, uint8_t *out_session_present) {
    if (!reg || !client_id || !out_session_present) return NULL;

    uint32_t idx = hash_client_id(client_id);
    *out_session_present = 0;

    pthread_mutex_lock(&reg->lock);

    mqtt_session_t *curr = reg->buckets[idx];
    mqtt_session_t *prev = NULL;

    while (curr) {
        if (strcmp(curr->client_id, client_id) == 0) {
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (curr) {
        /* Existing session found */
        if (clean_session) {
            /* Client requested clean session: discard old session state */
            log_info("[SESSION] Discarding previous session state for ClientID '%s' (clean_session=1)", client_id);
            if (prev) {
                prev->next = curr->next;
            } else {
                reg->buckets[idx] = curr->next;
            }
            if (curr->is_connected && reg->active_connected_sessions > 0) {
                reg->active_connected_sessions--;
            }
            reg->total_sessions--;
            free_session(curr);
            curr = NULL;
        } else {
            /* Persistent session resumed! */
            log_info("[SESSION] Resuming persistent session for ClientID '%s' (clean_session=0, %zu subs)",
                     client_id, curr->sub_count);
            curr->socket_fd = socket_fd;
            curr->is_connected = true;
            curr->keep_alive_sec = keep_alive;
            curr->last_active_time = time(NULL);
            *out_session_present = 1;
            reg->active_connected_sessions++;
            pthread_mutex_unlock(&reg->lock);
            return curr;
        }
    }

    /* Create brand new session */
    mqtt_session_t *new_sess = (mqtt_session_t *)calloc(1, sizeof(mqtt_session_t));
    if (!new_sess) {
        pthread_mutex_unlock(&reg->lock);
        return NULL;
    }

    snprintf(new_sess->client_id, sizeof(new_sess->client_id), "%s", client_id);
    new_sess->socket_fd = socket_fd;
    new_sess->is_connected = true;
    new_sess->clean_session = clean_session;
    new_sess->keep_alive_sec = keep_alive;
    new_sess->last_active_time = time(NULL);
    new_sess->created_time = time(NULL);

    /* Insert into hash bucket */
    new_sess->next = reg->buckets[idx];
    reg->buckets[idx] = new_sess;

    reg->total_sessions++;
    reg->active_connected_sessions++;
    *out_session_present = 0;

    pthread_mutex_unlock(&reg->lock);
    return new_sess;
}

void session_disconnect(session_registry_t *reg, const char *client_id, bool graceful) {
    if (!reg || !client_id) return;
    uint32_t idx = hash_client_id(client_id);

    pthread_mutex_lock(&reg->lock);
    mqtt_session_t *curr = reg->buckets[idx];
    mqtt_session_t *prev = NULL;

    while (curr) {
        if (strcmp(curr->client_id, client_id) == 0) {
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (!curr) {
        pthread_mutex_unlock(&reg->lock);
        return;
    }

    if (curr->is_connected && reg->active_connected_sessions > 0) {
        reg->active_connected_sessions--;
    }
    curr->is_connected = false;
    curr->socket_fd = -1;

    if (graceful) {
        /* On clean disconnect, clear will */
        session_clear_will(curr);
    }

    if (curr->clean_session) {
        /* Clean session: purge completely from memory */
        if (prev) {
            prev->next = curr->next;
        } else {
            reg->buckets[idx] = curr->next;
        }
        if (reg->total_sessions > 0) {
            reg->total_sessions--;
        }
        free_session(curr);
    } else {
        log_info("[SESSION] Retaining offline session for ClientID '%s' (%zu subs)", client_id, curr->sub_count);
    }

    pthread_mutex_unlock(&reg->lock);
}

bool session_add_subscription(mqtt_session_t *session, const char *topic_filter, uint8_t qos) {
    if (!session || !topic_filter) return false;

    /* Check if already present; update QoS */
    for (size_t i = 0; i < session->sub_count; i++) {
        if (strcmp(session->subscriptions[i].topic_filter, topic_filter) == 0) {
            session->subscriptions[i].qos = qos;
            return true;
        }
    }

    if (session->sub_count >= SESSION_MAX_SUBSCRIPTIONS) {
        log_warn("Session %s reached max subscriptions limit", session->client_id);
        return false;
    }

    snprintf(session->subscriptions[session->sub_count].topic_filter,
             sizeof(session->subscriptions[session->sub_count].topic_filter),
             "%s", topic_filter);
    session->subscriptions[session->sub_count].qos = qos;
    session->sub_count++;
    return true;
}

bool session_remove_subscription(mqtt_session_t *session, const char *topic_filter) {
    if (!session || !topic_filter) return false;

    for (size_t i = 0; i < session->sub_count; i++) {
        if (strcmp(session->subscriptions[i].topic_filter, topic_filter) == 0) {
            /* Shift remaining subscriptions */
            for (size_t j = i; j < session->sub_count - 1; j++) {
                session->subscriptions[j] = session->subscriptions[j + 1];
            }
            session->sub_count--;
            return true;
        }
    }
    return false;
}

void session_set_will(mqtt_session_t *session, const char *topic,
                      const uint8_t *payload, size_t payload_len,
                      uint8_t qos, uint8_t retain) {
    if (!session || !topic) return;
    session_clear_will(session);

    session->will.present = true;
    snprintf(session->will.topic, sizeof(session->will.topic), "%s", topic);
    session->will.qos = qos;
    session->will.retain = retain;
    session->will.payload_len = payload_len;

    if (payload && payload_len > 0) {
        session->will.payload = (uint8_t *)malloc(payload_len);
        if (session->will.payload) {
            memcpy(session->will.payload, payload, payload_len);
        }
    }
}

void session_clear_will(mqtt_session_t *session) {
    if (!session) return;
    session->will.present = false;
    session->will.topic[0] = '\0';
    if (session->will.payload) {
        free(session->will.payload);
        session->will.payload = NULL;
    }
    session->will.payload_len = 0;
}

bool session_queue_offline_message(mqtt_session_t *session, const char *topic,
                                  const uint8_t *payload, size_t payload_len,
                                  uint8_t qos, uint8_t retain, uint16_t packet_id) {
    if (!session || !topic || session->is_connected) return false;
    if (session->queue_count >= SESSION_MAX_OFFLINE_QUEUE) {
        log_warn("Offline queue full for client %s, dropping oldest", session->client_id);
        size_t oldest = session->queue_head;
        if (session->offline_queue[oldest].payload) {
            free(session->offline_queue[oldest].payload);
            session->offline_queue[oldest].payload = NULL;
        }
        session->queue_head = (session->queue_head + 1) % SESSION_MAX_OFFLINE_QUEUE;
        session->queue_count--;
    }

    size_t idx = session->queue_tail;
    queued_message_t *msg = &session->offline_queue[idx];
    snprintf(msg->topic, sizeof(msg->topic), "%s", topic);
    msg->qos = qos;
    msg->retain = retain;
    msg->packet_id = packet_id;
    msg->payload_len = payload_len;

    if (payload && payload_len > 0) {
        msg->payload = (uint8_t *)malloc(payload_len);
        if (msg->payload) {
            memcpy(msg->payload, payload, payload_len);
        }
    } else {
        msg->payload = NULL;
    }

    session->queue_tail = (session->queue_tail + 1) % SESSION_MAX_OFFLINE_QUEUE;
    session->queue_count++;
    return true;
}
