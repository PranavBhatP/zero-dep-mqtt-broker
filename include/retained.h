#ifndef RETAINED_H
#define RETAINED_H

#include "mqtt_protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

/**
 * @file retained.h
 * @brief Thread-safe in-memory store for MQTT Retained messages
 * 
 * Course: IT365 Advanced Computer Networks
 * Project: Zero-Dependency MQTT Broker
 */

#define RETAINED_MAX_MESSAGES 512

typedef struct retained_msg {
    char     topic[MQTT_MAX_TOPIC_LEN];
    uint8_t *payload;
    size_t   payload_len;
    uint8_t  qos;
    time_t   timestamp;
    struct retained_msg *next;
} retained_msg_t;

typedef struct {
    retained_msg_t  *head;
    size_t           count;
    pthread_mutex_t  lock;
} retained_store_t;

bool retained_store_init(retained_store_t *store);
void retained_store_cleanup(retained_store_t *store);

/**
 * Store or update a retained message.
 * If payload_len == 0, clears any existing retained message for the topic (MQTT-3.3.1-11).
 */
bool retained_store_put(retained_store_t *store, const char *topic,
                        const uint8_t *payload, size_t payload_len, uint8_t qos);

/**
 * Find exact retained message for a topic
 */
const retained_msg_t *retained_store_get(retained_store_t *store, const char *topic);

#endif /* RETAINED_H */

