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

static void print_banner(const char *host, uint16_t port) {
    printf("\n");
    printf("\033[1;36m========================================================================\033[0m\n");
    printf("\033[1;32m         ZERO-DEPENDENCY MQTT 3.1.1 BROKER (PHASE 1 FOUNDATION)         \033[0m\n");
    printf("\033[1;36m========================================================================\033[0m\n");
    printf(" \033[1mCourse:\033[0m    IT365 Advanced Computer Networks\n");
    printf(" \033[1mInstitute:\033[0m National Institute of Technology Karnataka (NITK), Surathkal\n");
    printf(" \033[1mTeam:\033[0m     Pranav Bhat (231IT049)\n");
    printf("           Pranav Vinodh (231IT050)\n");
    printf("           Rijul Raman (231IT056)\n");
    printf("------------------------------------------------------------------------\n");
    printf(" \033[1mArchitecture:\033[0m Non-blocking Event-Driven TCP Server (Linux epoll)\n");
    printf(" \033[1mParser:\033[0m       Strict Bitwise OASIS MQTT 3.1.1 Packet Decoder\n");
    printf(" \033[1mListening on:\033[0m %s:%u\n", host, port);
    printf("\033[1;36m========================================================================\033[0m\n\n");
    fflush(stdout);
}

static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Options:\n");
    printf("  -p, --port <port>       Port to bind broker (default: 1883)\n");
    printf("  -h, --host <host>       Host IP to bind broker (default: 0.0.0.0)\n");
    printf("  -d, --debug             Enable verbose debug logging and packet hex dumps\n");
    printf("      --help              Display this help message\n");
}

int main(int argc, char *argv[]) {
    uint16_t port = MQTT_DEFAULT_PORT;
    const char *host = "0.0.0.0";
    log_level_t log_level = LOG_LEVEL_INFO;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                port = (uint16_t)atoi(argv[++i]);
            } else {
                fprintf(stderr, "Option %s requires an argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) {
            if (i + 1 < argc) {
                host = argv[++i];
            } else {
                fprintf(stderr, "Option %s requires an argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            log_level = LOG_LEVEL_DEBUG;
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
    print_banner(host, port);

    /* Setup signal handling for graceful shutdown */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Initialize and start non-blocking server */
    if (!server_init(&g_server, host, port)) {
        log_error("Broker initialization failed on %s:%u", host, port);
        return 1;
    }

    server_run(&g_server);

    /* Shutdown and resource reclamation */
    server_cleanup(&g_server);

    log_info("Broker stopped. Statistics:");
    log_info("  Total connections accepted: %lu", g_server.total_connections_accepted);
    log_info("  Total packets received:     %lu", g_server.total_packets_received);
    log_info("  Total packets sent:         %lu", g_server.total_packets_sent);
    log_info("  Total bytes received:       %lu", g_server.total_bytes_received);
    log_info("  Total bytes sent:           %lu", g_server.total_bytes_sent);

    return 0;
}
