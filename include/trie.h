#ifndef TRIE_H
#define TRIE_H

#include "mqtt_protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file trie.h
 * @brief Topic Radix Tree (Topic Trie) for O(K) topic resolution and wildcard matching (+, #)
 * 
 * Course: IT365 Advanced Computer Networks
 * Project: Zero-Dependency MQTT Broker
 */

#define TRIE_MAX_LEVELS 32
#define TRIE_MAX_MATCHES 256

/**
 * Subscriber node attached to a trie node
 */
typedef struct trie_subscriber {
    char                    client_id[MQTT_MAX_CLIENT_ID_LEN];
    int                     socket_fd;
    uint8_t                 granted_qos;
    struct trie_subscriber *next;
} trie_subscriber_t;

/**
 * Trie node representing one segment in a topic path (e.g., "home", "sensor", "+", "#")
 */
typedef struct trie_node {
    char               *topic_level;        /* Segment string or "+" or "#" */
    trie_subscriber_t  *subscribers;        /* Linked list of subscribers at this node */

    struct trie_node   *children;           /* Linked list of exact matching child segments */
    struct trie_node   *next_sibling;       /* Next sibling in linked list */

    struct trie_node   *wildcard_plus;      /* Direct shortcut to '+' child if present */
    struct trie_node   *wildcard_hash;      /* Direct shortcut to '#' child if present */
} trie_node_t;

/**
 * Matched subscriber result
 */
typedef struct {
    char    client_id[MQTT_MAX_CLIENT_ID_LEN];
    int     socket_fd;
    uint8_t qos;
} matched_subscriber_t;

/**
 * Container for matched subscribers
 */
typedef struct {
    matched_subscriber_t subscribers[TRIE_MAX_MATCHES];
    size_t               count;
} match_result_t;

/**
 * Create a new trie node
 */
trie_node_t *trie_node_create(const char *level);

/**
 * Free a trie node and all its children recursively
 */
void trie_node_free(trie_node_t *node);

/**
 * Insert a topic subscription into the Trie
 * 
 * @param root Trie root node
 * @param topic_filter Topic filter string (e.g., "sport/tennis/+", "sensor/#")
 * @param client_id Client Identifier
 * @param socket_fd Socket file descriptor of the subscriber
 * @param qos Maximum granted QoS (0, 1, 2)
 * @return true on success, false on error
 */
bool trie_subscribe(trie_node_t *root, const char *topic_filter,
                    const char *client_id, int socket_fd, uint8_t qos);

/**
 * Remove a single topic subscription for a client
 * 
 * @param root Trie root node
 * @param topic_filter Topic filter to unsubscribe from
 * @param client_id Client Identifier
 * @return true if subscription was found and removed, false otherwise
 */
bool trie_unsubscribe(trie_node_t *root, const char *topic_filter, const char *client_id);

/**
 * Remove all subscriptions across the entire Trie for a given client (used on disconnect)
 * 
 * @param root Trie root node
 * @param client_id Client Identifier
 * @return Number of subscriptions removed
 */
size_t trie_unsubscribe_client(trie_node_t *root, const char *client_id);

/**
 * Update socket descriptor for a client in all active subscriptions (used on session resumption)
 */
void trie_update_client_socket(trie_node_t *root, const char *client_id, int new_fd);

/**
 * Match subscribers for a published topic (O(K) search with wildcard resolution)
 * Automatically deduplicates subscribers per MQTT-3.3.5-1.
 * 
 * @param root Trie root node
 * @param publish_topic Published topic name (must NOT contain wildcards)
 * @param out_result Output container with deduplicated matched subscribers
 * @return Number of unique matched subscribers
 */
size_t trie_match_subscribers(trie_node_t *root, const char *publish_topic, match_result_t *out_result);

#endif /* TRIE_H */

