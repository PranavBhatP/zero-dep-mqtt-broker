#include "trie.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void test_exact_subscription(void) {
    printf("[TEST] Testing exact topic matching...\n");
    trie_node_t *root = trie_node_create("");
    assert(root != NULL);

    bool ok = trie_subscribe(root, "home/livingroom/temperature", "sensor_1", 10, 0);
    assert(ok);

    match_result_t res;
    size_t count = trie_match_subscribers(root, "home/livingroom/temperature", &res);
    assert(count == 1);
    assert(strcmp(res.subscribers[0].client_id, "sensor_1") == 0);
    assert(res.subscribers[0].socket_fd == 10);
    assert(res.subscribers[0].qos == 0);

    /* Mismatched topic should yield 0 */
    count = trie_match_subscribers(root, "home/livingroom/humidity", &res);
    assert(count == 0);

    trie_node_free(root);
    printf("  -> PASS: Exact matching\n");
}

static void test_wildcards(void) {
    printf("[TEST] Testing single (+) and multi-level (#) wildcards...\n");
    trie_node_t *root = trie_node_create("");

    /* Subscribe client_plus to home/+/temp */
    assert(trie_subscribe(root, "home/+/temp", "client_plus", 11, 1));
    /* Subscribe client_hash to home/# */
    assert(trie_subscribe(root, "home/#", "client_hash", 12, 1));
    /* Subscribe client_exact to home/kitchen/temp */
    assert(trie_subscribe(root, "home/kitchen/temp", "client_exact", 13, 0));

    match_result_t res;
    /* Publish to home/kitchen/temp: should match client_exact, client_plus, and client_hash */
    size_t count = trie_match_subscribers(root, "home/kitchen/temp", &res);
    assert(count == 3);

    /* Publish to home/bedroom/temp: should match client_plus and client_hash */
    count = trie_match_subscribers(root, "home/bedroom/temp", &res);
    assert(count == 2);

    /* Publish to home/garden/outdoor/lux: should match client_hash only */
    count = trie_match_subscribers(root, "home/garden/outdoor/lux", &res);
    assert(count == 1);
    assert(strcmp(res.subscribers[0].client_id, "client_hash") == 0);

    trie_node_free(root);
    printf("  -> PASS: Wildcard matching\n");
}

static void test_deduplication(void) {
    printf("[TEST] Testing subscriber deduplication (MQTT-3.3.5-1)...\n");
    trie_node_t *root = trie_node_create("");

    /* Same client subscribes to overlapping filters with different QoS */
    assert(trie_subscribe(root, "sensor/+/data", "client_A", 20, 0));
    assert(trie_subscribe(root, "sensor/#", "client_A", 20, 1));

    match_result_t res;
    size_t count = trie_match_subscribers(root, "sensor/temp/data", &res);
    /* Should appear only ONCE with max QoS = 1 */
    assert(count == 1);
    assert(strcmp(res.subscribers[0].client_id, "client_A") == 0);
    assert(res.subscribers[0].qos == 1);

    trie_node_free(root);
    printf("  -> PASS: Deduplication\n");
}

static void test_unsubscription(void) {
    printf("[TEST] Testing unsubscription and client cleanup...\n");
    trie_node_t *root = trie_node_create("");

    assert(trie_subscribe(root, "a/b/c", "c1", 31, 0));
    assert(trie_subscribe(root, "a/+/c", "c1", 31, 0));
    assert(trie_subscribe(root, "a/b/c", "c2", 32, 0));

    /* Unsubscribe c1 from a/b/c only */
    assert(trie_unsubscribe(root, "a/b/c", "c1"));

    match_result_t res;
    size_t count = trie_match_subscribers(root, "a/b/c", &res);
    assert(count == 2); /* c1 matches via a/+/c, c2 matches via a/b/c */

    /* Unsubscribe entire client c1 */
    size_t removed = trie_unsubscribe_client(root, "c1");
    assert(removed == 1);

    count = trie_match_subscribers(root, "a/b/c", &res);
    assert(count == 1);
    assert(strcmp(res.subscribers[0].client_id, "c2") == 0);

    trie_node_free(root);
    printf("  -> PASS: Unsubscription & client cleanup\n");
}

int main(void) {
    printf("========================================\n");
    printf("RUNNING TOPIC RADIX TREE (TRIE) TESTS\n");
    printf("========================================\n");

    test_exact_subscription();
    test_wildcards();
    test_deduplication();
    test_unsubscription();

    printf("ALL TRIE TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}

