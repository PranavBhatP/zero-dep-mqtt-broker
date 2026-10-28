#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "server.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static mqtt_server_t g_server;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        log_info("Received termination signal (%d), stopping server gracefully...", sig);
        server_stop(&g_server);
    }
}

static void print_banner(const char *host, uint16_t port, bool crypto, bool throttle) {
    printf("\n");
    printf("\033[1;36m========================================================================\033[0m\n");
    printf("\033[1;32m      ZERO-DEPENDENCY MQTT 3.1.1 BROKER (FULL PRODUCTION ENGINE)        \033[0m\n");
    printf("\033[1;36m========================================================================\033[0m\n");
    printf(" \033[1mCourse:\033[0m        IT365 Advanced Computer Networks\n");
    printf(" \033[1mInstitute:\033[0m     National Institute of Technology Karnataka (NITK), Surathkal\n");
    printf(" \033[1mAuthors:\033[0m       Pranav Bhat (231IT049), Pranav Vinodh (231IT050), Rijul Raman (231IT056)\n");
    printf("------------------------------------------------------------------------\n");
    printf(" \033[1mArchitecture:\033[0m  Non-blocking Event-Driven Reactor (Linux epoll)\n");
    printf(" \033[1mRouting:\033[0m       Topic Radix Tree (Topic Trie) with O(K) Wildcards (+, #)\n");
    printf(" \033[1mSessions:\033[0m      Stateful Persistent Sessions & Offline QoS 1 Queues\n");
    printf(" \033[1mReliability:\033[0m   Keep-Alive Watchdog Timers & Last Will & Testament (LWT)\n");
    printf(" \033[1mSecurity:\033[0m      %s\n", crypto ? "\033[1;32mEnabled (ChaCha20 + HMAC-SHA256 authenticated encryption)\033[0m" : "Plaintext (Standard MQTT)");
    printf(" \033[1mTraffic Shaping:\033[0m %s\n", throttle ? "\033[1;32mEnabled (Token-Bucket rate limiter & bandwidth throttling)\033[0m" : "Unlimited");
    printf(" \033[1mListening on:\033[0m   %s:%u\n", host, port);
    printf("\033[1;36m========================================================================\033[0m\n\n");
    fflush(stdout);
}

static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Options:\n");
    printf("  -p, --port <port>             Port to bind broker (default: 1883)\n");
    printf("  -h, --host <host>             Host IP to bind broker (default: 0.0.0.0)\n");
    printf("  -d, --debug                   Enable verbose debug logging and packet hex dumps\n");
    printf("  -e, --encrypt                 Enable ChaCha20/HMAC payload encryption\n");
    printf("  -k, --key <string>            Encryption key string (up to 32 bytes)\n");
    printf("  -r, --rate-limit <pkts/sec>   Enable per-client token-bucket rate limiting\n");
    printf("  -b, --burst <packets>         Set token-bucket burst capacity (default: 10)\n");
    printf("  -m, --bandwidth <bytes/sec>   Set per-client bandwidth ceiling\n");
    printf("      --help                    Display this help message\n");
}

int main(int argc, char *argv[]) {
    uint16_t port = MQTT_DEFAULT_PORT;
    const char *host = "0.0.0.0";
    log_level_t log_level = LOG_LEVEL_INFO;
    bool enable_crypto = false;
    char crypto_key_str[64] = "acn-zero-dep-mqtt-secret-key-32b";
    bool enable_throttle = false;
    double max_pkts = 50.0;
    double burst_pkts = 10.0;
    double max_bytes = 1048576.0; /* 1 MB/s */
    double burst_bytes = 262144.0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) port = (uint16_t)atoi(argv[++i]);
            else { fprintf(stderr, "Option %s requires an argument.\n", argv[i]); return 1; }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) {
            if (i + 1 < argc) host = argv[++i];
            else { fprintf(stderr, "Option %s requires an argument.\n", argv[i]); return 1; }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            log_level = LOG_LEVEL_DEBUG;
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--encrypt") == 0) {
            enable_crypto = true;
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--key") == 0) {
            if (i + 1 < argc) {
                enable_crypto = true;
                snprintf(crypto_key_str, sizeof(crypto_key_str), "%s", argv[++i]);
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rate-limit") == 0) {
            if (i + 1 < argc) {
                enable_throttle = true;
                max_pkts = atof(argv[++i]);
            }
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--burst") == 0) {
            if (i + 1 < argc) burst_pkts = atof(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--bandwidth") == 0) {
            if (i + 1 < argc) {
                enable_throttle = true;
                max_bytes = atof(argv[++i]);
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    logger_set_level(log_level);
    print_banner(host, port, enable_crypto, enable_throttle);

    /* Setup signal handling for graceful shutdown */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Initialize server */
    if (!server_init(&g_server, host, port)) {
        log_error("Broker initialization failed on %s:%u", host, port);
        return 1;
    }

    /* Configure Cryptographic Engine */
    if (enable_crypto) {
        g_server.crypto_enabled = true;
        /* Derive 32-byte key via SHA-256 of key string */
        sha256((const uint8_t *)crypto_key_str, strlen(crypto_key_str), g_server.crypto_key);
        log_info("[SECURITY] Payload encryption enabled using ChaCha20 + HMAC-SHA256 authenticated cipher");
    }

    /* Configure Token-Bucket Rate Limiter */
    if (enable_throttle) {
        g_server.throttle_cfg.enabled = true;
        g_server.throttle_cfg.max_packets_per_sec = max_pkts;
        g_server.throttle_cfg.burst_packets = burst_pkts;
        g_server.throttle_cfg.max_bytes_per_sec = max_bytes;
        g_server.throttle_cfg.burst_bytes = burst_bytes;
        log_info("[THROTTLE] Global traffic shaping enabled: rate=%0.1f pkts/s (burst=%0.1f), bandwidth=%0.1f KB/s",
                 max_pkts, burst_pkts, max_bytes / 1024.0);
    }

    /* Run event loop */
    server_run(&g_server);

    /* Teardown and telemetry */
    telemetry_dump_cli(&g_server.telemetry);
    server_cleanup(&g_server);

    log_info("Broker stopped cleanly.");
    return 0;
}
