#ifndef SESSION_H
#define SESSION_H

#include "mqtt_protocol.h"
#include "buffer.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * @file session.h
 * @brief Session state machine, persistence store, and offline message queue
 * 
 * Course: IT365 Advanced Computer Networks
 * Project: Zero-Dependency MQTT Broker
 */

#define SESSION_MAX_SUBSCRIPTIONS 64
#define SESSION_MAX_OFFLINE_QUEUE 128
#define SESSION_HASH_SIZE         1024

/**
 * Queued offline message for persistent sessions
 */
typedef struct {
    char     topic[MQTT_MAX_TOPIC_LEN];
    uint8_t *payload;
    size_t   payload_len;
    uint8_t  qos;
    uint8_t  retain;
    uint16_t packet_id;
} queued_message_t;

/**
 * Subscription record in persistent session
 */
typedef struct {
    char    topic_filter[MQTT_MAX_TOPIC_LEN];
    uint8_t qos;
} session_sub_t;

/**
 * Will message record
 */
typedef struct {
    bool     present;
    char     topic[MQTT_MAX_TOPIC_LEN];
    uint8_t *payload;
    size_t   payload_len;
    uint8_t  qos;
    uint8_t  retain;
} will_message_t;

/**
 * Client Session Context
 */
typedef struct mqtt_session {
    char              client_id[MQTT_MAX_CLIENT_ID_LEN];
    int               socket_fd;          /* -1 if offline */
    bool              is_connected;
    uint8_t           clean_session;
    uint16_t          keep_alive_sec;
    time_t            last_active_time;
    time_t            created_time;

    /* Will message */
    will_message_t    will;

    /* Subscriptions list */
    session_sub_t     subscriptions[SESSION_MAX_SUBSCRIPTIONS];
    size_t            sub_count;

    /* Offline QoS 1 queue (for clean_session == 0) */
    queued_message_t  offline_queue[SESSION_MAX_OFFLINE_QUEUE];
    size_t            queue_head;
    size_t            queue_tail;
    size_t            queue_count;

    /* Next session in hash bucket */
    struct mqtt_session *next;
} mqtt_session_t;

/**
 * Thread-safe Global Session Registry
 */
typedef struct {
    mqtt_session_t  *buckets[SESSION_HASH_SIZE];
    size_t           total_sessions;
    size_t           active_connected_sessions;
    pthread_mutex_t  lock;
} session_registry_t;

/**
 * Initialize global session registry
 */
bool session_registry_init(session_registry_t *reg);

/**
 * Destroy global session registry and free all sessions
 */
void session_registry_cleanup(session_registry_t *reg);

/**
 * Lookup existing session by client_id
 */
mqtt_session_t *session_find(session_registry_t *reg, const char *client_id);

/**
 * Create or resume session upon receiving CONNECT packet
 * 
 * @param reg Session registry
 * @param client_id Client Identifier
 * @param socket_fd Connected socket
 * @param clean_session Clean session flag
 * @param keep_alive Keep alive in seconds
 * @param out_session_present Pointer to store 1 if resuming existing session, 0 if new
 * @return Pointer to session context or NULL on error
 */
mqtt_session_t *session_connect(session_registry_t *reg, const char *client_id,
                                int socket_fd, uint8_t clean_session,
                                uint16_t keep_alive, uint8_t *out_session_present);

/**
 * Mark a session as disconnected. Cleans up session if clean_session was true.
 */
void session_disconnect(session_registry_t *reg, const char *client_id, bool graceful);

/**
 * Add subscription to session
 */
bool session_add_subscription(mqtt_session_t *session, const char *topic_filter, uint8_t qos);

/**
 * Remove subscription from session
 */
bool session_remove_subscription(mqtt_session_t *session, const char *topic_filter);

/**
 * Set Will message for session
 */
void session_set_will(mqtt_session_t *session, const char *topic,
                      const uint8_t *payload, size_t payload_len,
                      uint8_t qos, uint8_t retain);

/**
 * Clear Will message (called on clean DISCONNECT)
 */
void session_clear_will(mqtt_session_t *session);

/**
 * Queue a message for an offline persistent session
 */
bool session_queue_offline_message(mqtt_session_t *session, const char *topic,
                                  const uint8_t *payload, size_t payload_len,
                                  uint8_t qos, uint8_t retain, uint16_t packet_id);

#endif /* SESSION_H */
