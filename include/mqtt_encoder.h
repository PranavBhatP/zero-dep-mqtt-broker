#ifndef MQTT_ENCODER_H
#define MQTT_ENCODER_H

#include "mqtt_protocol.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @file mqtt_encoder.h
 * @brief Binary packet serialization for MQTT broker responses
 */

/**
 * Encode CONNACK packet (Fixed size: 4 bytes)
 */
size_t mqtt_encode_connack(uint8_t *dest, size_t max_len, uint8_t session_present, uint8_t return_code);

/**
 * Encode PINGRESP packet (Fixed size: 2 bytes)
 */
size_t mqtt_encode_pingresp(uint8_t *dest, size_t max_len);

/**
 * Encode PUBACK packet (Fixed size: 4 bytes)
 */
size_t mqtt_encode_puback(uint8_t *dest, size_t max_len, uint16_t packet_id);

/**
 * Encode SUBACK packet
 */
size_t mqtt_encode_suback(uint8_t *dest, size_t max_len, uint16_t packet_id, const uint8_t *return_codes, size_t count);

/**
 * Encode PUBLISH packet for broker delivery
 */
size_t mqtt_encode_publish(uint8_t *dest, size_t max_len, const char *topic, uint16_t packet_id,
                           uint8_t qos, uint8_t retain, uint8_t dup,
                           const uint8_t *payload, size_t payload_len);

#endif /* MQTT_ENCODER_H */
