#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "throttle.h"
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

static void test_token_bucket(void) {
    printf("[TEST] Testing Token Bucket Rate Limiting...\n");
    client_throttle_t ct;
    /* Limit: 10 packets/sec, burst capacity of 3 */
    client_throttle_init(&ct, 10.0, 3.0, 0, 0);

    /* First 3 should succeed immediately (consuming burst) */
    assert(client_throttle_consume(&ct, 50));
    assert(client_throttle_consume(&ct, 50));
    assert(client_throttle_consume(&ct, 50));

    /* 4th packet in same instant should fail (rate limited) */
    assert(!client_throttle_consume(&ct, 50));
    assert(ct.dropped_packets == 1);

    /* Sleep 120ms to replenish at least 1 token (10/sec = 1 per 100ms) */
    usleep(120000);

    /* Now next packet should succeed */
    assert(client_throttle_consume(&ct, 50));
    printf("  -> PASS: Token bucket burst & refill limiting\n");
}

int main(void) {
    printf("========================================\n");
    printf("RUNNING THROTTLE & RATE LIMITER TESTS\n");
    printf("========================================\n");

    test_token_bucket();

    printf("ALL THROTTLE TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
