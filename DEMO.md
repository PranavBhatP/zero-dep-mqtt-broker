# Live Demonstration Guide: Zero-Dependency MQTT Broker

**Course:** IT365 Advanced Computer Networks  
**Institution:** National Institute of Technology Karnataka (NITK), Surathkal  
**Project:** Zero-Dependency MQTT Broker (OASIS MQTT 3.1.1)  
**Authors:**
- **Pranav Bhat** (231IT049)
- **Pranav Vinodh** (231IT050)
- **Rijul Raman** (231IT056)

---

## 1. Overview

This guide provides instructions for demonstrating the **Zero-Dependency MQTT Broker** during lab evaluations, vivas, and project presentations.

The broker contains zero third-party library dependencies and executes natively using the Linux `epoll` reactor architecture and standard POSIX sockets (`<sys/socket.h>`).

---

## 2. Interactive Side-by-Side Demonstration (Recommended)

To showcase both server-side packet decoding and client-side wire communication, open **two terminal windows side-by-side**.

### Terminal 1: Launch Broker in Verbose Debug Mode
Run the broker with verbose logging (`-d`) enabled so the panel can observe live TCP state transitions, bitwise packet parsing, and binary wire hex dumps:

```bash
cd /mnt/shared/code/acn-course-project
./mqtt_broker -d -p 1883
```

**Expected Terminal 1 Output:**
```text
========================================================================
         ZERO-DEPENDENCY MQTT 3.1.1 BROKER (PHASE 1 FOUNDATION)         
========================================================================
 Course:    IT365 Advanced Computer Networks
 Institute: National Institute of Technology Karnataka (NITK), Surathkal
 Team:     Pranav Bhat (231IT049)
           Pranav Vinodh (231IT050)
           Rijul Raman (231IT056)
------------------------------------------------------------------------
 Architecture: Non-blocking Event-Driven TCP Server (Linux epoll)
 Parser:       Strict Bitwise OASIS MQTT 3.1.1 Packet Decoder
 Listening on: 0.0.0.0:1883
========================================================================

[2026-10-08 00:20:00] [INFO ] Non-blocking TCP server initialized on 0.0.0.0:1883 (epoll_fd: 4, listen_fd: 3)
[2026-10-08 00:20:00] [INFO ] Entering epoll event loop. Ready to accept MQTT connections...
```

---

### Terminal 2: Run the Interactive Demo Client

In your second terminal window, launch the interactive demonstration utility:

```bash
cd /mnt/shared/code/acn-course-project
make demo
```
*(Or directly: `python3 scripts/demo.py -p 1883`)*

You will see the interactive demo menu:

```text
===============================================================
      ZERO-DEPENDENCY MQTT BROKER - INTERACTIVE DEMO MENU      
===============================================================
 Target Broker: 127.0.0.1:1883
 1. Run Complete Protocol Walkthrough (CONNECT, PING, PUBLISH, SUBSCRIBE)
 2. Run TCP Stream Fragmentation Demo (Byte-by-Byte slow trickle)
 3. Run High-Concurrency Load Demo (Simultaneous epoll connections)
 4. Run Protocol Violation & Security Rejection Demo
 5. Run All Demos Sequentially
 6. Exit
---------------------------------------------------------------
Select an option (1-6): 
```

---

## 3. Demonstration Scenarios & Explanations

### Scenario 1: Complete MQTT 3.1.1 Protocol Walkthrough (Menu Option 1)
- **What it does:** Demonstrates full wire-level packet serialization and deserialization across the protocol lifecycle.
- **Client Actions & Server Responses:**
  1. **`CONNECT` Handshake:** Client sends raw binary packet with `ClientID="nitk_weather_station"`, `CleanSession=1`, `KeepAlive=60s`. Server responds with `CONNACK` (`0x20 0x02 0x00 0x00` - Connection Accepted).
  2. **`PINGREQ` Heartbeat:** Client sends `0xC0 0x00`. Server responds with `PINGRESP` (`0xD0 0x00`).
  3. **`PUBLISH` QoS 0:** Client sends fire-and-forget payload `"31.5 C"` to topic `"surathkal/beach/temp"`.
  4. **`PUBLISH` QoS 1:** Client sends message with `PacketID=4096`. Server processes packet and issues `PUBACK` (`0x40 0x02 0x10 0x00`) acknowledging `PacketID 4096`.
  5. **`SUBSCRIBE` Request:** Client subscribes to `"nitk/dept/it/#"` (QoS 1) and `"campus/+/bus"` (QoS 0). Server returns `SUBACK` with matching `PacketID=101` and granted QoS array.
  6. **`DISCONNECT` Clean Teardown:** Client sends `0xE0 0x00` and socket closes gracefully.

---

### Scenario 2: TCP Stream Defragmentation Demonstration (Menu Option 2)
- **Why this is critical:** TCP is a continuous byte-stream without message boundaries. A naive parser waiting for full packets fails if network segments arrive split across packets.
- **How it works:**
  - The client transmits a complete 33-byte `CONNECT` packet **one single byte at a time** into the TCP stream, with a 20 millisecond sleep between bytes.
  - In Terminal 1, watch the broker buffer each byte into `byte_buffer_t`, signaling `MQTT_NEED_MORE_DATA`.
  - As soon as byte 33 arrives, the parser frames the packet, decodes the bitwise header, and issues `CONNACK`.

---

### Scenario 3: High-Concurrency C10K epoll Architecture (Menu Option 3)
- **Why this is critical:** Proves that the event-driven Linux `epoll` reactor handles many simultaneous IoT devices without thread spawning or lock contention.
- **How it works:**
  - Establishes **30 concurrent TCP client sockets** in parallel.
  - Sends 30 simultaneous `CONNECT` packets; all 30 handshakes complete in under **3 milliseconds**.
  - Broadcasts simultaneous `PINGREQ` heartbeats across all 30 sockets without packet loss.
  - Gracefully tears down all 30 sessions.

---

### Scenario 4: Protocol Violation & Security Enforcement (Menu Option 4)
- **Why this is critical:** Section 2.2.2 (Table 2.2) of the OASIS MQTT 3.1.1 specification mandates that reserved header flags MUST be strictly validated.
- **How it works:**
  - The client deliberately sends a corrupted fixed header with illegal reserved bits set (`0x12` instead of `0x10`).
  - The broker flags the protocol violation, logs the illegal bits, and immediately severs the TCP connection as mandated by the standard.

---

## 4. Automated One-Click Demonstration

To run all 4 demonstration scenarios sequentially without manual menu input:

```bash
# Start broker in background or separate terminal:
./mqtt_broker -p 1883 &
BROKER_PID=$!

# Run full automated test sequence:
make demo-auto

# Stop background broker when done:
kill $BROKER_PID
```

---

## 5. Verification & Unit Test Suite

To prove unit-level algorithmic correctness to the evaluation committee:

```bash
make test
```

This compiles and runs:
1. `build/test_buffer`: Dynamic stream buffer expansion, linear compaction via `memmove()`, and bounds safety.
2. `build/test_parser`: Variable-length integer encoding/decoding ($0$ to $268,435,455$), Table 2.2 flag validation, bitwise extraction, and pipelined packet parsing.
3. `tests/test_server_e2e.py`: End-to-end integration tests over loopback network sockets.

---

## 6. Raw Socket Manual Verification (Optional)

You can also send raw hexadecimal bytes using standard Unix utilities (`ncat` / `nc`) or Python:

```bash
# Send raw PINGREQ (0xC0 0x00) after connecting
python3 -c '
import socket
s = socket.socket()
s.connect(("127.0.0.1", 1883))
# Send CONNECT
s.sendall(bytes([0x10, 0x12, 0x00, 0x04, ord("M"), ord("Q"), ord("T"), ord("T"), 0x04, 0x02, 0x00, 0x3C, 0x00, 0x06, ord("m"), ord("a"), ord("n"), ord("u"), ord("a"), ord("l")]))
connack = s.recv(4)
print("CONNACK received:", [hex(b) for b in connack])
# Send PINGREQ
s.sendall(bytes([0xC0, 0x00]))
pingresp = s.recv(2)
print("PINGRESP received:", [hex(b) for b in pingresp])
s.close()
'
```
