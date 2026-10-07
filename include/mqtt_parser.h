#ifndef MQTT_PARSER_H
#define MQTT_PARSER_H

#include "mqtt_protocol.h"
#include "mqtt_packet.h"
#include <stdbool.h>

/**
 * @file mqtt_parser.h
 * @brief Bitwise packet decoder for MQTT 3.1.1 fixed and variable length headers
 */

/**
 * Decode variable length integer from byte buffer (OASIS MQTT 3.1.1 Section 2.2.3)
 * 
 * @param buf Input byte buffer
 * @param len Buffer length
 * @param out_val Pointer to store decoded value
 * @param out_bytes Pointer to store number of bytes consumed (1-4)
 * @return MQTT_SUCCESS on success, MQTT_NEED_MORE_DATA if incomplete, MQTT_ERR_MALFORMED on error
 */
int mqtt_decode_remaining_length(const uint8_t *buf, size_t len, uint32_t *out_val, size_t *out_bytes);

/**
 * Encode an integer into MQTT variable-length representation
 * 
 * @param dest Output buffer (must have at least 4 bytes)
 * @param length Value to encode
 * @return Number of bytes written (1-4)
 */
size_t mqtt_encode_remaining_length(uint8_t *dest, uint32_t length);

/**
 * Decode fixed header from raw bytes and validate header flags
 * 
 * @param buf Input byte buffer
 * @param len Buffer length
 * @param out_header Pointer to store parsed fixed header
 * @return MQTT_SUCCESS, MQTT_NEED_MORE_DATA, or an MQTT_ERR_* status code
 */
int mqtt_decode_fixed_header(const uint8_t *buf, size_t len, mqtt_fixed_header_t *out_header);

/**
 * Decode complete MQTT packet from byte buffer.
 * Performs stream framing: checks if the buffer contains the full packet.
 * If only partial packet is received, returns MQTT_NEED_MORE_DATA without advancing.
 * 
 * @param buf Input byte buffer
 * @param len Buffer length
 * @param out_packet Pointer to store parsed packet
 * @return MQTT_SUCCESS on success, MQTT_NEED_MORE_DATA if fragment, or negative error code
 */
int mqtt_decode_packet(const uint8_t *buf, size_t len, mqtt_packet_t *out_packet);

/**
 * Helper to decode a 2-byte length-prefixed UTF-8 string
 */
int mqtt_decode_string(const uint8_t *buf, size_t len, size_t *offset, char *dest, size_t max_dest_len);

/**
 * Specific packet decoders
 */
int mqtt_decode_connect(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_connect_pkt_t *out_pkt);
int mqtt_decode_publish(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_publish_pkt_t *out_pkt);
int mqtt_decode_subscribe(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_subscribe_pkt_t *out_pkt);
int mqtt_decode_unsubscribe(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_unsubscribe_pkt_t *out_pkt);
int mqtt_decode_ack(const uint8_t *buf, size_t total_len, const mqtt_fixed_header_t *fh, mqtt_ack_pkt_t *out_pkt);

#endif /* MQTT_PARSER_H */
