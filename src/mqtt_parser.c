#include "mqtt_parser.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>

int mqtt_decode_remaining_length(const uint8_t *buf, size_t len, uint32_t *out_val, size_t *out_bytes) {
    if (!buf || !out_val || !out_bytes) return MQTT_ERR_MALFORMED;

    uint32_t multiplier = 1;
    uint32_t value = 0;
    size_t count = 0;
    uint8_t encoded_byte;

    do {
        if (count >= len) {
            return MQTT_NEED_MORE_DATA;
        }

        if (count >= 4) {
            /* Remaining Length is limited to maximum 4 bytes in MQTT 3.1.1 Section 2.2.3 */
            return MQTT_ERR_MALFORMED;
        }

        encoded_byte = buf[count++];
        value += (encoded_byte & 0x7F) * multiplier;

        if (multiplier > 128 * 128 * 128) {
            return MQTT_ERR_MALFORMED;
        }
        multiplier *= 128;
    } while ((encoded_byte & 0x80) != 0);

    *out_val = value;
    *out_bytes = count;
    return MQTT_SUCCESS;
}

size_t mqtt_encode_remaining_length(uint8_t *dest, uint32_t length) {
    if (!dest) return 0;

    size_t bytes = 0;
    do {
        uint8_t byte = length % 128;
        length /= 128;
        if (length > 0) {
            byte |= 0x80;
        }
        dest[bytes++] = byte;
    } while (length > 0);

    return bytes;
}

int mqtt_decode_fixed_header(const uint8_t *buf, size_t len, mqtt_fixed_header_t *out_header) {
    if (!buf || !out_header) return MQTT_ERR_MALFORMED;
    if (len < 2) return MQTT_NEED_MORE_DATA;

    memset(out_header, 0, sizeof(mqtt_fixed_header_t));

    uint8_t byte0 = buf[0];
    uint8_t type = MQTT_EXTRACT_TYPE(byte0);
    uint8_t flags = MQTT_EXTRACT_FLAGS(byte0);

    /* Validate packet type range */
    if (type < MQTT_PKT_CONNECT || type > MQTT_PKT_DISCONNECT) {
        log_warn("Invalid packet type received: %u", type);
        return MQTT_ERR_MALFORMED;
    }

    /* Validate fixed header flags strictly according to Table 2.2 of OASIS MQTT 3.1.1 */
    switch (type) {
        case MQTT_PKT_CONNECT:
        case MQTT_PKT_CONNACK:
        case MQTT_PKT_PUBACK:
        case MQTT_PKT_PUBREC:
        case MQTT_PKT_PUBCOMP:
        case MQTT_PKT_SUBACK:
        case MQTT_PKT_UNSUBACK:
        case MQTT_PKT_PINGREQ:
        case MQTT_PKT_PINGRESP:
        case MQTT_PKT_DISCONNECT:
            if (flags != 0x00) {
                log_warn("Protocol violation: Packet %s (type %u) MUST have flags 0000, got 0x%02X",
                         mqtt_packet_type_str(type), type, flags);
                return MQTT_ERR_INVALID_FLAGS;
            }
            break;

        case MQTT_PKT_PUBREL:
        case MQTT_PKT_SUBSCRIBE:
        case MQTT_PKT_UNSUBSCRIBE:
            if (flags != 0x02) {
                log_warn("Protocol violation: Packet %s (type %u) MUST have flags 0010, got 0x%02X",
                         mqtt_packet_type_str(type), type, flags);
                return MQTT_ERR_INVALID_FLAGS;
            }
            break;

        case MQTT_PKT_PUBLISH: {
            uint8_t qos = MQTT_EXTRACT_QOS(flags);
            uint8_t dup = MQTT_EXTRACT_DUP(flags);
            if (qos == 3) {
                log_warn("Protocol violation: PUBLISH packet QoS cannot be 3");
                return MQTT_ERR_INVALID_FLAGS;
            }
            if (qos == 0 && dup != 0) {
                log_warn("Protocol violation: PUBLISH packet QoS 0 cannot have DUP flag set");
                return MQTT_ERR_INVALID_FLAGS;
            }
            break;
        }

        default:
            return MQTT_ERR_MALFORMED;
    }

    /* Decode remaining length (starts at offset 1) */
    uint32_t rem_len = 0;
    size_t rem_len_bytes = 0;
    int rc = mqtt_decode_remaining_length(&buf[1], len - 1, &rem_len, &rem_len_bytes);
    if (rc != MQTT_SUCCESS) {
        return rc;
    }

    out_header->type = type;
    out_header->flags = flags;
    out_header->dup = MQTT_EXTRACT_DUP(flags);
    out_header->qos = MQTT_EXTRACT_QOS(flags);
    out_header->retain = MQTT_EXTRACT_RETAIN(flags);
    out_header->remaining_length = rem_len;
    out_header->remaining_length_bytes = rem_len_bytes;
    out_header->header_size = 1 + rem_len_bytes;
    out_header->total_packet_size = out_header->header_size + rem_len;

    if (out_header->total_packet_size > MQTT_MAX_PACKET_SIZE) {
        log_warn("Packet size %zu exceeds maximum limit %u", 
                 out_header->total_packet_size, MQTT_MAX_PACKET_SIZE);
        return MQTT_ERR_BUFFER_OVERFLOW;
    }

    return MQTT_SUCCESS;
}

int mqtt_decode_string(const uint8_t *buf, size_t len, size_t *offset, char *dest, size_t max_dest_len) {
    if (!buf || !offset || !dest || max_dest_len == 0) return MQTT_ERR_MALFORMED;

    if (*offset + 2 > len) return MQTT_NEED_MORE_DATA;

    uint16_t str_len = (uint16_t)((buf[*offset] << 8) | buf[*offset + 1]);
    *offset += 2;

    if (*offset + str_len > len) return MQTT_NEED_MORE_DATA;

    if (str_len >= max_dest_len) {
        /* Truncate or reject */
        memcpy(dest, &buf[*offset], max_dest_len - 1);
        dest[max_dest_len - 1] = '\0';
    } else {
        memcpy(dest, &buf[*offset], str_len);
        dest[str_len] = '\0';
    }

    *offset += str_len;
    return MQTT_SUCCESS;
}

int mqtt_decode_connect(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_connect_pkt_t *out_pkt) {
    if (!buf || !fh || !out_pkt) return MQTT_ERR_MALFORMED;
    memset(out_pkt, 0, sizeof(mqtt_connect_pkt_t));

    size_t offset = fh->header_size;

    /* 1. Protocol Name (2-byte length + UTF-8 string) */
    int rc = mqtt_decode_string(buf, total_len, &offset, out_pkt->protocol_name, sizeof(out_pkt->protocol_name));
    if (rc != MQTT_SUCCESS) return rc;

    if (strcmp(out_pkt->protocol_name, MQTT_PROTOCOL_NAME) != 0) {
        log_warn("Invalid protocol name: '%s' (expected '%s')", out_pkt->protocol_name, MQTT_PROTOCOL_NAME);
        return MQTT_ERR_INVALID_PROTOCOL;
    }

    /* 2. Protocol Level (1 byte) */
    if (offset >= total_len) return MQTT_NEED_MORE_DATA;
    out_pkt->protocol_level = buf[offset++];
    if (out_pkt->protocol_level != MQTT_PROTOCOL_LEVEL_311) {
        log_warn("Unsupported protocol level: 0x%02X (expected 0x%02X)", 
                 out_pkt->protocol_level, MQTT_PROTOCOL_LEVEL_311);
        return MQTT_ERR_INVALID_PROTOCOL;
    }

    /* 3. Connect Flags (1 byte) */
    if (offset >= total_len) return MQTT_NEED_MORE_DATA;
    uint8_t flags = buf[offset++];

    /* Reserved bit 0 MUST be 0 */
    if ((flags & 0x01) != 0) {
        log_warn("Protocol violation: CONNECT flag reserved bit 0 is not 0");
        return MQTT_ERR_MALFORMED;
    }

    out_pkt->clean_session = (flags >> 1) & 0x01;
    out_pkt->will_flag     = (flags >> 2) & 0x01;
    out_pkt->will_qos      = (flags >> 3) & 0x03;
    out_pkt->will_retain   = (flags >> 5) & 0x01;
    out_pkt->password_flag = (flags >> 6) & 0x01;
    out_pkt->username_flag = (flags >> 7) & 0x01;

    /* If Will Flag is 0, Will QoS and Will Retain MUST be 0 */
    if (out_pkt->will_flag == 0 && (out_pkt->will_qos != 0 || out_pkt->will_retain != 0)) {
        log_warn("Protocol violation: Will QoS or Retain set when Will Flag is 0");
        return MQTT_ERR_MALFORMED;
    }
    if (out_pkt->will_qos == 3) {
        log_warn("Protocol violation: Will QoS cannot be 3");
        return MQTT_ERR_MALFORMED;
    }
    /* If Username flag is 0, Password flag MUST be 0 */
    if (out_pkt->username_flag == 0 && out_pkt->password_flag != 0) {
        log_warn("Protocol violation: Password Flag set without Username Flag");
        return MQTT_ERR_MALFORMED;
    }

    /* 4. Keep Alive (2 bytes, big-endian) */
    if (offset + 2 > total_len) return MQTT_NEED_MORE_DATA;
    out_pkt->keep_alive = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
    offset += 2;

    /* 5. Payload: Client Identifier */
    rc = mqtt_decode_string(buf, total_len, &offset, out_pkt->client_id, sizeof(out_pkt->client_id));
    if (rc != MQTT_SUCCESS) return rc;

    if (strlen(out_pkt->client_id) == 0 && out_pkt->clean_session == 0) {
        log_warn("Protocol violation: Zero-byte ClientId requires CleanSession=1");
        return MQTT_ERR_INVALID_CLIENT_ID;
    }

    /* 6. Will Topic and Will Message if Will Flag set */
    if (out_pkt->will_flag) {
        rc = mqtt_decode_string(buf, total_len, &offset, out_pkt->will_topic, sizeof(out_pkt->will_topic));
        if (rc != MQTT_SUCCESS) return rc;

        if (offset + 2 > total_len) return MQTT_NEED_MORE_DATA;
        out_pkt->will_message_len = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
        offset += 2;

        if (offset + out_pkt->will_message_len > total_len) return MQTT_NEED_MORE_DATA;
        size_t copy_len = out_pkt->will_message_len < sizeof(out_pkt->will_message) ?
                          out_pkt->will_message_len : sizeof(out_pkt->will_message) - 1;
        memcpy(out_pkt->will_message, &buf[offset], copy_len);
        offset += out_pkt->will_message_len;
    }

    /* 7. Username if User Name Flag set */
    if (out_pkt->username_flag) {
        rc = mqtt_decode_string(buf, total_len, &offset, out_pkt->username, sizeof(out_pkt->username));
        if (rc != MQTT_SUCCESS) return rc;
    }

    /* 8. Password if Password Flag set */
    if (out_pkt->password_flag) {
        if (offset + 2 > total_len) return MQTT_NEED_MORE_DATA;
        out_pkt->password_len = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
        offset += 2;

        if (offset + out_pkt->password_len > total_len) return MQTT_NEED_MORE_DATA;
        size_t copy_len = out_pkt->password_len < sizeof(out_pkt->password) - 1 ?
                          out_pkt->password_len : sizeof(out_pkt->password) - 1;
        memcpy(out_pkt->password, &buf[offset], copy_len);
        out_pkt->password[copy_len] = '\0';
        offset += out_pkt->password_len;
    }

    if (offset != total_len) {
        log_warn("CONNECT payload length mismatch: consumed %zu, total %zu", offset, total_len);
        return MQTT_ERR_MALFORMED;
    }

    return MQTT_SUCCESS;
}

int mqtt_decode_publish(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_publish_pkt_t *out_pkt) {
    if (!buf || !fh || !out_pkt) return MQTT_ERR_MALFORMED;
    memset(out_pkt, 0, sizeof(mqtt_publish_pkt_t));

    out_pkt->dup = fh->dup;
    out_pkt->qos = fh->qos;
    out_pkt->retain = fh->retain;

    size_t offset = fh->header_size;

    /* 1. Topic Name */
    int rc = mqtt_decode_string(buf, total_len, &offset, out_pkt->topic, sizeof(out_pkt->topic));
    if (rc != MQTT_SUCCESS) return rc;

    /* Wildcards '+' and '#' are prohibited in PUBLISH topic names (MQTT-4.7.1-1) */
    if (strchr(out_pkt->topic, '+') != NULL || strchr(out_pkt->topic, '#') != NULL) {
        log_warn("Protocol violation: PUBLISH topic '%s' contains wildcard", out_pkt->topic);
        return MQTT_ERR_INVALID_TOPIC;
    }

    /* 2. Packet Identifier (only present if QoS > 0) */
    if (out_pkt->qos > 0) {
        if (offset + 2 > total_len) return MQTT_NEED_MORE_DATA;
        out_pkt->packet_id = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
        offset += 2;
        if (out_pkt->packet_id == 0) {
            log_warn("Protocol violation: Packet Identifier cannot be 0 for QoS > 0");
            return MQTT_ERR_MALFORMED;
        }
    } else {
        out_pkt->packet_id = 0;
    }

    /* 3. Payload (remaining bytes) */
    if (offset > total_len) return MQTT_ERR_MALFORMED;
    out_pkt->payload_len = total_len - offset;
    out_pkt->payload = (out_pkt->payload_len > 0) ? &buf[offset] : NULL;

    return MQTT_SUCCESS;
}

int mqtt_decode_subscribe(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_subscribe_pkt_t *out_pkt) {
    if (!buf || !fh || !out_pkt) return MQTT_ERR_MALFORMED;
    memset(out_pkt, 0, sizeof(mqtt_subscribe_pkt_t));

    size_t offset = fh->header_size;

    /* 1. Packet Identifier (2 bytes) */
    if (offset + 2 > total_len) return MQTT_NEED_MORE_DATA;
    out_pkt->packet_id = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
    offset += 2;

    if (out_pkt->packet_id == 0) {
        log_warn("Protocol violation: SUBSCRIBE Packet Identifier cannot be 0");
        return MQTT_ERR_MALFORMED;
    }

    /* 2. Topic Filters and Requested QoS */
    out_pkt->topic_count = 0;
    while (offset < total_len) {
        if (out_pkt->topic_count >= MQTT_MAX_SUB_TOPICS) {
            log_warn("Exceeded max topics per SUBSCRIBE packet");
            break;
        }

        mqtt_sub_topic_t *entry = &out_pkt->topics[out_pkt->topic_count];
        int rc = mqtt_decode_string(buf, total_len, &offset, entry->topic_filter, sizeof(entry->topic_filter));
        if (rc != MQTT_SUCCESS) return rc;

        if (offset >= total_len) return MQTT_NEED_MORE_DATA;
        uint8_t qos_byte = buf[offset++];
        
        /* Reserved bits 7-2 MUST be 0 */
        if ((qos_byte & 0xFC) != 0 || (qos_byte & 0x03) > 2) {
            log_warn("Protocol violation: SUBSCRIBE requested QoS invalid: 0x%02X", qos_byte);
            return MQTT_ERR_MALFORMED;
        }

        entry->qos = qos_byte & 0x03;
        out_pkt->topic_count++;
    }

    /* SUBSCRIBE packet MUST contain at least one topic filter (MQTT-3.8.3-3) */
    if (out_pkt->topic_count == 0) {
        log_warn("Protocol violation: SUBSCRIBE packet has empty topic list");
        return MQTT_ERR_MALFORMED;
    }

    return MQTT_SUCCESS;
}

int mqtt_decode_unsubscribe(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_unsubscribe_pkt_t *out_pkt) {
    if (!buf || !fh || !out_pkt) return MQTT_ERR_MALFORMED;
    memset(out_pkt, 0, sizeof(mqtt_unsubscribe_pkt_t));

    size_t offset = fh->header_size;

    if (offset + 2 > total_len) return MQTT_NEED_MORE_DATA;
    out_pkt->packet_id = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
    offset += 2;

    out_pkt->topic_count = 0;
    while (offset < total_len) {
        if (out_pkt->topic_count >= MQTT_MAX_SUB_TOPICS) break;
        int rc = mqtt_decode_string(buf, total_len, &offset, out_pkt->topics[out_pkt->topic_count], MQTT_MAX_TOPIC_LEN);
        if (rc != MQTT_SUCCESS) return rc;
        out_pkt->topic_count++;
    }

    if (out_pkt->topic_count == 0) {
        return MQTT_ERR_MALFORMED;
    }

    return MQTT_SUCCESS;
}

int mqtt_decode_ack(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_ack_pkt_t *out_pkt) {
    if (!buf || !fh || !out_pkt) return MQTT_ERR_MALFORMED;
    if (fh->remaining_length != 2) {
        log_warn("ACK packet remaining length must be 2, got %u", fh->remaining_length);
        return MQTT_ERR_MALFORMED;
    }

    size_t offset = fh->header_size;
    if (offset + 2 > total_len) return MQTT_NEED_MORE_DATA;

    out_pkt->packet_id = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
    return MQTT_SUCCESS;
}

int mqtt_decode_packet(const uint8_t *buf, size_t len, mqtt_packet_t *out_packet) {
    if (!buf || !out_packet) return MQTT_ERR_MALFORMED;

    /* 1. Decode and validate fixed header */
    int rc = mqtt_decode_fixed_header(buf, len, &out_packet->fixed_header);
    if (rc != MQTT_SUCCESS) {
        return rc;
    }

    /* 2. Check if complete packet has arrived in stream buffer */
    if (len < out_packet->fixed_header.total_packet_size) {
        return MQTT_NEED_MORE_DATA;
    }

    size_t total_packet_size = out_packet->fixed_header.total_packet_size;

    /* 3. Dispatch to packet-specific decoders */
    switch (out_packet->fixed_header.type) {
        case MQTT_PKT_CONNECT:
            return mqtt_decode_connect(buf, total_packet_size, &out_packet->fixed_header, &out_packet->connect);

        case MQTT_PKT_PUBLISH:
            return mqtt_decode_publish(buf, total_packet_size, &out_packet->fixed_header, &out_packet->publish);

        case MQTT_PKT_SUBSCRIBE:
            return mqtt_decode_subscribe(buf, total_packet_size, &out_packet->fixed_header, &out_packet->subscribe);

        case MQTT_PKT_UNSUBSCRIBE:
            return mqtt_decode_unsubscribe(buf, total_packet_size, &out_packet->fixed_header, &out_packet->unsubscribe);

        case MQTT_PKT_PUBACK:
        case MQTT_PKT_PUBREC:
        case MQTT_PKT_PUBREL:
        case MQTT_PKT_PUBCOMP:
            return mqtt_decode_ack(buf, total_packet_size, &out_packet->fixed_header, &out_packet->ack);

        case MQTT_PKT_PINGREQ:
        case MQTT_PKT_DISCONNECT:
            if (out_packet->fixed_header.remaining_length != 0) {
                log_warn("Packet %s must have remaining length 0", 
                         mqtt_packet_type_str(out_packet->fixed_header.type));
                return MQTT_ERR_MALFORMED;
            }
            return MQTT_SUCCESS;

        default:
            log_warn("Unhandled control packet type: %u", out_packet->fixed_header.type);
            return MQTT_ERR_MALFORMED;
    }
}
