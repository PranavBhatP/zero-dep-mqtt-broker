#define _GNU_SOURCE
#include "throttle.h"
#include <time.h>
#include <string.h>

uint64_t throttle_get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

void throttle_init_bucket(token_bucket_t *bucket, double rate_per_sec, double capacity) {
    if (!bucket) return;
    bucket->refill_rate = rate_per_sec;
    bucket->capacity = capacity;
    bucket->tokens = capacity; /* Start full */
    bucket->last_update_ms = throttle_get_time_ms();
}

void client_throttle_init(client_throttle_t *ct, double pkt_rate, double pkt_burst,
                          double byte_rate, double byte_burst) {
    if (!ct) return;
    memset(ct, 0, sizeof(client_throttle_t));
    throttle_init_bucket(&ct->packet_bucket, pkt_rate, pkt_burst);
    throttle_init_bucket(&ct->byte_bucket, byte_rate, byte_burst);
}

static void refill_bucket(token_bucket_t *bucket, uint64_t now_ms) {
    if (bucket->refill_rate <= 0) {
        bucket->tokens = 1e9; /* Unlimited */
        return;
    }
    uint64_t elapsed_ms = now_ms - bucket->last_update_ms;
    if (elapsed_ms > 0) {
        double added = (elapsed_ms / 1000.0) * bucket->refill_rate;
        bucket->tokens += added;
        if (bucket->tokens > bucket->capacity) {
            bucket->tokens = bucket->capacity;
        }
        bucket->last_update_ms = now_ms;
    }
}

bool client_throttle_consume(client_throttle_t *ct, size_t bytes) {
    if (!ct) return true;
    uint64_t now = throttle_get_time_ms();

    refill_bucket(&ct->packet_bucket, now);
    refill_bucket(&ct->byte_bucket, now);

    /* Check packet limit */
    if (ct->packet_bucket.refill_rate > 0 && ct->packet_bucket.tokens < 1.0) {
        ct->dropped_packets++;
        ct->dropped_bytes += bytes;
        return false; /* Rate limited */
    }

    /* Check byte limit */
    if (ct->byte_bucket.refill_rate > 0 && ct->byte_bucket.tokens < (double)bytes) {
        ct->dropped_packets++;
        ct->dropped_bytes += bytes;
        return false; /* Bandwidth throttled */
    }

    /* Consume tokens */
    if (ct->packet_bucket.refill_rate > 0) {
        ct->packet_bucket.tokens -= 1.0;
    }
    if (ct->byte_bucket.refill_rate > 0) {
        ct->byte_bucket.tokens -= (double)bytes;
    }

    ct->total_packets++;
    ct->total_bytes += bytes;
    return true;
}

