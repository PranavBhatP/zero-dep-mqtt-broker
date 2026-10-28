#include "telemetry.h"
#include <stdio.h>
#include <string.h>

bool telemetry_init(broker_telemetry_t *t) {
    if (!t) return false;
    memset(t, 0, sizeof(broker_telemetry_t));
    t->start_time = time(NULL);
    if (pthread_mutex_init(&t->lock, NULL) != 0) {
        return false;
    }
    return true;
}

void telemetry_cleanup(broker_telemetry_t *t) {
    if (!t) return;
    pthread_mutex_destroy(&t->lock);
}

void telemetry_dump_cli(broker_telemetry_t *t) {
    if (!t) return;
    pthread_mutex_lock(&t->lock);
    time_t uptime = time(NULL) - t->start_time;
    if (uptime == 0) uptime = 1;

    double rx_kb = (double)t->total_bytes_rx / 1024.0;
    double tx_kb = (double)t->total_bytes_tx / 1024.0;
    double msg_rate = (double)t->total_packets_rx / (double)uptime;

    printf("\n\033[36m\033[1m================ BROKER TELEMETRY & METRICS ================\033[0m\n");
    printf(" Uptime: %ld seconds | Active Clients: %lu | Total Accepted: %lu\n",
           (long)uptime, (unsigned long)t->current_active_clients, (unsigned long)t->total_connections);
    printf(" Messages: %lu In (%0.1f msg/s), %lu Out | Fan-outs (Publishes): %lu\n",
           (unsigned long)t->total_packets_rx, msg_rate,
           (unsigned long)t->total_packets_tx, (unsigned long)t->total_publishes);
    printf(" Subscriptions: %lu active/processed | Unsubscribes: %lu\n",
           (unsigned long)t->total_subscribes, (unsigned long)t->total_unsubscribes);
    printf(" Traffic: %0.2f KB received, %0.2f KB transmitted\n", rx_kb, tx_kb);
    printf(" Reliability: %lu LWT fired | %lu Retained msgs | %lu Throttled\n",
           (unsigned long)t->total_lwt_published, (unsigned long)t->total_retained_messages,
           (unsigned long)t->total_throttled_packets);
    printf("\033[36m\033[1m============================================================\033[0m\n\n");
    pthread_mutex_unlock(&t->lock);
}

size_t telemetry_render_json(broker_telemetry_t *t, char *buf, size_t max_len) {
    if (!t || !buf || max_len == 0) return 0;
    pthread_mutex_lock(&t->lock);
    time_t uptime = time(NULL) - t->start_time;

    int written = snprintf(buf, max_len,
        "{"
        "\"uptime_sec\":%ld,"
        "\"active_clients\":%lu,"
        "\"total_connections\":%lu,"
        "\"packets_rx\":%lu,"
        "\"packets_tx\":%lu,"
        "\"bytes_rx\":%lu,"
        "\"bytes_tx\":%lu,"
        "\"publishes\":%lu,"
        "\"subscribes\":%lu,"
        "\"throttled_packets\":%lu,"
        "\"lwt_published\":%lu"
        "}",
        (long)uptime,
        (unsigned long)t->current_active_clients,
        (unsigned long)t->total_connections,
        (unsigned long)t->total_packets_rx,
        (unsigned long)t->total_packets_tx,
        (unsigned long)t->total_bytes_rx,
        (unsigned long)t->total_bytes_tx,
        (unsigned long)t->total_publishes,
        (unsigned long)t->total_subscribes,
        (unsigned long)t->total_throttled_packets,
        (unsigned long)t->total_lwt_published);

    pthread_mutex_unlock(&t->lock);
    return (written > 0 && (size_t)written < max_len) ? (size_t)written : 0;
}

