#ifndef MQTT_PACKET_H
#define MQTT_PACKET_H

#include "mqtt_protocol.h"
#include <stdbool.h>

/**
 * @file mqtt_packet.h
 * @brief Structured representation of decoded MQTT 3.1.1 Control Packets
 */

/**
 * Decoded Fixed Header (Common to all MQTT packets)
 */
typedef struct {
    uint8_t  type;                    /* Packet type (1-14) */
    uint8_t  flags;                   /* Raw 4-bit flags */
    uint8_t  dup;                     /* PUBLISH DUP flag (0 or 1) */
    uint8_t  qos;                     /* PUBLISH QoS (0, 1, 2) */
    uint8_t  retain;                  /* PUBLISH RETAIN flag (0 or 1) */
    uint32_t remaining_length;        /* Decoded variable-byte length */
    size_t   remaining_length_bytes;  /* Number of bytes used to encode remaining_length (1 to 4) */
    size_t   header_size;             /* 1 + remaining_length_bytes */
    size_t   total_packet_size;       /* header_size + remaining_length */
} mqtt_fixed_header_t;

/**
 * CONNECT Packet (Type 1)
 */
typedef struct {
    char     protocol_name[16];       /* Must be "MQTT" */
    uint8_t  protocol_level;          /* Must be 0x04 for MQTT 3.1.1 */
    uint8_t  clean_session;           /* Bit 1 of Connect Flags */
    uint8_t  will_flag;               /* Bit 2 of Connect Flags */
    uint8_t  will_qos;                /* Bits 4-3 of Connect Flags */
    uint8_t  will_retain;             /* Bit 5 of Connect Flags */
    uint8_t  password_flag;           /* Bit 6 of Connect Flags */
    uint8_t  username_flag;           /* Bit 7 of Connect Flags */
    uint16_t keep_alive;              /* Keep Alive timer in seconds */
    
    char     client_id[MQTT_MAX_CLIENT_ID_LEN];
    char     will_topic[MQTT_MAX_TOPIC_LEN];
    uint8_t  will_message[1024];
    uint16_t will_message_len;
    char     username[256];
    char     password[256];
    uint16_t password_len;
} mqtt_connect_pkt_t;

/**
 * PUBLISH Packet (Type 3)
 */
typedef struct {
    uint8_t        dup;
    uint8_t        qos;
    uint8_t        retain;
    char           topic[MQTT_MAX_TOPIC_LEN];
    uint16_t       packet_id;         /* Present only if QoS > 0 */
    const uint8_t *payload;           /* Pointer into packet buffer */
    size_t         payload_len;       /* Payload length in bytes */
} mqtt_publish_pkt_t;

/**
 * Individual subscription entry in a SUBSCRIBE packet
 */
typedef struct {
    char    topic_filter[MQTT_MAX_TOPIC_LEN];
    uint8_t qos;
} mqtt_sub_topic_t;

/**
 * SUBSCRIBE Packet (Type 8)
 */
typedef struct {
    uint16_t         packet_id;
    size_t           topic_count;
    mqtt_sub_topic_t topics[MQTT_MAX_SUB_TOPICS];
} mqtt_subscribe_pkt_t;

/**
 * UNSUBSCRIBE Packet (Type 10)
 */
typedef struct {
    uint16_t packet_id;
    size_t   topic_count;
    char     topics[MQTT_MAX_SUB_TOPICS][MQTT_MAX_TOPIC_LEN];
} mqtt_unsubscribe_pkt_t;

/**
 * Acknowledgment Packets with only a 2-byte Packet Identifier:
 * PUBACK (Type 4), PUBREC (Type 5), PUBREL (Type 6), PUBCOMP (Type 7), UNSUBACK (Type 11)
 */
typedef struct {
    uint16_t packet_id;
} mqtt_ack_pkt_t;

/**
 * Universal Packet Container
 */
typedef struct {
    mqtt_fixed_header_t fixed_header;
    union {
        mqtt_connect_pkt_t     connect;
        mqtt_publish_pkt_t     publish;
        mqtt_subscribe_pkt_t   subscribe;
        mqtt_unsubscribe_pkt_t unsubscribe;
        mqtt_ack_pkt_t         ack;       /* PUBACK, PUBREC, PUBREL, PUBCOMP */
    };
} mqtt_packet_t;

#endif /* MQTT_PACKET_H */
