#ifndef THROTTLE_H
#define THROTTLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * @file throttle.h
 * @brief Token-Bucket Rate Limiter & Bandwidth Throttling for IoT traffic shaping
 * 
 * Course: IT365 Advanced Computer Networks
 * Project: Zero-Dependency MQTT Broker
 */

typedef struct {
    double   tokens;           /* Current available tokens */
    double   capacity;         /* Maximum burst capacity */
    double   refill_rate;      /* Tokens added per second */
    uint64_t last_update_ms;   /* Timestamp of last token update in milliseconds */
} token_bucket_t;

typedef struct {
    token_bucket_t  packet_bucket;     /* Packets per second limit */
    token_bucket_t  byte_bucket;       /* Bandwidth bytes per second limit */
    uint64_t        total_packets;
    uint64_t        total_bytes;
    uint64_t        dropped_packets;
    uint64_t        dropped_bytes;
} client_throttle_t;

typedef struct {
    bool              enabled;
    double            max_packets_per_sec;
    double            burst_packets;
    double            max_bytes_per_sec;
    double            burst_bytes;
    pthread_mutex_t   lock;
} global_throttle_config_t;

uint64_t throttle_get_time_ms(void);

void throttle_init_bucket(token_bucket_t *bucket, double rate_per_sec, double capacity);
void client_throttle_init(client_throttle_t *ct, double pkt_rate, double pkt_burst,
                          double byte_rate, double byte_burst);

/**
 * Check and consume tokens for a packet.
 * Returns true if allowed, false if throttled/dropped.
 */
bool client_throttle_consume(client_throttle_t *ct, size_t bytes);

#endif /* THROTTLE_H */
