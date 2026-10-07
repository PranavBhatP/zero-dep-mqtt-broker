#ifndef MQTT_PROTOCOL_H
#define MQTT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file mqtt_protocol.h
 * @brief MQTT 3.1.1 OASIS Standard Protocol Constants and Bitwise Definitions
 * 
 * Course: IT365 Advanced Computer Networks
 * Project: Zero-Dependency MQTT Broker
 * Reference: OASIS MQTT Version 3.1.1 Standard (2014)
 */

/* MQTT Protocol Name & Version */
#define MQTT_PROTOCOL_NAME            "MQTT"
#define MQTT_PROTOCOL_NAME_LEN        4
#define MQTT_PROTOCOL_LEVEL_311       0x04

/* Default Configuration */
#define MQTT_DEFAULT_PORT             1883
#define MQTT_MAX_PACKET_SIZE          (256 * 1024)   /* 256 KB max packet size */
#define MQTT_MAX_BUFFER_SIZE          (512 * 1024)   /* 512 KB connection buffer */
#define MQTT_DEFAULT_BUFFER_SIZE      4096
#define MQTT_MAX_CLIENT_ID_LEN        256
#define MQTT_MAX_TOPIC_LEN            512
#define MQTT_MAX_SUB_TOPICS           32

/**
 * MQTT Control Packet Types (Bits 7-4 of Byte 1 in Fixed Header)
 */
typedef enum {
    MQTT_PKT_RESERVED    = 0,
    MQTT_PKT_CONNECT     = 1,   /* Client -> Server: Connection request */
    MQTT_PKT_CONNACK     = 2,   /* Server -> Client: Connection acknowledgment */
    MQTT_PKT_PUBLISH     = 3,   /* Both: Publish message */
    MQTT_PKT_PUBACK      = 4,   /* Both: Publish acknowledgment (QoS 1) */
    MQTT_PKT_PUBREC      = 5,   /* Both: Publish received (QoS 2 part 1) */
    MQTT_PKT_PUBREL      = 6,   /* Both: Publish release (QoS 2 part 2) */
    MQTT_PKT_PUBCOMP     = 7,   /* Both: Publish complete (QoS 2 part 3) */
    MQTT_PKT_SUBSCRIBE   = 8,   /* Client -> Server: Subscribe request */
    MQTT_PKT_SUBACK      = 9,   /* Server -> Client: Subscribe acknowledgment */
    MQTT_PKT_UNSUBSCRIBE = 10,  /* Client -> Server: Unsubscribe request */
    MQTT_PKT_UNSUBACK    = 11,  /* Server -> Client: Unsubscribe acknowledgment */
    MQTT_PKT_PINGREQ     = 12,  /* Client -> Server: PING request */
    MQTT_PKT_PINGRESP    = 13,  /* Server -> Client: PING response */
    MQTT_PKT_DISCONNECT  = 14,  /* Client -> Server: Disconnect notification */
    MQTT_PKT_RESERVED_15 = 15
} mqtt_packet_type_t;

/**
 * Expected Fixed Header Flags (Bits 3-0 of Byte 1)
 * According to OASIS MQTT 3.1.1 Table 2.2:
 * Most packets MUST have 0000 flags, except:
 * - PUBLISH: DUP (bit 3), QoS (bits 2-1), RETAIN (bit 0)
 * - PUBREL: 0010 (bit 1 is 1)
 * - SUBSCRIBE: 0010 (bit 1 is 1)
 * - UNSUBSCRIBE: 0010 (bit 1 is 1)
 */
#define MQTT_FLAGS_DEFAULT            0x00
#define MQTT_FLAGS_PUBREL_EXPECTED    0x02
#define MQTT_FLAGS_SUBSCRIBE_EXPECTED 0x02
#define MQTT_FLAGS_UNSUB_EXPECTED     0x02

/**
 * Quality of Service (QoS) Levels
 */
typedef enum {
    MQTT_QOS_0 = 0,             /* At most once delivery */
    MQTT_QOS_1 = 1,             /* At least once delivery */
    MQTT_QOS_2 = 2              /* Exactly once delivery */
} mqtt_qos_t;

/**
 * CONNACK Return Codes (Connect Acknowledgment)
 */
typedef enum {
    MQTT_CONNACK_ACCEPTED                      = 0x00, /* Connection Accepted */
    MQTT_CONNACK_REFUSED_PROTOCOL_VERSION      = 0x01, /* Connection Refused: Unacceptable Protocol Version */
    MQTT_CONNACK_REFUSED_IDENTIFIER_REJECTED   = 0x02, /* Connection Refused: Identifier Rejected */
    MQTT_CONNACK_REFUSED_SERVER_UNAVAILABLE    = 0x03, /* Connection Refused: Server Unavailable */
    MQTT_CONNACK_REFUSED_BAD_USERNAME_PASSWORD = 0x04, /* Connection Refused: Bad User Name or Password */
    MQTT_CONNACK_REFUSED_NOT_AUTHORIZED        = 0x05  /* Connection Refused: Not Authorized */
} mqtt_connack_rc_t;

/**
 * SUBACK Return Codes
 */
typedef enum {
    MQTT_SUBACK_SUCCESS_QOS0 = 0x00,
    MQTT_SUBACK_SUCCESS_QOS1 = 0x01,
    MQTT_SUBACK_SUCCESS_QOS2 = 0x02,
    MQTT_SUBACK_FAILURE      = 0x80
} mqtt_suback_rc_t;

/**
 * Bitwise Extraction Macros for Fixed Header Byte 1
 */
#define MQTT_EXTRACT_TYPE(b)      (((uint8_t)(b) >> 4) & 0x0F)
#define MQTT_EXTRACT_FLAGS(b)     ((uint8_t)(b) & 0x0F)
#define MQTT_EXTRACT_DUP(flags)   (((uint8_t)(flags) >> 3) & 0x01)
#define MQTT_EXTRACT_QOS(flags)   (((uint8_t)(flags) >> 1) & 0x03)
#define MQTT_EXTRACT_RETAIN(flags)((uint8_t)(flags) & 0x01)

/**
 * Parser Return Status Codes
 */
typedef enum {
    MQTT_SUCCESS                 = 0,
    MQTT_NEED_MORE_DATA          = 1,   /* TCP stream fragment, wait for more bytes */
    MQTT_ERR_MALFORMED           = -1,  /* Malformed header or remaining length */
    MQTT_ERR_INVALID_FLAGS       = -2,  /* Reserved flags violated spec */
    MQTT_ERR_INVALID_PROTOCOL    = -3,  /* Protocol name is not "MQTT" or level != 4 */
    MQTT_ERR_INVALID_CLIENT_ID   = -4,  /* Client ID invalid / empty on clean_session=0 */
    MQTT_ERR_INVALID_TOPIC       = -5,  /* Invalid characters in topic name/filter */
    MQTT_ERR_PAYLOAD_TRUNCATED   = -6,  /* Remaining length shorter than expected payload */
    MQTT_ERR_BUFFER_OVERFLOW     = -7   /* Packet exceeds max buffer capacity */
} mqtt_status_t;

/**
 * Helper to get human-readable packet type name
 */
static inline const char *mqtt_packet_type_str(uint8_t type) {
    switch (type) {
        case MQTT_PKT_CONNECT:     return "CONNECT";
        case MQTT_PKT_CONNACK:     return "CONNACK";
        case MQTT_PKT_PUBLISH:     return "PUBLISH";
        case MQTT_PKT_PUBACK:      return "PUBACK";
        case MQTT_PKT_PUBREC:      return "PUBREC";
        case MQTT_PKT_PUBREL:      return "PUBREL";
        case MQTT_PKT_PUBCOMP:     return "PUBCOMP";
        case MQTT_PKT_SUBSCRIBE:   return "SUBSCRIBE";
        case MQTT_PKT_SUBACK:      return "SUBACK";
        case MQTT_PKT_UNSUBSCRIBE: return "UNSUBSCRIBE";
        case MQTT_PKT_UNSUBACK:    return "UNSUBACK";
        case MQTT_PKT_PINGREQ:     return "PINGREQ";
        case MQTT_PKT_PINGRESP:    return "PINGRESP";
        case MQTT_PKT_DISCONNECT:  return "DISCONNECT";
        default:                   return "UNKNOWN";
    }
}

#endif /* MQTT_PROTOCOL_H */
