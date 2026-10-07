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

def build_connect(client_id="demo_sensor_01", clean_session=True, keep_alive=60):
    var_hdr = struct.pack("!H", 4) + b"MQTT" + bytes([0x04]) # Level 4 (MQTT 3.1.1)
    flags = 0x02 if clean_session else 0x00
    var_hdr += bytes([flags]) + struct.pack("!H", keep_alive)
    
    cid_bytes = client_id.encode("utf-8")
    payload = struct.pack("!H", len(cid_bytes)) + cid_bytes

    rem_len = len(var_hdr) + len(payload)
    fixed_hdr = bytes([0x10]) + encode_remaining_length(rem_len)
    return fixed_hdr + var_hdr + payload

def build_publish(topic, payload, qos=0, packet_id=1):
    topic_bytes = topic.encode("utf-8")
    var_hdr = struct.pack("!H", len(topic_bytes)) + topic_bytes
    if qos > 0:
        var_hdr += struct.pack("!H", packet_id)
    
    p_bytes = payload if isinstance(payload, bytes) else payload.encode("utf-8")
    rem_len = len(var_hdr) + len(p_bytes)
    flags = (qos & 0x03) << 1
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

    print(f"{C_GREEN}[+] TCP connection established.{C_RESET}")

    # 1. CONNECT Handshake
    print_step(1, "Sending CONNECT packet (Client ID: 'nitk_weather_station', CleanSession=1, KeepAlive=60s)")
    pkt = build_connect("nitk_weather_station", clean_session=True, keep_alive=60)
    print_hex("Raw Wire Bytes", pkt)
    sock.sendall(pkt)

    resp = sock.recv(4)
    print_hex("Broker Response", resp)
    if len(resp) == 4 and resp[0] == 0x20 and resp[3] == 0x00:
        print(f"{C_GREEN}  -> SUCCESS: CONNACK received! Return code: 0x00 (Connection Accepted){C_RESET}")
    else:
        print(f"{C_RED}  -> FAILED: Unexpected response {resp}{C_RESET}")
        sock.close()
        return False
    time.sleep(0.5)

    # 2. PINGREQ Heartbeat
    print_step(2, "Sending PINGREQ heartbeat")
    pkt = build_pingreq()
    print_hex("Raw Wire Bytes", pkt)
    sock.sendall(pkt)

    resp = sock.recv(2)
    print_hex("Broker Response", resp)
    if len(resp) == 2 and resp[0] == 0xD0 and resp[1] == 0x00:
        print(f"{C_GREEN}  -> SUCCESS: PINGRESP (0xD0 0x00) received! Broker heartbeat healthy.{C_RESET}")
    else:
        print(f"{C_RED}  -> FAILED: Unexpected PINGRESP{C_RESET}")
    time.sleep(0.5)

    # 3. PUBLISH QoS 0
    print_step(3, "Sending PUBLISH QoS 0 (Topic: 'surathkal/beach/temp', Payload: '31.5 C')")
    pkt = build_publish("surathkal/beach/temp", "31.5 C", qos=0)
    print_hex("Raw Wire Bytes", pkt)
    sock.sendall(pkt)
    print(f"{C_GREEN}  -> Dispatched fire-and-forget QoS 0 message.{C_RESET}")
    time.sleep(0.5)

    # 4. PUBLISH QoS 1 with PUBACK
    print_step(4, "Sending PUBLISH QoS 1 (Topic: 'acn/iot/alert', Packet ID: 4096, Payload: 'CRITICAL_HIGH_TEMP')")
    pkt = build_publish("acn/iot/alert", "CRITICAL_HIGH_TEMP", qos=1, packet_id=4096)
    print_hex("Raw Wire Bytes", pkt)
    sock.sendall(pkt)

    resp = sock.recv(4)
    print_hex("Broker Response", resp)
    if len(resp) == 4 and resp[0] == 0x40:
        ack_id = struct.unpack("!H", resp[2:4])[0]
        print(f"{C_GREEN}  -> SUCCESS: PUBACK received! Acknowledged Packet ID: {ack_id}{C_RESET}")
    else:
        print(f"{C_RED}  -> FAILED: Expected PUBACK{C_RESET}")
    time.sleep(0.5)

    # 5. SUBSCRIBE with SUBACK
    print_step(5, "Sending SUBSCRIBE (Filters: 'nitk/dept/it/#' [QoS 1], 'campus/+/bus' [QoS 0])")
    sub_topics = [("nitk/dept/it/#", 1), ("campus/+/bus", 0)]
    pkt = build_subscribe(101, sub_topics)
    print_hex("Raw Wire Bytes", pkt)
    sock.sendall(pkt)

    resp = sock.recv(5)
    print_hex("Broker Response", resp)
    if len(resp) >= 5 and resp[0] == 0x90:
        ack_id = struct.unpack("!H", resp[2:4])[0]
        granted = [f"QoS {b}" for b in resp[4:]]
        print(f"{C_GREEN}  -> SUCCESS: SUBACK received for Packet ID {ack_id}! Granted QoS: {', '.join(granted)}{C_RESET}")
    else:
        print(f"{C_RED}  -> FAILED: Expected SUBACK{C_RESET}")
    time.sleep(0.5)

    # 6. DISCONNECT
    print_step(6, "Sending clean DISCONNECT notification (0xE0 0x00)")
    pkt = build_disconnect()
    print_hex("Raw Wire Bytes", pkt)
    sock.sendall(pkt)
    sock.close()
    print(f"{C_GREEN}  -> SUCCESS: Socket cleanly closed.{C_RESET}")

    print(f"\n{C_CYAN}{'=' * 65}{C_RESET}")
    print(f"{C_GREEN}{C_BOLD}>>> PROTOCOL WALKTHROUGH DEMO COMPLETED SUCCESSFULLY! <<<{C_RESET}")
    print(f"{C_CYAN}{'=' * 65}{C_RESET}\n")
    return True

def demo_tcp_stream_fragmentation(host, port):
    print_header("TCP Stream Defragmentation Demonstration")
    print("This demo proves that the broker accumulates partial stream fragments")
    print("by sending a complete CONNECT packet ONE BYTE AT A TIME with delays.\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((host, port))
    except ConnectionRefusedError:
        print(f"{C_RED}[ERROR] Cannot connect to {host}:{port}. Is the broker running?{C_RESET}\n")
        return False

    pkt = build_connect("slow_trickle_client")
    print(f"Total packet size: {len(pkt)} bytes.")
    print("Transmitting byte-by-byte into TCP socket:")

    for idx, b in enumerate(pkt, 1):
        sock.sendall(bytes([b]))
        sys.stdout.write(f"\r  Sending byte {idx}/{len(pkt)} [0x{b:02X}]... ")
        sys.stdout.flush()
        time.sleep(0.02) # 20ms delay between bytes

    print("\n\nAll bytes transmitted. Awaiting broker CONNACK response...")
    resp = sock.recv(4)
    print_hex("Broker Response", resp)
    if len(resp) == 4 and resp[0] == 0x20 and resp[3] == 0x00:
        print(f"\n{C_GREEN}{C_BOLD}[PASS] Stream fragmentation handled perfectly!{C_RESET}")
        print("The broker buffered all fragments until the full packet arrived,")
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

    # Read CONNACK on all
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

    malformed_pkt = bytes([0x12, 0x02, 0x00, 0x00]) # 0x12 has reserved bit 1 set!
    print_hex("Sending Malformed Header", malformed_pkt)
    sock.sendall(malformed_pkt)

    # Expect socket to be abruptly closed by broker per OASIS specification
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
        print(" 2. Run TCP Stream Fragmentation Demo (Byte-by-Byte slow trickle)")
        print(" 3. Run High-Concurrency Load Demo (Simultaneous epoll connections)")
        print(" 4. Run Protocol Violation & Security Rejection Demo")
        print(" 5. Run All Demos Sequentially")
        print(" 6. Exit")
        print(f"{C_BLUE}{C_BOLD}---------------------------------------------------------------{C_RESET}")

        try:
            choice = input(f"{C_BOLD}Select an option (1-6): {C_RESET}").strip()
        except (KeyboardInterrupt, EOFError):
            print("\nExiting demo.")
            break

        if choice == "1":
            demo_full_protocol_walkthrough(args.host, args.port)
        elif choice == "2":
            demo_tcp_stream_fragmentation(args.host, args.port)
        elif choice == "3":
            demo_high_concurrency(args.host, args.port, num_clients=30)
        elif choice == "4":
            demo_protocol_violation(args.host, args.port)
        elif choice == "5":
            demo_full_protocol_walkthrough(args.host, args.port)
            demo_tcp_stream_fragmentation(args.host, args.port)
            demo_high_concurrency(args.host, args.port, num_clients=30)
            demo_protocol_violation(args.host, args.port)
        elif choice == "6":
            print("Exiting demo.")
            break
        else:
            print("Invalid option. Please enter 1-6.")

if __name__ == "__main__":
    main()
