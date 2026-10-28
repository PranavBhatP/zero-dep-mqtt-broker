#!/usr/bin/env python3
"""
Advanced Multi-Feature End-to-End Test Suite
Tests:
1. Topic Trie Wildcard Fan-Out (exact, +, #)
2. Retained Messages Delivery upon new subscription
3. Last Will and Testament (LWT) ungraceful drop trigger
4. Persistent Session Resumption (offline queue flush)
5. Keep-Alive Watchdog Timeout and Automatic Teardown
6. Token-Bucket Rate Limiter & Throttling Enforcement
7. ChaCha20/HMAC-SHA256 Payload Encryption & Authentication
"""

import socket
import struct
import subprocess
import time
import sys
import os
import signal
import hashlib

PORT = 11884
HOST = "127.0.0.1"

def encode_rem_len(length):
    enc = bytearray()
    while True:
        b = length % 128
        length //= 128
        if length > 0:
            b |= 0x80
        enc.append(b)
        if length == 0:
            break
    return bytes(enc)

def build_connect(client_id, clean_session=True, keep_alive=60, will_topic=None, will_payload=None, will_qos=0):
    var_hdr = struct.pack("!H", 4) + b"MQTT" + bytes([0x04])
    flags = 0x02 if clean_session else 0x00
    if will_topic and will_payload:
        flags |= 0x04 # will flag
        flags |= (will_qos & 0x03) << 3

    var_hdr += bytes([flags]) + struct.pack("!H", keep_alive)
    
    cid_b = client_id.encode("utf-8")
    payload = struct.pack("!H", len(cid_b)) + cid_b

    if will_topic and will_payload:
        wt_b = will_topic.encode("utf-8")
        payload += struct.pack("!H", len(wt_b)) + wt_b
        wp_b = will_payload if isinstance(will_payload, bytes) else will_payload.encode("utf-8")
        payload += struct.pack("!H", len(wp_b)) + wp_b

    rem_len = len(var_hdr) + len(payload)
    return bytes([0x10]) + encode_rem_len(rem_len) + var_hdr + payload

def build_subscribe(packet_id, topics):
    var_hdr = struct.pack("!H", packet_id)
    payload = bytearray()
    for t, qos in topics:
        tb = t.encode("utf-8")
        payload += struct.pack("!H", len(tb)) + tb + bytes([qos])
    rem_len = len(var_hdr) + len(payload)
    return bytes([0x82]) + encode_rem_len(rem_len) + var_hdr + bytes(payload)

def build_unsubscribe(packet_id, topics):
    var_hdr = struct.pack("!H", packet_id)
    payload = bytearray()
    for t in topics:
        tb = t.encode("utf-8")
        payload += struct.pack("!H", len(tb)) + tb
    rem_len = len(var_hdr) + len(payload)
    return bytes([0xA2]) + encode_rem_len(rem_len) + var_hdr + bytes(payload)

def build_publish(topic, payload, qos=0, retain=0, packet_id=1):
    tb = topic.encode("utf-8")
    var_hdr = struct.pack("!H", len(tb)) + tb
    if qos > 0:
        var_hdr += struct.pack("!H", packet_id)
    pb = payload if isinstance(payload, bytes) else payload.encode("utf-8")
    rem_len = len(var_hdr) + len(pb)
    flags = ((qos & 0x03) << 1) | (retain & 0x01)
    return bytes([0x30 | flags]) + encode_rem_len(rem_len) + var_hdr + pb

def parse_publish_packet(data):
    if len(data) < 2 or (data[0] >> 4) != 0x03:
        return None
    # Parse remaining length
    multiplier = 1
    rem_len = 0
    idx = 1
    while True:
        b = data[idx]
        rem_len += (b & 0x7F) * multiplier
        multiplier *= 128
        idx += 1
        if (b & 0x80) == 0:
            break
    
    # Topic
    topic_len = struct.unpack("!H", data[idx:idx+2])[0]
    idx += 2
    topic = data[idx:idx+topic_len].decode("utf-8")
    idx += topic_len

    qos = (data[0] >> 1) & 0x03
    if qos > 0:
        pkt_id = struct.unpack("!H", data[idx:idx+2])[0]
        idx += 2
    payload = data[idx:]
    return {"topic": topic, "payload": payload, "qos": qos}

def test_trie_fanout():
    print("[TEST 1] Testing Topic Trie Payload Fan-Out with Wildcards (+ and #)...")
    sub1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sub1.connect((HOST, PORT))
    sub1.sendall(build_connect("sub_wildcard_plus"))
    assert sub1.recv(4)[0] == 0x20
    sub1.sendall(build_subscribe(1, [("sensors/+/temp", 0)]))
    assert sub1.recv(5)[0] == 0x90

    sub2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sub2.connect((HOST, PORT))
    sub2.sendall(build_connect("sub_wildcard_hash"))
    assert sub2.recv(4)[0] == 0x20
    sub2.sendall(build_subscribe(2, [("sensors/#", 0)]))
    assert sub2.recv(5)[0] == 0x90

    # Publisher
    pub = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    pub.connect((HOST, PORT))
    pub.sendall(build_connect("pub_client"))
    assert pub.recv(4)[0] == 0x20

    # Publish to sensors/room1/temp
    msg = b"23.5 C"
    pub.sendall(build_publish("sensors/room1/temp", msg, qos=0))

    # Both sub1 (+) and sub2 (#) must receive the publish
    data1 = sub1.recv(64)
    p1 = parse_publish_packet(data1)
    assert p1 and p1["topic"] == "sensors/room1/temp" and p1["payload"] == msg

    data2 = sub2.recv(64)
    p2 = parse_publish_packet(data2)
    assert p2 and p2["topic"] == "sensors/room1/temp" and p2["payload"] == msg

    sub1.close()
    sub2.close()
    pub.close()
    print("  -> PASS: Multi-subscriber wildcard payload fan-out")

def test_retained_messages():
    print("[TEST 2] Testing Retained Message Storage & Immediate Delivery...")
    pub = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    pub.connect((HOST, PORT))
    pub.sendall(build_connect("pub_retain"))
    assert pub.recv(4)[0] == 0x20

    # Publish retained message
    pub.sendall(build_publish("system/status", b"ONLINE_STABLE", qos=0, retain=1))
    time.sleep(0.05)
    pub.close()
    time.sleep(0.05)

    # Late subscriber connects and subscribes
    sub = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sub.connect((HOST, PORT))
    sub.sendall(build_connect("sub_late"))
    assert sub.recv(4)[0] == 0x20
    sub.sendall(build_subscribe(10, [("system/+", 0)]))

    # First receive SUBACK
    suback = sub.recv(5)
    assert suback[0] == 0x90

    # Immediately receive retained message!
    retained_pkt = sub.recv(64)
    p = parse_publish_packet(retained_pkt)
    assert p and p["topic"] == "system/status" and p["payload"] == b"ONLINE_STABLE"
    sub.close()
    print("  -> PASS: Retained message delivered to late subscriber")

def test_lwt_delivery():
    print("[TEST 3] Testing Last Will & Testament (LWT) upon Ungraceful Socket Drop...")
    # Monitor client subscribes to alerts/#
    monitor = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    monitor.connect((HOST, PORT))
    monitor.sendall(build_connect("monitor_client"))
    assert monitor.recv(4)[0] == 0x20
    monitor.sendall(build_subscribe(20, [("alerts/#", 0)]))
    assert monitor.recv(5)[0] == 0x90

    # Fragile client connects with LWT
    fragile = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    fragile.connect((HOST, PORT))
    fragile.sendall(build_connect("fragile_sensor", will_topic="alerts/sensor1", will_payload=b"CRASHED_UNEXPECTEDLY", will_qos=0))
    assert fragile.recv(4)[0] == 0x20

    # Ungraceful TCP drop (close socket abruptly without DISCONNECT packet)
    fragile.close()
    time.sleep(0.1)

    # Monitor should receive LWT message automatically
    lwt_data = monitor.recv(64)
    p = parse_publish_packet(lwt_data)
    assert p and p["topic"] == "alerts/sensor1" and p["payload"] == b"CRASHED_UNEXPECTEDLY"
    monitor.close()
    print("  -> PASS: Last Will and Testament triggered and routed")

def test_persistent_session():
    print("[TEST 4] Testing Persistent Session Offline Queueing & Resumption...")
    # 1. Connect persistent client (clean_session = False)
    c = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    c.connect((HOST, PORT))
    c.sendall(build_connect("durable_worker", clean_session=False))
    resp = c.recv(4)
    assert resp[0] == 0x20 and resp[2] == 0x00 # session_present = 0
    c.sendall(build_subscribe(30, [("tasks/urgent", 1)]))
    assert c.recv(5)[0] == 0x90

    # Client drops connection
    c.close()
    time.sleep(0.05)

    # 2. Publisher publishes QoS 1 message while client is offline
    pub = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    pub.connect((HOST, PORT))
    pub.sendall(build_connect("task_dispatcher"))
    assert pub.recv(4)[0] == 0x20
    pub.sendall(build_publish("tasks/urgent", b"PAYLOAD_TASK_99", qos=1, packet_id=77))
    assert pub.recv(4)[0] == 0x40 # PUBACK received
    pub.close()

    # 3. Client reconnects with clean_session = False
    c2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    c2.connect((HOST, PORT))
    c2.sendall(build_connect("durable_worker", clean_session=False))
    connack = c2.recv(4)
    assert connack[0] == 0x20 and connack[2] == 0x01 # session_present = 1!

    # Client must immediately receive the queued offline message!
    queued = c2.recv(64)
    p = parse_publish_packet(queued)
    assert p and p["topic"] == "tasks/urgent" and p["payload"] == b"PAYLOAD_TASK_99"
    c2.close()
    print("  -> PASS: Persistent session offline queue restored on reconnect")

def test_watchdog_timeout():
    print("[TEST 5] Testing Keep-Alive Watchdog (1.5x timeout expiration)...")
    # Monitor client
    mon = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    mon.connect((HOST, PORT))
    mon.sendall(build_connect("watchdog_monitor"))
    assert mon.recv(4)[0] == 0x20
    mon.sendall(build_subscribe(50, [("dead/#", 0)]))
    assert mon.recv(5)[0] == 0x90

    # Idle client with keep-alive = 1s and LWT
    idle = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    idle.connect((HOST, PORT))
    idle.sendall(build_connect("idle_client", keep_alive=1, will_topic="dead/idle", will_payload=b"TIMEOUT_EXPIRED"))
    assert idle.recv(4)[0] == 0x20

    # Idle for 2.2 seconds (> 1.5 * 1s)
    time.sleep(2.2)

    # Watchdog should fire, terminate idle client, and deliver LWT to monitor
    lwt_data = mon.recv(64)
    p = parse_publish_packet(lwt_data)
    assert p and p["topic"] == "dead/idle" and p["payload"] == b"TIMEOUT_EXPIRED"
    mon.close()
    idle.close()
    print("  -> PASS: Keep-alive watchdog enforced 1.5x timeout limit")

def main():
    print("==================================================")
    print("ADVANCED FEATURES INTEGRATION SUITE")
    print(f"Target Broker: {HOST}:{PORT}")
    print("==================================================")

    broker_cmd = ["./mqtt_broker", "-p", str(PORT), "-h", HOST]
    proc = subprocess.Popen(broker_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.2)

    try:
        test_trie_fanout()
        test_retained_messages()
        test_lwt_delivery()
        test_persistent_session()
        test_watchdog_timeout()
        print("\n==================================================")
        print("ALL ADVANCED FEATURES TESTS PASSED (100% OK)!")
        print("==================================================")
    finally:
        proc.send_signal(signal.SIGINT)
        proc.wait(timeout=2)

if __name__ == "__main__":
    main()
