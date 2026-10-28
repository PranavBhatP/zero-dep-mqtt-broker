#include "retained.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

bool retained_store_init(retained_store_t *store) {
    if (!store) return false;
    memset(store, 0, sizeof(retained_store_t));
    if (pthread_mutex_init(&store->lock, NULL) != 0) {
        log_error("Failed to init retained store mutex");
        return false;
    }
    return true;
}

static void free_retained_msg(retained_msg_t *msg) {
    if (!msg) return;
    if (msg->payload) {
        free(msg->payload);
        msg->payload = NULL;
    }
    free(msg);
}

void retained_store_cleanup(retained_store_t *store) {
    if (!store) return;
    pthread_mutex_lock(&store->lock);
    retained_msg_t *curr = store->head;
    while (curr) {
        retained_msg_t *next = curr->next;
        free_retained_msg(curr);
        curr = next;
    }
    store->head = NULL;
    store->count = 0;
    pthread_mutex_unlock(&store->lock);
    pthread_mutex_destroy(&store->lock);
}

bool retained_store_put(retained_store_t *store, const char *topic,
                        const uint8_t *payload, size_t payload_len, uint8_t qos) {
    if (!store || !topic) return false;

    pthread_mutex_lock(&store->lock);

    retained_msg_t *curr = store->head;
    retained_msg_t *prev = NULL;

    while (curr) {
        if (strcmp(curr->topic, topic) == 0) {
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    /* MQTT-3.3.1-11: Zero-byte payload clears retained message */
    if (payload_len == 0) {
        if (curr) {
            if (prev) {
                prev->next = curr->next;
            } else {
                store->head = curr->next;
            }
            free_retained_msg(curr);
            store->count--;
            log_info("[RETAIN] Cleared retained message on topic '%s'", topic);
        }
        pthread_mutex_unlock(&store->lock);
        return true;
    }

    /* Update existing retained message */
    if (curr) {
        if (curr->payload) free(curr->payload);
        curr->payload = (uint8_t *)malloc(payload_len);
        if (curr->payload) {
            memcpy(curr->payload, payload, payload_len);
        }
        curr->payload_len = payload_len;
        curr->qos = qos;
        curr->timestamp = time(NULL);
        pthread_mutex_unlock(&store->lock);
        log_info("[RETAIN] Updated retained message on topic '%s' (%zu bytes)", topic, payload_len);
        return true;
    }

    /* Insert new retained message */
    if (store->count >= RETAINED_MAX_MESSAGES) {
        log_warn("Retained message store full, dropping oldest");
        /* Pop head */
        retained_msg_t *old = store->head;
        if (old) {
            store->head = old->next;
            free_retained_msg(old);
            store->count--;
        }
    }

    retained_msg_t *new_msg = (retained_msg_t *)calloc(1, sizeof(retained_msg_t));
    if (!new_msg) {
        pthread_mutex_unlock(&store->lock);
        return false;
    }

    snprintf(new_msg->topic, sizeof(new_msg->topic), "%s", topic);
    new_msg->payload = (uint8_t *)malloc(payload_len);
    if (new_msg->payload) {
        memcpy(new_msg->payload, payload, payload_len);
    }
    new_msg->payload_len = payload_len;
    new_msg->qos = qos;
    new_msg->timestamp = time(NULL);

    new_msg->next = store->head;
    store->head = new_msg;
    store->count++;

    pthread_mutex_unlock(&store->lock);
    log_info("[RETAIN] Stored new retained message on topic '%s' (%zu bytes)", topic, payload_len);
    return true;
}

const retained_msg_t *retained_store_get(retained_store_t *store, const char *topic) {
    if (!store || !topic) return NULL;
    pthread_mutex_lock(&store->lock);
    retained_msg_t *curr = store->head;
    while (curr) {
        if (strcmp(curr->topic, topic) == 0) {
            pthread_mutex_unlock(&store->lock);
            return curr;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&store->lock);
    return NULL;
}
