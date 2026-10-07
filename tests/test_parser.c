#include "mqtt_protocol.h"
#include "mqtt_packet.h"
#include "mqtt_parser.h"
#include "mqtt_encoder.h"
#include "buffer.h"
#include "logger.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_remaining_length_encoding_decoding(void) {
    printf("[TEST] Running test_remaining_length_encoding_decoding...\n");
    
    uint32_t test_values[] = {
        0, 1, 64, 127,           /* 1 byte */
        128, 255, 16383,        /* 2 bytes */
        16384, 65535, 2097151,  /* 3 bytes */
        2097152, 268435455      /* 4 bytes (max) */
    };
    size_t count = sizeof(test_values) / sizeof(test_values[0]);

    for (size_t i = 0; i < count; i++) {
        uint32_t val = test_values[i];
        uint8_t encoded[4];
        size_t enc_bytes = mqtt_encode_remaining_length(encoded, val);
        assert(enc_bytes >= 1 && enc_bytes <= 4);

        uint32_t decoded_val = 0;
        size_t dec_bytes = 0;
        int rc = mqtt_decode_remaining_length(encoded, enc_bytes, &decoded_val, &dec_bytes);

        assert(rc == MQTT_SUCCESS);
        assert(decoded_val == val);
        assert(dec_bytes == enc_bytes);
    }

    /* Test incomplete remaining length */
    uint8_t partial[] = { 0x80 }; /* Continuation bit set, but no 2nd byte */
    uint32_t val = 0;
    size_t bytes = 0;
    int rc = mqtt_decode_remaining_length(partial, 1, &val, &bytes);
    assert(rc == MQTT_NEED_MORE_DATA);

    /* Test malformed 5-byte overflow */
    uint8_t overflow[] = { 0x80, 0x80, 0x80, 0x80, 0x80 };
    rc = mqtt_decode_remaining_length(overflow, 5, &val, &bytes);
    assert(rc == MQTT_ERR_MALFORMED);

    printf("  -> PASS: test_remaining_length_encoding_decoding\n");
}

void test_fixed_header_flags(void) {
    printf("[TEST] Running test_fixed_header_flags...\n");

    mqtt_fixed_header_t fh;

    /* Valid CONNECT (0x10 0x00) */
    uint8_t valid_connect[] = { 0x10, 0x00 };
    assert(mqtt_decode_fixed_header(valid_connect, 2, &fh) == MQTT_SUCCESS);
    assert(fh.type == MQTT_PKT_CONNECT);
    assert(fh.flags == 0x00);
    assert(fh.remaining_length == 0);

    /* Invalid CONNECT with reserved flags set (0x12 0x00) */
    uint8_t invalid_connect[] = { 0x12, 0x00 };
    assert(mqtt_decode_fixed_header(invalid_connect, 2, &fh) == MQTT_ERR_INVALID_FLAGS);

    /* Valid SUBSCRIBE (0x82 0x05) - Flags MUST be 0010 */
    uint8_t valid_subscribe[] = { 0x82, 0x05 };
    assert(mqtt_decode_fixed_header(valid_subscribe, 2, &fh) == MQTT_SUCCESS);
    assert(fh.type == MQTT_PKT_SUBSCRIBE);
    assert(fh.flags == 0x02);

    /* Invalid SUBSCRIBE with flags 0000 (0x80 0x05) */
    uint8_t invalid_subscribe[] = { 0x80, 0x05 };
    assert(mqtt_decode_fixed_header(invalid_subscribe, 2, &fh) == MQTT_ERR_INVALID_FLAGS);

    /* Valid PUBLISH (0x30 QoS 0, 0x32 QoS 1, 0x34 QoS 2, 0x35 QoS 2 Retain) */
    uint8_t pub_qos0[] = { 0x30, 0x00 };
    assert(mqtt_decode_fixed_header(pub_qos0, 2, &fh) == MQTT_SUCCESS);
    assert(fh.qos == 0);
    assert(fh.dup == 0);
    assert(fh.retain == 0);

    uint8_t pub_qos1[] = { 0x32, 0x00 };
    assert(mqtt_decode_fixed_header(pub_qos1, 2, &fh) == MQTT_SUCCESS);
    assert(fh.qos == 1);

    uint8_t pub_invalid_qos3[] = { 0x36, 0x00 }; /* QoS 3 is forbidden */
    assert(mqtt_decode_fixed_header(pub_invalid_qos3, 2, &fh) == MQTT_ERR_INVALID_FLAGS);

    uint8_t pub_qos0_dup1[] = { 0x38, 0x00 }; /* QoS 0 with DUP=1 is forbidden */
    assert(mqtt_decode_fixed_header(pub_qos0_dup1, 2, &fh) == MQTT_ERR_INVALID_FLAGS);

    /* Valid PINGREQ (0xC0 0x00) */
    uint8_t valid_ping[] = { 0xC0, 0x00 };
    assert(mqtt_decode_fixed_header(valid_ping, 2, &fh) == MQTT_SUCCESS);
    assert(fh.type == MQTT_PKT_PINGREQ);

    /* Valid DISCONNECT (0xE0 0x00) */
    uint8_t valid_disc[] = { 0xE0, 0x00 };
    assert(mqtt_decode_fixed_header(valid_disc, 2, &fh) == MQTT_SUCCESS);
    assert(fh.type == MQTT_PKT_DISCONNECT);

    printf("  -> PASS: test_fixed_header_flags\n");
}

void test_connect_packet_parsing(void) {
    printf("[TEST] Running test_connect_packet_parsing...\n");

    /* Construct a valid binary CONNECT packet */
    /* Fixed Header: 0x10, remaining length = 18 */
    /* Variable Header:
       Protocol Name length: 0x00, 0x04, "MQTT"
       Protocol Level: 0x04
       Connect Flags: 0x02 (CleanSession=1)
       Keep Alive: 0x00, 0x3C (60s)
       Payload:
       Client ID: length 0x00, 0x07, "client1"
    */
    uint8_t raw_connect[] = {
        0x10, 19,
        0x00, 0x04, 'M', 'Q', 'T', 'T',
        0x04,
        0x02,
        0x00, 0x3C,
        0x00, 0x07, 'c', 'l', 'i', 'e', 'n', 't', '1'
    };

    mqtt_packet_t pkt;
    int rc = mqtt_decode_packet(raw_connect, sizeof(raw_connect), &pkt);
    assert(rc == MQTT_SUCCESS);
    assert(pkt.fixed_header.type == MQTT_PKT_CONNECT);
    assert(pkt.connect.clean_session == 1);
    assert(pkt.connect.keep_alive == 60);
    assert(strcmp(pkt.connect.client_id, "client1") == 0);
    assert(strcmp(pkt.connect.protocol_name, "MQTT") == 0);
    assert(pkt.connect.protocol_level == 4);

    /* Test invalid protocol level (e.g., 0x03) */
    uint8_t raw_invalid_ver[sizeof(raw_connect)];
    memcpy(raw_invalid_ver, raw_connect, sizeof(raw_connect));
    raw_invalid_ver[8] = 0x03; /* Protocol level 3 */
    rc = mqtt_decode_packet(raw_invalid_ver, sizeof(raw_invalid_ver), &pkt);
    assert(rc == MQTT_ERR_INVALID_PROTOCOL);

    /* Test invalid protocol name (e.g., "MQIsdp") */
    uint8_t raw_invalid_proto[] = {
        0x10, 21,
        0x00, 0x06, 'M', 'Q', 'I', 's', 'd', 'p',
        0x03,
        0x02,
        0x00, 0x3C,
        0x00, 0x07, 'c', 'l', 'i', 'e', 'n', 't', '1'
    };
    rc = mqtt_decode_packet(raw_invalid_proto, sizeof(raw_invalid_proto), &pkt);
    assert(rc == MQTT_ERR_INVALID_PROTOCOL);

    /* Test connect flag reserved bit 0 set (protocol violation) */
    uint8_t raw_reserved_flag[sizeof(raw_connect)];
    memcpy(raw_reserved_flag, raw_connect, sizeof(raw_connect));
    raw_reserved_flag[9] = 0x03; /* Bit 0 is 1 */
    rc = mqtt_decode_packet(raw_reserved_flag, sizeof(raw_reserved_flag), &pkt);
    assert(rc == MQTT_ERR_MALFORMED);

    printf("  -> PASS: test_connect_packet_parsing\n");
}

void test_publish_packet_parsing(void) {
    printf("[TEST] Running test_publish_packet_parsing...\n");

    /* PUBLISH QoS 0: Topic "sensors/temp", Payload "24.5C" */
    /* Variable Header: Topic len 0x00 0x0C, "sensors/temp"
       Payload: "24.5C" (5 bytes)
       Remaining length = 2 + 12 + 5 = 19
    */
    uint8_t raw_pub_qos0[] = {
        0x30, 19,
        0x00, 0x0C, 's', 'e', 'n', 's', 'o', 'r', 's', '/', 't', 'e', 'm', 'p',
        '2', '4', '.', '5', 'C'
    };

    mqtt_packet_t pkt;
    int rc = mqtt_decode_packet(raw_pub_qos0, sizeof(raw_pub_qos0), &pkt);
    assert(rc == MQTT_SUCCESS);
    assert(pkt.fixed_header.type == MQTT_PKT_PUBLISH);
    assert(pkt.publish.qos == 0);
    assert(strcmp(pkt.publish.topic, "sensors/temp") == 0);
    assert(pkt.publish.payload_len == 5);
    assert(memcmp(pkt.publish.payload, "24.5C", 5) == 0);

    /* PUBLISH QoS 1: Topic "home/light", PacketID 100, Payload "ON" */
    /* Remaining length: 2 + 10 + 2 + 2 = 16 */
    uint8_t raw_pub_qos1[] = {
        0x32, 16,
        0x00, 0x0A, 'h', 'o', 'm', 'e', '/', 'l', 'i', 'g', 'h', 't',
        0x00, 0x64, /* Packet ID 100 */
        'O', 'N'
    };

    rc = mqtt_decode_packet(raw_pub_qos1, sizeof(raw_pub_qos1), &pkt);
    assert(rc == MQTT_SUCCESS);
    assert(pkt.publish.qos == 1);
    assert(pkt.publish.packet_id == 100);
    assert(strcmp(pkt.publish.topic, "home/light") == 0);
    assert(pkt.publish.payload_len == 2);
    assert(memcmp(pkt.publish.payload, "ON", 2) == 0);

    /* PUBLISH with wildcard in topic -> MUST be rejected */
    uint8_t raw_pub_wildcard[] = {
        0x30, 15,
        0x00, 0x08, 's', 'e', 'n', 's', 'o', 'r', 's', '+',
        '2', '4', '.', '5', 'C'
    };
    rc = mqtt_decode_packet(raw_pub_wildcard, sizeof(raw_pub_wildcard), &pkt);
    assert(rc == MQTT_ERR_INVALID_TOPIC);

    printf("  -> PASS: test_publish_packet_parsing\n");
}

void test_subscribe_packet_parsing(void) {
    printf("[TEST] Running test_subscribe_packet_parsing...\n");

    /* SUBSCRIBE PacketID 42, Topic "sensors/#" QoS 1, Topic "devices/+" QoS 0 */
    /* PacketID: 0x00 0x2A (2)
       Topic 1: 0x00 0x09 "sensors/#" (11) + QoS 0x01 (1) = 12
       Topic 2: 0x00 0x09 "devices/+" (11) + QoS 0x00 (1) = 12
       Total remaining length = 2 + 12 + 12 = 26
    */
    uint8_t raw_sub[] = {
        0x82, 26,
        0x00, 0x2A,
        0x00, 0x09, 's', 'e', 'n', 's', 'o', 'r', 's', '/', '#', 0x01,
        0x00, 0x09, 'd', 'e', 'v', 'i', 'c', 'e', 's', '/', '+', 0x00
    };

    mqtt_packet_t pkt;
    int rc = mqtt_decode_packet(raw_sub, sizeof(raw_sub), &pkt);
    assert(rc == MQTT_SUCCESS);
    assert(pkt.fixed_header.type == MQTT_PKT_SUBSCRIBE);
    assert(pkt.subscribe.packet_id == 42);
    assert(pkt.subscribe.topic_count == 2);
    assert(strcmp(pkt.subscribe.topics[0].topic_filter, "sensors/#") == 0);
    assert(pkt.subscribe.topics[0].qos == 1);
    assert(strcmp(pkt.subscribe.topics[1].topic_filter, "devices/+") == 0);
    assert(pkt.subscribe.topics[1].qos == 0);

    printf("  -> PASS: test_subscribe_packet_parsing\n");
}

void test_stream_defragmentation(void) {
    printf("[TEST] Running test_stream_defragmentation (byte-by-byte TCP stream accumulation)...\n");

    /* We take a valid PUBLISH packet and feed it 1 byte at a time into byte_buffer_t */
    uint8_t raw_pub[] = {
        0x30, 11,
        0x00, 0x04, 't', 'e', 's', 't',
        'D', 'A', 'T', 'A', '!'
    };
    size_t total_len = sizeof(raw_pub);

    byte_buffer_t rx_buf;
    byte_buffer_init(&rx_buf, 64, 1024);

    mqtt_packet_t pkt;

    for (size_t i = 0; i < total_len; i++) {
        /* Append 1 byte to buffer */
        byte_buffer_append(&rx_buf, &raw_pub[i], 1);

        int rc = mqtt_decode_packet(rx_buf.data, rx_buf.size, &pkt);

        if (i < total_len - 1) {
            /* Before the last byte arrives, decoder MUST return MQTT_NEED_MORE_DATA */
            assert(rc == MQTT_NEED_MORE_DATA);
        } else {
            /* Once the final byte is received, packet must be decoded successfully! */
            assert(rc == MQTT_SUCCESS);
            assert(pkt.fixed_header.type == MQTT_PKT_PUBLISH);
            assert(strcmp(pkt.publish.topic, "test") == 0);
            assert(pkt.publish.payload_len == 5);
            assert(memcmp(pkt.publish.payload, "DATA!", 5) == 0);
        }
    }

    /* Drain packet */
    byte_buffer_drain(&rx_buf, pkt.fixed_header.total_packet_size);
    assert(rx_buf.size == 0);

    byte_buffer_free(&rx_buf);
    printf("  -> PASS: test_stream_defragmentation\n");
}

void test_packet_pipelining(void) {
    printf("[TEST] Running test_packet_pipelining (multiple packets in single TCP chunk)...\n");

    /* Create two packets glued together in one stream: PINGREQ (2 bytes) + DISCONNECT (2 bytes) */
    uint8_t glued[] = {
        0xC0, 0x00,  /* PINGREQ */
        0xE0, 0x00   /* DISCONNECT */
    };

    byte_buffer_t rx_buf;
    byte_buffer_init(&rx_buf, 64, 1024);
    byte_buffer_append(&rx_buf, glued, sizeof(glued));

    mqtt_packet_t pkt1, pkt2;

    /* Parse packet 1 */
    int rc1 = mqtt_decode_packet(rx_buf.data, rx_buf.size, &pkt1);
    assert(rc1 == MQTT_SUCCESS);
    assert(pkt1.fixed_header.type == MQTT_PKT_PINGREQ);
    byte_buffer_drain(&rx_buf, pkt1.fixed_header.total_packet_size);
    assert(rx_buf.size == 2);

    /* Parse packet 2 */
    int rc2 = mqtt_decode_packet(rx_buf.data, rx_buf.size, &pkt2);
    assert(rc2 == MQTT_SUCCESS);
    assert(pkt2.fixed_header.type == MQTT_PKT_DISCONNECT);
    byte_buffer_drain(&rx_buf, pkt2.fixed_header.total_packet_size);
    assert(rx_buf.size == 0);

    byte_buffer_free(&rx_buf);
    printf("  -> PASS: test_packet_pipelining\n");
}

int main(void) {
    logger_set_level(LOG_LEVEL_NONE); /* Silence logs during unit tests */
    printf("========================================\n");
    printf("RUNNING MQTT PARSER & FRAMING UNIT TESTS\n");
    printf("========================================\n");
    test_remaining_length_encoding_decoding();
    test_fixed_header_flags();
    test_connect_packet_parsing();
    test_publish_packet_parsing();
    test_subscribe_packet_parsing();
    test_stream_defragmentation();
    test_packet_pipelining();
    printf("ALL MQTT PARSER TESTS PASSED SUCCESSFULLY!\n\n");
    return 0;
}
