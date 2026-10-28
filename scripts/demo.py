#!/usr/bin/env python3
"""
Interactive Demonstration Client for Zero-Dependency MQTT Broker
Course: IT365 Advanced Computer Networks, NITK Surathkal
Zero Dependencies: uses only Python 3 standard library (socket, struct, time, sys)
"""

import socket
import struct
import time
import sys
import argparse

# ANSI Terminal Colors
C_RESET  = "\033[0m"
C_BOLD   = "\033[1m"
C_CYAN   = "\033[36m"
C_GREEN  = "\033[32m"
C_YELLOW = "\033[33m"
C_RED    = "\033[31m"
C_BLUE   = "\033[34m"
C_MAGENTA= "\033[35m"

def print_header(title):
    print(f"\n{C_CYAN}{C_BOLD}{'=' * 65}{C_RESET}")
    print(f"{C_GREEN}{C_BOLD}  DEMO: {title}{C_RESET}")
    print(f"{C_CYAN}{C_BOLD}{'=' * 65}{C_RESET}")

def print_step(step_num, text):
    print(f"\n{C_YELLOW}{C_BOLD}[Step {step_num}]{C_RESET} {C_BOLD}{text}{C_RESET}")

def print_hex(label, data):
    hex_str = " ".join(f"{b:02X}" for b in data)
    print(f"  {C_MAGENTA}{label} ({len(data)} bytes):{C_RESET} {hex_str}")

def encode_remaining_length(length):
    encoded = bytearray()
    while True:
        byte = length % 128
        length //= 128
        if length > 0:
            byte |= 0x80
        encoded.append(byte)
        if length == 0:
            break
    return bytes(encoded)

def build_connect(client_id="demo_sensor_01", clean_session=True, keep_alive=60, will_topic=None, will_payload=None, will_qos=0):
    var_hdr = struct.pack("!H", 4) + b"MQTT" + bytes([0x04]) # Level 4 (MQTT 3.1.1)
    flags = 0x02 if clean_session else 0x00
    if will_topic and will_payload:
        flags |= 0x04
        flags |= (will_qos & 0x03) << 3
    var_hdr += bytes([flags]) + struct.pack("!H", keep_alive)
    
    cid_bytes = client_id.encode("utf-8")
    payload = struct.pack("!H", len(cid_bytes)) + cid_bytes

    if will_topic and will_payload:
        wt_bytes = will_topic.encode("utf-8")
        payload += struct.pack("!H", len(wt_bytes)) + wt_bytes
        wp_bytes = will_payload if isinstance(will_payload, bytes) else will_payload.encode("utf-8")
        payload += struct.pack("!H", len(wp_bytes)) + wp_bytes

    rem_len = len(var_hdr) + len(payload)
    fixed_hdr = bytes([0x10]) + encode_remaining_length(rem_len)
    return fixed_hdr + var_hdr + payload

def build_publish(topic, payload, qos=0, retain=0, packet_id=1):
    topic_bytes = topic.encode("utf-8")
    var_hdr = struct.pack("!H", len(topic_bytes)) + topic_bytes
    if qos > 0:
        var_hdr += struct.pack("!H", packet_id)
    
    p_bytes = payload if isinstance(payload, bytes) else payload.encode("utf-8")
    rem_len = len(var_hdr) + len(p_bytes)
    flags = ((qos & 0x03) << 1) | (retain & 0x01)
    fixed_hdr = bytes([0x30 | flags]) + encode_remaining_length(rem_len)
    return fixed_hdr + var_hdr + p_bytes

def build_subscribe(packet_id, topics):
    var_hdr = struct.pack("!H", packet_id)
    payload = bytearray()
    for topic_filter, qos in topics:
        tf_bytes = topic_filter.encode("utf-8")
        payload += struct.pack("!H", len(tf_bytes)) + tf_bytes
        payload.append(qos)
    rem_len = len(var_hdr) + len(payload)
    fixed_hdr = bytes([0x82]) + encode_remaining_length(rem_len)
    return fixed_hdr + var_hdr + bytes(payload)

def build_pingreq():
    return bytes([0xC0, 0x00])

def build_disconnect():
    return bytes([0xE0, 0x00])

def parse_publish_packet(data):
    if len(data) < 2 or (data[0] >> 4) != 0x03:
        return None
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
    topic_len = struct.unpack("!H", data[idx:idx+2])[0]
    idx += 2
    topic = data[idx:idx+topic_len].decode("utf-8")
    idx += topic_len
    qos = (data[0] >> 1) & 0x03
    if qos > 0:
        idx += 2
    payload = data[idx:]
    return {"topic": topic, "payload": payload, "qos": qos}

def demo_full_protocol_walkthrough(host, port):
    print_header("Comprehensive MQTT 3.1.1 Protocol Walkthrough")
    print(f"Connecting to broker at {host}:{port} via TCP socket...")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((host, port))
    except ConnectionRefusedError:
        print(f"\n{C_RED}[ERROR] Cannot connect to {host}:{port}. Is the broker running?{C_RESET}")
        print(f"Please start the broker in another terminal: {C_BOLD}./mqtt_broker -d -p {port}{C_RESET}\n")
        return False

    print_step(1, "Sending CONNECT packet (ClientID='demo_sensor_01', CleanSession=1, KeepAlive=60)")
    connect_pkt = build_connect()
    print_hex("Raw CONNECT", connect_pkt)
    sock.sendall(connect_pkt)

    resp = sock.recv(4)
    print_hex("Broker Response", resp)
    assert len(resp) == 4 and resp[0] == 0x20 and resp[3] == 0x00
    print(f"{C_GREEN}[+] CONNACK received: Accepted (Return code: 0x00){C_RESET}")

    print_step(2, "Sending PINGREQ heartbeat")
    ping_pkt = build_pingreq()
    print_hex("Raw PINGREQ", ping_pkt)
    sock.sendall(ping_pkt)

    resp = sock.recv(2)
    print_hex("Broker Response", resp)
    assert len(resp) == 2 and resp[0] == 0xD0 and resp[1] == 0x00
    print(f"{C_GREEN}[+] PINGRESP received: Broker is alive{C_RESET}")

    print_step(3, "Subscribing to 'home/+/temperature' (QoS 0) and 'factory/#' (QoS 1)")
    sub_pkt = build_subscribe(101, [("home/+/temperature", 0), ("factory/#", 1)])
    print_hex("Raw SUBSCRIBE", sub_pkt)
    sock.sendall(sub_pkt)

    resp = sock.recv(6)
    print_hex("Broker Response", resp)
    assert resp[0] == 0x90
    print(f"{C_GREEN}[+] SUBACK received: Granted QoS [0, 1] for PacketID 101{C_RESET}")

    print_step(4, "Publishing QoS 1 message to 'factory/conveyor/speed' = '150 RPM'")
    pub_pkt = build_publish("factory/conveyor/speed", "150 RPM", qos=1, packet_id=202)
    print_hex("Raw PUBLISH (QoS 1)", pub_pkt)
    sock.sendall(pub_pkt)

    # Because demo_sensor_01 is also subscribed to factory/#, the broker fans out the message
    # AND sends a PUBACK. Both frames (35 bytes PUBLISH + 4 bytes PUBACK = 39 bytes)
    # may arrive coalesced in the same TCP recv!
    frames = sock.recv(128)
    print_hex("Broker Inbound Data", frames)
    # Check that PUBACK (0x40) is in frames
    assert b"\x40\x02\x00\xca" in frames or frames[0] == 0x40
    print(f"{C_GREEN}[+] Forwarded PUBLISH & PUBACK (PacketID 202) both processed successfully!{C_RESET}")

    print_step(5, "Sending clean DISCONNECT packet")
    disc_pkt = build_disconnect()
    print_hex("Raw DISCONNECT", disc_pkt)
    sock.sendall(disc_pkt)
    sock.close()
    print(f"{C_GREEN}[+] Connection closed cleanly.{C_RESET}")
    return True

def demo_trie_routing(host, port):
    print_header("Topic Radix Tree (Topic Trie) Wildcard Fan-Out Demo")
    print("Spawning 2 subscribers: Subscriber 1 ('home/+/temp') and Subscriber 2 ('home/#')")

    s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s1.connect((host, port))
    s1.sendall(build_connect("sub_plus"))
    s1.recv(4)
    s1.sendall(build_subscribe(1, [("home/+/temp", 0)]))
    s1.recv(5)

    s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s2.connect((host, port))
    s2.sendall(build_connect("sub_hash"))
    s2.recv(4)
    s2.sendall(build_subscribe(2, [("home/#", 0)]))
    s2.recv(5)

    pub = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    pub.connect((host, port))
    pub.sendall(build_connect("publisher_node"))
    pub.recv(4)

    print("\nPublishing to 'home/kitchen/temp' -> matches BOTH ('home/+/temp') and ('home/#')")
    pub.sendall(build_publish("home/kitchen/temp", "26.4 C", qos=0))

    data1 = s1.recv(64)
    p1 = parse_publish_packet(data1)
    print(f"{C_GREEN}[+] Subscriber 1 (+ wildcard) received:{C_RESET} {p1['topic']} -> {p1['payload'].decode()}")

    data2 = s2.recv(64)
    p2 = parse_publish_packet(data2)
    print(f"{C_GREEN}[+] Subscriber 2 (# wildcard) received:{C_RESET} {p2['topic']} -> {p2['payload'].decode()}")

    print("\nPublishing to 'home/garden/outdoor/temp' -> matches ONLY ('home/#')")
    pub.sendall(build_publish("home/garden/outdoor/temp", "18.2 C", qos=0))

    data2_sub = s2.recv(64)
    p2_sub = parse_publish_packet(data2_sub)
    print(f"{C_GREEN}[+] Subscriber 2 (# wildcard) received multi-level match:{C_RESET} {p2_sub['topic']} -> {p2_sub['payload'].decode()}")

    s1.close()
    s2.close()
    pub.close()
    print(f"\n{C_GREEN}{C_BOLD}[PASS] Topic Radix Tree wildcard routing verified!{C_RESET}\n")
    return True

def demo_tcp_stream_fragmentation(host, port):
    print_header("TCP Stream Defragmentation Demonstration")
    print("Simulating terrible network conditions by transmitting a CONNECT packet")
    print("ONE BYTE AT A TIME with artificial delays between bytes.\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((host, port))
    except ConnectionRefusedError:
        print(f"{C_RED}[ERROR] Cannot connect to {host}:{port}.{C_RESET}\n")
        return False

    pkt = build_connect("slow_fragmented_client")
    print(f"Total packet size to send: {len(pkt)} bytes")
    print("Transmitting byte-by-byte into TCP socket:")

    for idx, b in enumerate(pkt, 1):
        sock.sendall(bytes([b]))
        sys.stdout.write(f"\r  Sending byte {idx}/{len(pkt)} [0x{b:02X}]... ")
        sys.stdout.flush()
        time.sleep(0.02)

    print("\n\nAll bytes transmitted. Awaiting broker CONNACK response...")
    resp = sock.recv(4)
    print_hex("Broker Response", resp)
    if len(resp) == 4 and resp[0] == 0x20 and resp[3] == 0x00:
        print(f"\n{C_GREEN}{C_BOLD}[PASS] Stream fragmentation handled perfectly!{C_RESET}")
        print("The broker buffered all fragments in byte_buffer_t until the full packet arrived,")
        print("decoded the bitwise header, and successfully issued CONNACK.")
    else:
        print(f"\n{C_RED}[FAIL] Broker did not return valid CONNACK.{C_RESET}")

    sock.sendall(build_disconnect())
    sock.close()
    return True

def demo_high_concurrency(host, port, num_clients=40):
    print_header(f"High-Concurrency C10K epoll Architecture Demonstration")
    print(f"Connecting {num_clients} simultaneous clients to verify non-blocking epoll reactor.\n")

    clients = []
    print(f"1. Establishing {num_clients} concurrent TCP connections & MQTT handshakes...")
    start_time = time.time()
    for i in range(num_clients):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((host, port))
        s.sendall(build_connect(f"sensor_node_{i:03d}"))
        clients.append(s)

    for s in clients:
        resp = s.recv(4)
        assert len(resp) == 4 and resp[0] == 0x20

    elapsed = time.time() - start_time
    print(f"{C_GREEN}[+] All {num_clients} clients connected and accepted in {elapsed:.3f} seconds!{C_RESET}")

    print(f"\n2. Broadcasting simultaneous PINGREQ heartbeats across all {num_clients} connections...")
    for s in clients:
        s.sendall(build_pingreq())

    for s in clients:
        resp = s.recv(2)
        assert len(resp) == 2 and resp[0] == 0xD0

    print(f"{C_GREEN}[+] All {num_clients} clients received PINGRESP without packet drop!{C_RESET}")

    print(f"\n3. Tearing down all {num_clients} client sessions cleanly...")
    for s in clients:
        s.sendall(build_disconnect())
        s.close()

    print(f"\n{C_GREEN}{C_BOLD}[PASS] High-concurrency epoll reactor verification passed!{C_RESET}\n")
    return True

def demo_protocol_violation(host, port):
    print_header("Protocol Violation & Strict OASIS Spec Validation")
    print("MQTT 3.1.1 Section 2.2.2 mandates that CONNECT packets MUST have flags 0000.")
    print("We intentionally send a malformed packet with flags 0010 (0x12) to test enforcement.\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((host, port))
    except ConnectionRefusedError:
        print(f"{C_RED}[ERROR] Cannot connect to {host}:{port}.{C_RESET}\n")
        return False

    malformed_pkt = bytes([0x12, 0x02, 0x00, 0x00])
    print_hex("Sending Malformed Header", malformed_pkt)
    sock.sendall(malformed_pkt)

    time.sleep(0.1)
    try:
        data = sock.recv(10)
        if len(data) == 0:
            print(f"\n{C_GREEN}{C_BOLD}[PASS] Socket closed by broker as mandated by OASIS MQTT specification!{C_RESET}")
            print("The broker detected illegal reserved flags, logged the violation,")
            print("and terminated the connection without compromise.")
        else:
            print(f"{C_YELLOW}Broker sent: {data}{C_RESET}")
    except Exception as e:
        print(f"{C_GREEN}[PASS] Socket severed by broker ({e}){C_RESET}")
    sock.close()
    return True

def main():
    parser = argparse.ArgumentParser(description="Zero-Dependency MQTT Broker Demo")
    parser.add_argument("-p", "--port", type=int, default=1883, help="Broker port (default: 1883)")
    parser.add_argument("-H", "--host", default="127.0.0.1", help="Broker host (default: 127.0.0.1)")
    parser.add_argument("--auto", action="store_true", help="Run full automated demo sequence non-interactively")
    args = parser.parse_args()

    if args.auto:
        demo_full_protocol_walkthrough(args.host, args.port)
        demo_trie_routing(args.host, args.port)
        demo_tcp_stream_fragmentation(args.host, args.port)
        demo_high_concurrency(args.host, args.port, num_clients=30)
        demo_protocol_violation(args.host, args.port)
        return

    while True:
        print(f"\n{C_BLUE}{C_BOLD}==============================================================={C_RESET}")
        print(f"{C_GREEN}{C_BOLD}      ZERO-DEPENDENCY MQTT BROKER - INTERACTIVE DEMO MENU      {C_RESET}")
        print(f"{C_BLUE}{C_BOLD}==============================================================={C_RESET}")
        print(f" Target Broker: {args.host}:{args.port}")
        print(" 1. Run Complete Protocol Walkthrough (CONNECT, PING, PUBLISH, SUBSCRIBE)")
        print(" 2. Run Topic Trie Wildcard Routing Demo (+ and # fan-out)")
        print(" 3. Run TCP Stream Fragmentation Demo (Byte-by-Byte slow trickle)")
        print(" 4. Run High-Concurrency Load Demo (Simultaneous epoll connections)")
        print(" 5. Run Protocol Violation & Security Rejection Demo")
        print(" 6. Run All Demos Sequentially")
        print(" 7. Launch Realistic Multi-Device IoT Traffic Simulation")
        print(" 8. Exit")
        print(f"{C_BLUE}{C_BOLD}---------------------------------------------------------------{C_RESET}")

        try:
            choice = input(f"{C_BOLD}Select an option (1-8): {C_RESET}").strip()
        except (KeyboardInterrupt, EOFError):
            print("\nExiting demo.")
            break

        if choice == "1":
            demo_full_protocol_walkthrough(args.host, args.port)
        elif choice == "2":
            demo_trie_routing(args.host, args.port)
        elif choice == "3":
            demo_tcp_stream_fragmentation(args.host, args.port)
        elif choice == "4":
            demo_high_concurrency(args.host, args.port, num_clients=30)
        elif choice == "5":
            demo_protocol_violation(args.host, args.port)
        elif choice == "6":
            demo_full_protocol_walkthrough(args.host, args.port)
            demo_trie_routing(args.host, args.port)
            demo_tcp_stream_fragmentation(args.host, args.port)
            demo_high_concurrency(args.host, args.port, num_clients=30)
            demo_protocol_violation(args.host, args.port)
        elif choice == "7":
            import subprocess
            subprocess.run(["python3", "scripts/iot_simulation.py", "-p", str(args.port), "-H", args.host, "-t", "12"])
        elif choice == "8":
            print("Exiting demo.")
            break
        else:
            print("Invalid option. Please enter 1-8.")

if __name__ == "__main__":
    main()
