#include "mqtt_encoder.h"
#include "mqtt_parser.h"
#include <string.h>

size_t mqtt_encode_connack(uint8_t *dest, size_t max_len, uint8_t session_present, uint8_t return_code) {
    if (!dest || max_len < 4) return 0;

    dest[0] = (uint8_t)(MQTT_PKT_CONNACK << 4); /* 0x20 */
    dest[1] = 0x02;                             /* Remaining length = 2 */
    dest[2] = session_present & 0x01;           /* Session Present flag */
    dest[3] = return_code;                      /* Return code */
    return 4;
}

size_t mqtt_encode_pingresp(uint8_t *dest, size_t max_len) {
    if (!dest || max_len < 2) return 0;

    dest[0] = (uint8_t)(MQTT_PKT_PINGRESP << 4); /* 0xD0 */
    dest[1] = 0x00;                              /* Remaining length = 0 */
    return 2;
}

size_t mqtt_encode_puback(uint8_t *dest, size_t max_len, uint16_t packet_id) {
    if (!dest || max_len < 4) return 0;

    dest[0] = (uint8_t)(MQTT_PKT_PUBACK << 4); /* 0x40 */
    dest[1] = 0x02;                            /* Remaining length = 2 */
    dest[2] = (uint8_t)((packet_id >> 8) & 0xFF);
    dest[3] = (uint8_t)(packet_id & 0xFF);
    return 4;
}

size_t mqtt_encode_suback(uint8_t *dest, size_t max_len, uint16_t packet_id, const uint8_t *return_codes, size_t count) {
    if (!dest || !return_codes || count == 0) return 0;

    uint32_t rem_len = 2 + (uint32_t)count;
    uint8_t rem_bytes[4];
    size_t rem_byte_count = mqtt_encode_remaining_length(rem_bytes, rem_len);

    size_t total_size = 1 + rem_byte_count + rem_len;
    if (total_size > max_len) return 0;

    size_t offset = 0;
    dest[offset++] = (uint8_t)(MQTT_PKT_SUBACK << 4); /* 0x90 */

    for (size_t i = 0; i < rem_byte_count; i++) {
        dest[offset++] = rem_bytes[i];
    }

    dest[offset++] = (uint8_t)((packet_id >> 8) & 0xFF);
    dest[offset++] = (uint8_t)(packet_id & 0xFF);

    for (size_t i = 0; i < count; i++) {
        dest[offset++] = return_codes[i];
    }

    return offset;
}

size_t mqtt_encode_publish(uint8_t *dest, size_t max_len, const char *topic, uint16_t packet_id,
                           uint8_t qos, uint8_t retain, uint8_t dup,
                           const uint8_t *payload, size_t payload_len) {
    if (!dest || !topic) return 0;

    size_t topic_len = strlen(topic);
    uint32_t rem_len = 2 + (uint32_t)topic_len + (qos > 0 ? 2 : 0) + (uint32_t)payload_len;

    uint8_t rem_bytes[4];
    size_t rem_byte_count = mqtt_encode_remaining_length(rem_bytes, rem_len);

    size_t total_size = 1 + rem_byte_count + rem_len;
    if (total_size > max_len) return 0;

    size_t offset = 0;
    uint8_t flags = ((dup & 0x01) << 3) | ((qos & 0x03) << 1) | (retain & 0x01);
    dest[offset++] = (uint8_t)((MQTT_PKT_PUBLISH << 4) | flags);

    for (size_t i = 0; i < rem_byte_count; i++) {
        dest[offset++] = rem_bytes[i];
    }

    /* Topic length + Topic */
    dest[offset++] = (uint8_t)((topic_len >> 8) & 0xFF);
    dest[offset++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&dest[offset], topic, topic_len);
    offset += topic_len;

    /* Packet Identifier if QoS > 0 */
    if (qos > 0) {
        dest[offset++] = (uint8_t)((packet_id >> 8) & 0xFF);
        dest[offset++] = (uint8_t)(packet_id & 0xFF);
    }

    /* Payload */
    if (payload && payload_len > 0) {
        memcpy(&dest[offset], payload, payload_len);
        offset += payload_len;
    }

    return offset;
}
