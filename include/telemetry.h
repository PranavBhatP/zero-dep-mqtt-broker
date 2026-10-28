#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * @file telemetry.h
 * @brief Live network traffic metrics, throughput counters, and diagnostic statistics
 * 
 * Course: IT365 Advanced Computer Networks
 * Project: Zero-Dependency MQTT Broker
 */

typedef struct {
    uint64_t total_connections;
    uint64_t current_active_clients;
    uint64_t total_packets_rx;
    uint64_t total_packets_tx;
    uint64_t total_bytes_rx;
    uint64_t total_bytes_tx;
    uint64_t total_publishes;
    uint64_t total_subscribes;
    uint64_t total_unsubscribes;
    uint64_t total_throttled_packets;
    uint64_t total_lwt_published;
    uint64_t total_retained_messages;

    time_t   start_time;
    pthread_mutex_t lock;
} broker_telemetry_t;

bool telemetry_init(broker_telemetry_t *t);
void telemetry_cleanup(broker_telemetry_t *t);

void telemetry_dump_cli(broker_telemetry_t *t);
size_t telemetry_render_json(broker_telemetry_t *t, char *buf, size_t max_len);

#endif /* TELEMETRY_H */
