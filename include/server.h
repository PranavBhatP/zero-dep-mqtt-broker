#ifndef SERVER_H
#define SERVER_H

#include "mqtt_protocol.h"
#include "mqtt_packet.h"
#include "buffer.h"
#include <netinet/in.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/**
 * @file server.h
 * @brief Non-blocking event-driven TCP server using Linux epoll reactor architecture
 * 
 * Course: IT365 Advanced Computer Networks
 * Project: Zero-Dependency MQTT Broker
 */

#define SERVER_MAX_CLIENTS      1024
#define SERVER_EPOLL_MAX_EVENTS 64
#define SERVER_READ_CHUNK_SIZE  4096

/**
 * Client connection state
 */
typedef enum {
    CONN_STATE_NEW,             /* TCP connected, awaiting CONNECT packet */
    CONN_STATE_CONNECTED,       /* CONNECT accepted, CONNACK sent */
    CONN_STATE_DISCONNECTING,   /* Marked for teardown */
    CONN_STATE_CLOSED           /* Closed and pending reclamation */
} conn_state_t;

/**
 * Per-client connection context
 */
typedef struct client_conn {
    int           fd;
    char          ip[INET_ADDRSTRLEN];
    uint16_t      port;
    conn_state_t  state;
    
    byte_buffer_t rx_buffer;    /* Incoming TCP stream defragmentation buffer */
    byte_buffer_t tx_buffer;    /* Outgoing TCP write queue */

    char          client_id[MQTT_MAX_CLIENT_ID_LEN];
    uint8_t       clean_session;
    uint16_t      keep_alive_sec;
    time_t        last_active_time;

    struct client_conn *prev;
    struct client_conn *next;
} client_conn_t;

/**
 * Broker server instance
 */
typedef struct {
    int               listen_fd;
    int               epoll_fd;
    char              host[64];
    uint16_t          port;
    volatile bool     running;

    client_conn_t    *clients[SERVER_MAX_CLIENTS];
    size_t            active_client_count;

    pthread_mutex_t   lock;     /* Thread-safe protection for client registry */

    /* Statistics */
    uint64_t          total_connections_accepted;
    uint64_t          total_packets_received;
    uint64_t          total_packets_sent;
    uint64_t          total_bytes_received;
    uint64_t          total_bytes_sent;
} mqtt_server_t;

/**
 * Initialize server structures and bind/listen to port
 */
bool server_init(mqtt_server_t *srv, const char *host, uint16_t port);

/**
 * Run the epoll event loop (blocks until srv->running is set to false)
 */
void server_run(mqtt_server_t *srv);

/**
 * Signal the server to stop gracefully
 */
void server_stop(mqtt_server_t *srv);

/**
 * Release all sockets, memory buffers, and thread locks
 */
void server_cleanup(mqtt_server_t *srv);

/**
 * Send raw binary packet to client (handles non-blocking write & queuing)
 */
bool server_send_bytes(mqtt_server_t *srv, client_conn_t *client, const uint8_t *data, size_t len);

#endif /* SERVER_H */
