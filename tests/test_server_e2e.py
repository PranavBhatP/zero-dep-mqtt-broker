#!/usr/bin/env python3
"""
End-to-End Integration Test Suite for Zero-Dependency MQTT Broker
Course: IT365 Advanced Computer Networks
Zero third-party library dependencies (pure Python standard library: socket, struct, subprocess)
"""

import socket
import struct
import subprocess
import time
import sys
import os
import signal

PORT = 11883
HOST = "127.0.0.1"

def build_connect_packet(client_id="test_client", clean_session=True, keep_alive=60):
    # Variable header: Protocol Name ("MQTT"), Level (4), Connect Flags, Keep Alive (2 bytes)
    proto_name = b"MQTT"
    var_header = struct.pack("!H", len(proto_name)) + proto_name
    var_header += bytes([0x04]) # Level 4 (MQTT 3.1.1)
    
    flags = 0x02 if clean_session else 0x00
    var_header += bytes([flags])
    var_header += struct.pack("!H", keep_alive)

    # Payload: Client ID
    cid_bytes = client_id.encode("utf-8")
    payload = struct.pack("!H", len(cid_bytes)) + cid_bytes

    rem_len = len(var_header) + len(payload)
    fixed_header = bytes([0x10, rem_len]) # CONNECT packet
    return fixed_header + var_header + payload

def build_pingreq_packet():
    return bytes([0xC0, 0x00])

def build_disconnect_packet():
    return bytes([0xE0, 0x00])

def build_publish_packet(topic, payload, qos=0, packet_id=1):
    topic_bytes = topic.encode("utf-8")
    var_header = struct.pack("!H", len(topic_bytes)) + topic_bytes
    if qos > 0:
        var_header += struct.pack("!H", packet_id)
    
    p_bytes = payload if isinstance(payload, bytes) else payload.encode("utf-8")
    rem_len = len(var_header) + len(p_bytes)

    flags = (qos & 0x03) << 1
    fixed_header = bytes([0x30 | flags, rem_len])
    return fixed_header + var_header + p_bytes

def build_subscribe_packet(packet_id, topics):
    # topics is a list of (topic_filter_str, qos_int)
    var_header = struct.pack("!H", packet_id)
    payload = bytearray()
    for tf, qos in topics:
        tf_bytes = tf.encode("utf-8")
        payload += struct.pack("!H", len(tf_bytes)) + tf_bytes
        payload.append(qos)

    rem_len = len(var_header) + len(payload)
    fixed_header = bytes([0x82, rem_len])
    return fixed_header + var_header + bytes(payload)

def test_handshake(sock):
    print("  -> Testing CONNECT handshake...")
    pkt = build_connect_packet("client_handshake")
    sock.sendall(pkt)
    resp = sock.recv(4)
    assert len(resp) == 4, f"Expected 4 bytes CONNACK, got {len(resp)}"
    assert resp[0] == 0x20, f"Expected CONNACK (0x20), got 0x{resp[0]:02X}"
    assert resp[1] == 0x02, f"Expected remaining length 2, got {resp[1]}"
    assert resp[3] == 0x00, f"Expected Connect Return Code 0 (Accepted), got {resp[3]}"
    print("     [PASS] CONNACK received successfully (Accepted)")

def test_ping(sock):
    print("  -> Testing PINGREQ heartbeat...")
    sock.sendall(build_pingreq_packet())
    resp = sock.recv(2)
    assert len(resp) == 2, f"Expected 2 bytes PINGRESP, got {len(resp)}"
    assert resp[0] == 0xD0, f"Expected PINGRESP (0xD0), got 0x{resp[0]:02X}"
    assert resp[1] == 0x00, f"Expected remaining length 0, got {resp[1]}"
    print("     [PASS] PINGRESP received successfully")

def test_publish_qos1(sock):
    print("  -> Testing PUBLISH QoS 1 and PUBACK...")
    pkt_id = 1337
    pkt = build_publish_packet("sensors/temperature", "28.5C", qos=1, packet_id=pkt_id)
    sock.sendall(pkt)
    resp = sock.recv(4)
    assert len(resp) == 4, f"Expected 4 bytes PUBACK, got {len(resp)}"
    assert resp[0] == 0x40, f"Expected PUBACK (0x40), got 0x{resp[0]:02X}"
    assert resp[1] == 0x02, f"Expected remaining length 2, got {resp[1]}"
    ack_id = struct.unpack("!H", resp[2:4])[0]
    assert ack_id == pkt_id, f"Expected packet ID {pkt_id}, got {ack_id}"
    print(f"     [PASS] PUBACK received with matching PacketID {ack_id}")

def test_subscribe(sock):
    print("  -> Testing SUBSCRIBE and SUBACK...")
    pkt_id = 99
    topics = [("home/livingroom/temp", 0), ("home/bedroom/+", 1)]
    pkt = build_subscribe_packet(pkt_id, topics)
    sock.sendall(pkt)
    resp = sock.recv(5) # 1 byte type, 1 byte rem_len (3), 2 bytes pkt_id, 2 bytes return codes
    assert resp[0] == 0x90, f"Expected SUBACK (0x90), got 0x{resp[0]:02X}"
    ack_id = struct.unpack("!H", resp[2:4])[0]
    assert ack_id == pkt_id, f"Expected packet ID {pkt_id}, got {ack_id}"
    assert resp[4] == 0x00, f"Expected QoS 0 granted for first topic, got {resp[4]}"
    print(f"     [PASS] SUBACK received for {len(topics)} topics with matching PacketID {ack_id}")

def test_stream_fragmentation():
    print("  -> Testing TCP Stream Fragmentation (sending 1 byte at a time with delays)...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    
    pkt = build_connect_packet("fragmented_client")
    for b in pkt:
        sock.sendall(bytes([b]))
        time.sleep(0.005) # 5ms delay per byte to force TCP packet splitting
    
    resp = sock.recv(4)
    assert len(resp) == 4
    assert resp[0] == 0x20 and resp[3] == 0x00
    print("     [PASS] Fragmented TCP stream properly defragmented and accepted")
    sock.close()

def test_concurrency(num_clients=25):
    print(f"  -> Testing Concurrent Client Handling ({num_clients} simultaneous sockets)...")
    sockets = []
    for i in range(num_clients):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORT))
        s.sendall(build_connect_packet(f"client_{i}"))
        sockets.append(s)

    # Verify all received CONNACK
    for i, s in enumerate(sockets):
        resp = s.recv(4)
        assert len(resp) == 4 and resp[0] == 0x20 and resp[3] == 0x00

    # Send PINGREQ on all simultaneously
    for s in sockets:
        s.sendall(build_pingreq_packet())

    for s in sockets:
        resp = s.recv(2)
        assert len(resp) == 2 and resp[0] == 0xD0

    # Clean disconnect all
    for s in sockets:
        s.sendall(build_disconnect_packet())
        s.close()

    print(f"     [PASS] Successfully handled {num_clients} concurrent client sessions")

def main():
    print("==================================================")
    print("STARTING END-TO-END INTEGRATION TEST SUITE")
    print(f"Target Broker: {HOST}:{PORT}")
    print("==================================================")

    broker_cmd = ["./mqtt_broker", "-p", str(PORT), "-h", HOST]
    proc = subprocess.Popen(broker_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.2) # Allow broker to bind and initialize epoll

    try:
        # Basic client connection
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))

        test_handshake(sock)
        test_ping(sock)
        test_publish_qos1(sock)
        test_subscribe(sock)
        
        sock.sendall(build_disconnect_packet())
        sock.close()

        # Advanced tests
        test_stream_fragmentation()
        test_concurrency(num_clients=30)

        print("\n==================================================")
        print("ALL END-TO-END INTEGRATION TESTS PASSED (100% OK)")
        print("==================================================")
    finally:
        proc.send_signal(signal.SIGINT)
        proc.wait(timeout=2)

if __name__ == "__main__":
    main()
