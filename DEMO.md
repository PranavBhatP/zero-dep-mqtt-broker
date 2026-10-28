# Complete Live Demonstration & Simulation Guide: Zero-Dependency MQTT Broker

**Course:** IT365 Advanced Computer Networks  
**Institution:** National Institute of Technology Karnataka (NITK), Surathkal  
**Project:** Zero-Dependency MQTT Broker (OASIS MQTT 3.1.1 Full Production Engine)  
**Authors:**
- **Pranav Bhat** (231IT049)
- **Pranav Vinodh** (231IT050)
- **Rijul Raman** (231IT056)

---

## 1. Overview

The **Zero-Dependency MQTT Broker** is an OASIS MQTT 3.1.1 compliant message broker developed entirely in standard C (C11) from first principles. It relies strictly on standard Linux kernel socket primitives (`sys/socket.h`, `sys/epoll.h`) with **zero external libraries**.

### Full Feature Set
1. **Non-blocking Event-Driven Reactor:** Linux `epoll` level-triggered event loop handling high concurrency (C10K capable).
2. **Dynamic Stream Defragmentation Buffer:** Linear accumulation buffer (`byte_buffer_t`) with compaction sliding that reliably frames variable-length MQTT packets under arbitrary network fragmentation.
3. **Bitwise Protocol Decoder & Encoder:** Strict OASIS MQTT 3.1.1 byte-level parser and binary wire serializer.
4. **Hierarchical Topic Radix Tree (Topic Trie):** Multi-way prefix tree delivering $O(K)$ topic resolution and wildcard matching (`+` single-level, `#` multi-level) with subscriber deduplication (MQTT-3.3.5-1).
5. **Stateful Session Store & Persistence:** Session lifecycle management, clean vs. persistent sessions (`clean_session == 0`), and offline QoS 1 queues.
6. **Reliability & Watchdogs:** Keep-alive heartbeat monitoring with $1.5 \times$ timeout disconnects, and automatic Last Will and Testament (LWT) message publishing on ungraceful drops.
7. **Retained Messages:** In-memory store for retain messages delivered immediately to new matching subscribers.
8. **Network Traffic Shaping & Throttling:** Token-bucket rate limiter limiting both packets/sec and bandwidth bytes/sec.
9. **Payload Cryptography:** Zero-dependency software ChaCha20 stream cipher with HMAC-SHA256 authenticated encryption.
10. **Live Telemetry & Diagnostics:** Real-time uptime, throughput, packet counts, and traffic statistics.

---

## 2. Quick Start Commands

```bash
# 1. Compile the broker and all test suites
make

# 2. Run the complete automated test verification suite
make test

# 3. Start the broker
./mqtt_broker -p 1883

# 4. Launch the Interactive Demo Menu
make demo

# 5. Launch the Realistic Multi-Device IoT Traffic Simulation
make sim
```

---

## 3. Side-by-Side Live Evaluation Guide

For presentations, vivas, and lab evaluations, open **two terminal windows side-by-side**.

### Terminal 1: Launch Broker with Features Enabled
```bash
# Start broker on port 1883 with verbose debug logging:
./mqtt_broker -p 1883 -d

# (Optional) With rate limiting and bandwidth throttling enabled:
./mqtt_broker -p 1883 -d -r 50 -b 10 -m 1048576

# (Optional) With ChaCha20/HMAC payload encryption enabled:
./mqtt_broker -p 1883 -d -e -k "my-super-secret-iot-network-key"
```

### Terminal 2: Run Demonstrations and Simulations

#### Mode A: Interactive Demonstration Menu (`make demo`)
Run `python3 scripts/demo.py -p 1883` to launch the interactive menu:
```text
===============================================================
      ZERO-DEPENDENCY MQTT BROKER - INTERACTIVE DEMO MENU      
===============================================================
 Target Broker: 127.0.0.1:1883
 1. Run Complete Protocol Walkthrough (CONNECT, PING, PUBLISH, SUBSCRIBE)
 2. Run Topic Trie Wildcard Routing Demo (+ and # fan-out)
 3. Run TCP Stream Fragmentation Demo (Byte-by-Byte slow trickle)
 4. Run High-Concurrency Load Demo (Simultaneous epoll connections)
 5. Run Protocol Violation & Security Rejection Demo
 6. Run All Demos Sequentially
 7. Launch Realistic Multi-Device IoT Traffic Simulation
 8. Exit
---------------------------------------------------------------
```

#### Mode B: Realistic Multi-Device IoT Traffic Simulator (`make sim`)
Run `python3 scripts/iot_simulation.py -p 1883 -t 15` to simulate real-world municipal & industrial IoT network traffic:
- **Central Municipal Control Dashboard:** Subscribes to multi-level wildcards (`city/#`, `grid/#`, `alerts/#`).
- **4 Environmental Weather Stations:** Publishes real-time temperature, humidity, and atmospheric telemetry to `city/zone<N>/weather`.
- **3 Industrial Power Grid Substations:** Publishes 3-phase Voltage, Current, and Frequency telemetry to `grid/substation<N>/power`.
- **3 GPS Fleet Logistics Trackers:** Publishes moving coordinates, speeds, and vehicle diagnostics to `city/fleet/truck<N>/gps`.
- **2 Security Perimeter Cameras:** Configured with **Last Will & Testament (LWT)** failover on `alerts/security/cam<N>`. The simulation simulates power/hardware failure by abruptly terminating sockets, and the broker immediately fires the stored LWT alert message to the Control Dashboard!

---

## 4. Architectural Verification Matrix

All modules are verified with automated unit tests and integration suites:

| Module / Component | Verification Command | Scope Verified | Pass Rate |
| :--- | :--- | :--- | :--- |
| **Stream Accumulation Buffer** | `./build/test_buffer` | Geometric doubling, linear compaction, 512KB safety bounds | **100%** |
| **Bitwise Protocol Parser** | `./build/test_parser` | Variable-byte integers (1-4B), Table 2.2 flags, framing | **100%** |
| **Topic Radix Tree (Trie)** | `./build/test_trie` | Exact match, `+` single wildcard, `#` multi-level wildcard, deduplication | **100%** |
| **Session Registry** | `./build/test_session` | Clean vs. persistent sessions, offline QoS 1 queues | **100%** |
| **Token-Bucket Throttling** | `./build/test_throttle` | Burst capacity, token replenishment, packet & byte rate limits | **100%** |
| **Payload Cryptography** | `./build/test_crypto` | SHA-256 vectors, ChaCha20 stream XOR, HMAC-SHA256 tampering check | **100%** |
| **Core Network Integration** | `python3 tests/test_server_e2e.py` | Real TCP handshakes, heartbeats, QoS 1 PUBACK, 30 concurrent sockets | **100%** |
| **Advanced Multi-Feature Suite** | `python3 tests/test_advanced_e2e.py` | Trie fan-out, retain message delivery, LWT on crash, keep-alive watchdog | **100%** |
| **IoT Traffic Simulator** | `python3 scripts/iot_simulation.py` | Multi-device simulated smart city traffic and dashboard routing | **100%** |
