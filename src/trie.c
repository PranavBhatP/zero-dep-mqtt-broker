#define _GNU_SOURCE
#include "trie.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

trie_node_t *trie_node_create(const char *level) {
    trie_node_t *node = (trie_node_t *)calloc(1, sizeof(trie_node_t));
    if (!node) return NULL;
    if (level) {
        node->topic_level = strdup(level);
        if (!node->topic_level) {
            free(node);
            return NULL;
        }
    }
    return node;
}

static void free_subscribers(trie_subscriber_t *sub) {
    while (sub) {
        trie_subscriber_t *next = sub->next;
        free(sub);
        sub = next;
    }
}

void trie_node_free(trie_node_t *node) {
    if (!node) return;

    /* Free exact matching children */
    trie_node_t *curr = node->children;
    while (curr) {
        trie_node_t *next = curr->next_sibling;
        trie_node_free(curr);
        curr = next;
    }

    /* Free wildcard child shortcuts if they weren't in children list */
    if (node->wildcard_plus) {
        trie_node_free(node->wildcard_plus);
        node->wildcard_plus = NULL;
    }
    if (node->wildcard_hash) {
        trie_node_free(node->wildcard_hash);
        node->wildcard_hash = NULL;
    }

    free_subscribers(node->subscribers);
    if (node->topic_level) {
        free(node->topic_level);
        node->topic_level = NULL;
    }
    free(node);
}

/* Helper to tokenize a topic into levels without dynamic allocation overhead */
static size_t tokenize_topic(const char *topic, char levels[TRIE_MAX_LEVELS][MQTT_MAX_TOPIC_LEN]) {
    if (!topic || !*topic) return 0;
    size_t count = 0;
    const char *start = topic;
    const char *p = topic;

    while (*p && count < TRIE_MAX_LEVELS) {
        if (*p == '/') {
            size_t len = p - start;
            if (len >= MQTT_MAX_TOPIC_LEN) len = MQTT_MAX_TOPIC_LEN - 1;
            memcpy(levels[count], start, len);
            levels[count][len] = '\0';
            count++;
            start = p + 1;
        }
        p++;
    }

    if (count < TRIE_MAX_LEVELS) {
        size_t len = p - start;
        if (len >= MQTT_MAX_TOPIC_LEN) len = MQTT_MAX_TOPIC_LEN - 1;
        memcpy(levels[count], start, len);
        levels[count][len] = '\0';
        count++;
    }

    return count;
}

static trie_node_t *find_or_create_child(trie_node_t *parent, const char *level) {
    if (strcmp(level, "+") == 0) {
        if (!parent->wildcard_plus) {
            parent->wildcard_plus = trie_node_create("+");
        }
        return parent->wildcard_plus;
    }

    if (strcmp(level, "#") == 0) {
        if (!parent->wildcard_hash) {
            parent->wildcard_hash = trie_node_create("#");
        }
        return parent->wildcard_hash;
    }

    /* Exact string match */
    trie_node_t *curr = parent->children;
    while (curr) {
        if (curr->topic_level && strcmp(curr->topic_level, level) == 0) {
            return curr;
        }
        curr = curr->next_sibling;
    }

    /* Not found: create and insert at head of children */
    trie_node_t *new_node = trie_node_create(level);
    if (!new_node) return NULL;
    new_node->next_sibling = parent->children;
    parent->children = new_node;
    return new_node;
}

bool trie_subscribe(trie_node_t *root, const char *topic_filter,
                    const char *client_id, int socket_fd, uint8_t qos) {
    if (!root || !topic_filter || !client_id) return false;

    char levels[TRIE_MAX_LEVELS][MQTT_MAX_TOPIC_LEN];
    size_t num_levels = tokenize_topic(topic_filter, levels);
    if (num_levels == 0) return false;

    trie_node_t *curr = root;
    for (size_t i = 0; i < num_levels; i++) {
        /* Check '#' is only allowed as the last token */
        if (strcmp(levels[i], "#") == 0 && i != num_levels - 1) {
            log_warn("Trie: '#' wildcard must be the last topic level");
            return false;
        }
        curr = find_or_create_child(curr, levels[i]);
        if (!curr) return false;
    }

    /* Check if client is already subscribed at this node; if so, update QoS and socket */
    trie_subscriber_t *sub = curr->subscribers;
    while (sub) {
        if (strcmp(sub->client_id, client_id) == 0) {
            sub->granted_qos = qos;
            sub->socket_fd = socket_fd;
            return true;
        }
        sub = sub->next;
    }

    /* Add new subscriber */
    trie_subscriber_t *new_sub = (trie_subscriber_t *)calloc(1, sizeof(trie_subscriber_t));
    if (!new_sub) return false;
    snprintf(new_sub->client_id, sizeof(new_sub->client_id), "%s", client_id);
    new_sub->socket_fd = socket_fd;
    new_sub->granted_qos = qos;
    new_sub->next = curr->subscribers;
    curr->subscribers = new_sub;

    return true;
}

bool trie_unsubscribe(trie_node_t *root, const char *topic_filter, const char *client_id) {
    if (!root || !topic_filter || !client_id) return false;

    char levels[TRIE_MAX_LEVELS][MQTT_MAX_TOPIC_LEN];
    size_t num_levels = tokenize_topic(topic_filter, levels);
    if (num_levels == 0) return false;

    trie_node_t *curr = root;
    for (size_t i = 0; i < num_levels; i++) {
        if (strcmp(levels[i], "+") == 0) {
            curr = curr->wildcard_plus;
        } else if (strcmp(levels[i], "#") == 0) {
            curr = curr->wildcard_hash;
        } else {
            trie_node_t *c = curr->children;
            while (c) {
                if (c->topic_level && strcmp(c->topic_level, levels[i]) == 0) {
                    break;
                }
                c = c->next_sibling;
            }
            curr = c;
        }
        if (!curr) return false; /* Topic path not found */
    }

    /* Remove client from subscribers list at this node */
    trie_subscriber_t *sub = curr->subscribers;
    trie_subscriber_t *prev = NULL;
    while (sub) {
        if (strcmp(sub->client_id, client_id) == 0) {
            if (prev) {
                prev->next = sub->next;
            } else {
                curr->subscribers = sub->next;
            }
            free(sub);
            return true;
        }
        prev = sub;
        sub = sub->next;
    }

    return false;
}

static size_t recursive_unsubscribe_client(trie_node_t *node, const char *client_id) {
    if (!node) return 0;
    size_t removed = 0;

    /* Check subscribers at this node */
    trie_subscriber_t *sub = node->subscribers;
    trie_subscriber_t *prev = NULL;
    while (sub) {
        if (strcmp(sub->client_id, client_id) == 0) {
            trie_subscriber_t *to_free = sub;
            if (prev) {
                prev->next = sub->next;
            } else {
                node->subscribers = sub->next;
            }
            sub = sub->next;
            free(to_free);
            removed++;
        } else {
            prev = sub;
            sub = sub->next;
        }
    }

    /* Recurse on children */
    trie_node_t *child = node->children;
    while (child) {
        removed += recursive_unsubscribe_client(child, client_id);
        child = child->next_sibling;
    }

    if (node->wildcard_plus) {
        removed += recursive_unsubscribe_client(node->wildcard_plus, client_id);
    }
    if (node->wildcard_hash) {
        removed += recursive_unsubscribe_client(node->wildcard_hash, client_id);
    }

    return removed;
}

size_t trie_unsubscribe_client(trie_node_t *root, const char *client_id) {
    if (!root || !client_id) return 0;
    return recursive_unsubscribe_client(root, client_id);
}

static void recursive_update_client_socket(trie_node_t *node, const char *client_id, int new_fd) {
    if (!node) return;

    trie_subscriber_t *sub = node->subscribers;
    while (sub) {
        if (strcmp(sub->client_id, client_id) == 0) {
            sub->socket_fd = new_fd;
        }
        sub = sub->next;
    }

    trie_node_t *child = node->children;
    while (child) {
        recursive_update_client_socket(child, client_id, new_fd);
        child = child->next_sibling;
    }

    if (node->wildcard_plus) {
        recursive_update_client_socket(node->wildcard_plus, client_id, new_fd);
    }
    if (node->wildcard_hash) {
        recursive_update_client_socket(node->wildcard_hash, client_id, new_fd);
    }
}

void trie_update_client_socket(trie_node_t *root, const char *client_id, int new_fd) {
    if (!root || !client_id) return;
    recursive_update_client_socket(root, client_id, new_fd);
}

/* Deduplication helper: adds subscriber to match_result_t, keeping the maximum granted QoS */
static void add_matched_subscriber(match_result_t *res, const trie_subscriber_t *sub) {
    if (!res || !sub || res->count >= TRIE_MAX_MATCHES) return;

    /* Check if already present */
    for (size_t i = 0; i < res->count; i++) {
        if (strcmp(res->subscribers[i].client_id, sub->client_id) == 0) {
            /* Already matched: per MQTT-3.3.5-1, use maximum granted QoS */
            if (sub->granted_qos > res->subscribers[i].qos) {
                res->subscribers[i].qos = sub->granted_qos;
            }
            if (sub->socket_fd >= 0) {
                res->subscribers[i].socket_fd = sub->socket_fd;
            }
            return;
        }
    }

    /* Add new entry */
    snprintf(res->subscribers[res->count].client_id, sizeof(res->subscribers[res->count].client_id), "%s", sub->client_id);
    res->subscribers[res->count].socket_fd = sub->socket_fd;
    res->subscribers[res->count].qos = sub->granted_qos;
    res->count++;
}

static void collect_node_subscribers(trie_node_t *node, match_result_t *res) {
    if (!node) return;
    trie_subscriber_t *sub = node->subscribers;
    while (sub) {
        add_matched_subscriber(res, sub);
        sub = sub->next;
    }
}

/* Recursive Trie matching */
static void match_recursive(trie_node_t *node, char levels[TRIE_MAX_LEVELS][MQTT_MAX_TOPIC_LEN],
                            size_t level_idx, size_t total_levels, match_result_t *res) {
    if (!node) return;

    /* 1. Multi-level wildcard '#' matches this level and all subsequent subordinate levels */
    if (node->wildcard_hash) {
        collect_node_subscribers(node->wildcard_hash, res);
    }

    /* If we have processed all levels of the published topic */
    if (level_idx == total_levels) {
        collect_node_subscribers(node, res);
        return;
    }

    const char *curr_level = levels[level_idx];

    /* 2. Exact match child */
    trie_node_t *child = node->children;
    while (child) {
        if (child->topic_level && strcmp(child->topic_level, curr_level) == 0) {
            match_recursive(child, levels, level_idx + 1, total_levels, res);
            break;
        }
        child = child->next_sibling;
    }

    /* 3. Single-level wildcard '+' child */
    if (node->wildcard_plus) {
        match_recursive(node->wildcard_plus, levels, level_idx + 1, total_levels, res);
    }
}

size_t trie_match_subscribers(trie_node_t *root, const char *publish_topic, match_result_t *out_result) {
    if (!root || !publish_topic || !out_result) return 0;

    memset(out_result, 0, sizeof(match_result_t));

    char levels[TRIE_MAX_LEVELS][MQTT_MAX_TOPIC_LEN];
    size_t num_levels = tokenize_topic(publish_topic, levels);
    if (num_levels == 0) return 0;

    match_recursive(root, levels, 0, num_levels, out_result);
    return out_result->count;
}

