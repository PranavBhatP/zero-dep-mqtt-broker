# Zero-Dependency MQTT Broker

An ultra-lightweight, high-performance MQTT 3.1.1 message broker developed entirely from scratch in C (C11) using native Linux OS socket primitives, without external networking or protocol libraries.

**Course:** IT365 Advanced Computer Networks  
**Institution:** National Institute of Technology Karnataka (NITK), Surathkal  
**Authors:**
- **Pranav Bhat** (231IT049)
- **Pranav Vinodh** (231IT050)
- **Rijul Raman** (231IT056)

---

## Mid-Semester Evaluation Deliverables

- **Technical Report:** [`IT365_MidSem_Report.pdf`](IT365_MidSem_Report.pdf)  
  *IEEE Conference format (4 pages), detailing problem formulation, theoretical foundations, zero-dependency socket architecture, and Phase 1 verification methodology.*
- **Evaluation Presentation:** [`IT365_MidSem_Presentation.pdf`](IT365_MidSem_Presentation.pdf)  
  *17-slide LaTeX Beamer presentation (16:9 widescreen) covering project objectives, Linux epoll reactor, stream framing buffers, bitwise decoders, live verification matrices, and Phases 2 & 3 roadmap.*

---

## 1. Project Motivation & Philosophy

Most real-world IoT deployments rely on large, heavy third-party message brokers (e.g., Mosquitto, HiveMQ, EMQX) that carry extensive external dependency chains. The goal of this project is to implement an MQTT 3.1.1 compliant broker from first principles to deeply master and demonstrate core networking concepts:

1. **Zero External Dependencies:** Built solely on standard POSIX sockets (`<sys/socket.h>`), Linux event demultiplexing (`<sys/epoll.h>`), and standard C runtime (`libc`).
2. **Handling the C10K Problem:** Traditional blocking thread-per-connection architectures collapse under concurrency. We employ an event-driven Reactor pattern with Linux `epoll` for non-blocking I/O multiplexing.
3. **Application-Layer TCP Stream Defragmentation:** TCP is an unstructured byte-stream protocol. We implement stream accumulation and linear compaction buffers to reliably frame variable-length MQTT packets under arbitrary network fragmentation.
4. **Strict Bitwise Protocol Conformance:** Wire-level parsing of bit-shifted headers, variable-byte remaining length integers, and strict enforcement of the OASIS MQTT 3.1.1 specification.

---

## 2. System Architecture

```
                       +-----------------------------+
                       |     Linux Kernel Space      |
                       | TCP Sockets / epoll reactor |
                       +--------------+--------------+
                                      |
                      epoll_wait() level-triggered I/O
                                      |
                                      v
+---------------------------------------------------------------------------------+
|                       User Space MQTT Broker Daemon                             |
|                                                                                 |
|  +------------------------+      +--------------------+      +---------------+  |
|  |     server.[c/h]       | ---> |    throttle.[c/h]  | ---> |  buffer.[c/h] |  |
|  | - Master listen socket |      | - Token bucket     |      | - Dynamic rx  |  |
|  | - accept4() non-block  |      | - Packet & byte cap|      | - Defrag      |  |
|  +-----------+------------+      +--------------------+      +-------+-------+  |
|              |                                                       |          |
|              v                                                       v          |
|  +------------------------+      +--------------------+      +---------------+  |
|  |    telemetry.[c/h]     |      |     crypto.[c/h]   | <--- | mqtt_parser.c |  |
|  | - Uptime & throughput  |      | - ChaCha20 cipher  |      | - Wire decode |  |
|  | - Dropped & Tx/Rx stats|      | - HMAC-SHA256 MAC  |      | - Table 2.2   |  |
|  +------------------------+      +---------+----------+      +-------+-------+  |
|                                            |                         |          |
|                                            v                         v          |
|  +------------------------+      +--------------------+      +---------------+  |
|  |      session.[c/h]     | <--- |     trie.[c/h]     | <--- | mqtt_encoder.c|  |
|  | - Client state machine |      | - Topic Radix Tree |      | - Binary wire |  |
|  | - QoS 1 offline queue  |      | - Wildcards (+, #) |      | - Responses   |  |
|  | - Retained store link  |      | - O(K) fan-out     |      | (CONN/SUB/PUB)|  |
|  +------------------------+      +--------------------+      +---------------+  |
+---------------------------------------------------------------------------------+
```

### Core Components
- **Non-blocking TCP Server (`src/server.c`, `include/server.h`):**
  Uses `socket()`, `setsockopt(SO_REUSEADDR | SO_REUSEPORT)`, `fcntl(O_NONBLOCK)`, `setsockopt(TCP_NODELAY)`, and `epoll_create1()`. Disables Nagle's algorithm for low-latency transmission of small control packets.
- **TCP Stream Buffer (`src/buffer.c`, `include/buffer.h`):**
  Dynamic linear byte buffer with dynamic capacity doubling and `memmove()`-based compaction. Accommodates partial packet fragments and pipelines multiple packets in a single TCP read.
- **Bitwise Protocol Decoder (`src/mqtt_parser.c`, `include/mqtt_parser.h`):**
  Strict decoder for OASIS MQTT 3.1.1 control packets (`CONNECT`, `PUBLISH`, `SUBSCRIBE`, `UNSUBSCRIBE`, `PINGREQ`, `DISCONNECT`). Decodes variable-byte remaining lengths (1 to 4 bytes) and enforces Table 2.2 reserved flag rules.
- **Binary Response Serializer (`src/mqtt_encoder.c`, `include/mqtt_encoder.h`):**
  Constructs wire-format responses (`CONNACK`, `PINGRESP`, `SUBACK`, `PUBACK`).
- **Topic Radix Tree / Topic Trie (`src/trie.c`, `include/trie.h`):**
  Hierarchical prefix tree splitting topics on `/`. Resolves exact topics, single-level wildcards (`+`), and multi-level wildcards (`#`) in $O(K)$ time with subscriber deduplication (MQTT-3.3.5-1).
- **Session State Machine & Offline Queue (`src/session.c`, `include/session.h`):**
  Hash-indexed client session registry. Manages clean vs. persistent sessions (`clean_session == 0`), Last Will and Testament (LWT) parameters, and bounded offline FIFO queueing for QoS 1 messages with replay upon reconnection.
- **Retained Message Repository (`src/retained.c`, `include/retained.h`):**
  In-memory store retaining the latest payload per topic. Automatically delivers retained messages to matching new subscribers immediately following `SUBACK`. Payload length 0 clears retained messages.
- **Zero-Dependency Cryptography Engine (`src/crypto.c`, `include/crypto.h`):**
  Pure C implementation of the ChaCha20 stream cipher (RFC 8439), SHA-256 (RFC 6234), and HMAC-SHA256 authenticated payload encryption with constant-time MAC verification.
- **High-Resolution Traffic Shaper & Throttling (`src/throttle.c`, `include/throttle.h`):**
  Dual-criterion token-bucket rate limiter enforcing configurable packets-per-second and bandwidth bytes-per-second thresholds per client socket to mitigate Denial of Service (DoS).
- **Broker Live Telemetry (`src/telemetry.c`, `include/telemetry.h`):**
  Thread-safe metrics engine tracking active connections, packet counters, bandwidth bytes, LWT fires, and throttle drops with JSON telemetry output.

---

## 3. Project Directory Structure

```
acn-course-project/
├── IT365_MidSem_Presentation.pdf   # Mid-Semester Evaluation Presentation (16:9 Beamer, 17 slides)
├── IT365_MidsemPresentation.pdf   # Original Project Proposal Presentation
├── IT365_MidSem_Report.pdf         # Mid-Semester Technical Report (IEEE Conference Format, 4 pages)
├── PLAN.md                        # Master architectural blueprint & 3-phase implementation plan
├── README.md                      # Project documentation and guide
├── Makefile                       # GNU Makefile (strict c11, -Wall -Wextra -pedantic -O2)
├── include/
│   ├── mqtt_protocol.h            # MQTT 3.1.1 constants, packet types, flags, return codes
│   ├── mqtt_packet.h              # In-memory structured representation of decoded packets
│   ├── mqtt_parser.h              # Bitwise packet decoder & framing prototypes
│   ├── mqtt_encoder.h             # Binary response packet serializer prototypes
│   ├── buffer.h                   # Dynamic stream accumulation buffer prototypes
│   ├── trie.h                     # Topic Radix Tree (Trie) & wildcard matching
│   ├── session.h                  # Client session state machine & offline FIFO queue
│   ├── retained.h                 # Retained message repository
│   ├── crypto.h                   # ChaCha20 & HMAC-SHA256 authenticated encryption
│   ├── throttle.h                 # Token bucket rate-limiter and traffic shaper
│   ├── telemetry.h                # Live metrics, packet accounting, and JSON dump
│   ├── server.h                   # Non-blocking TCP server & epoll reactor definitions
│   └── logger.h                   # Colorized, formatted logging utilities
├── src/
│   ├── main.c                     # Broker CLI entrypoint, argument parsing, signal handling
│   ├── server.c                   # Linux epoll event loop, client management, dispatch
│   ├── buffer.c                   # Stream accumulation buffer implementation
│   ├── mqtt_parser.c              # Strict bitwise packet decoder implementation
│   ├── mqtt_encoder.c             # Binary packet serializers
│   ├── trie.c                     # Topic Radix Tree implementation
│   ├── session.c                  # Client session store & offline queueing
│   ├── retained.c                 # Retained message store implementation
│   ├── crypto.c                   # RFC 8439 ChaCha20 + SHA256 / HMAC implementation
│   ├── throttle.c                 # Token-bucket rate limiter implementation
│   ├── telemetry.c                # Telemetry counters & reporting
│   └── logger.c                   # Timestamped logging implementation
├── scripts/
│   ├── demo.py                    # Interactive terminal demonstration harness
│   └── iot_simulation.py          # Multi-device IoT smart city & industrial simulation
└── tests/
    ├── test_buffer.c              # Unit tests for stream buffer growth, drain, boundaries
    ├── test_parser.c              # Unit tests for bitwise decoder, variable integers, framing
    ├── test_trie.c                # Unit tests for Radix Tree insertion, wildcards, deduplication
    ├── test_session.c             # Unit tests for clean/persistent sessions & offline queue
    ├── test_throttle.c            # Unit tests for token bucket rate limiting & bandwidth caps
    ├── test_crypto.c              # Unit tests for ChaCha20, SHA-256, and HMAC authenticated encryption
    ├── test_server_e2e.py         # End-to-end integration tests using pure Python sockets
    └── test_advanced_e2e.py       # End-to-end tests for wildcards, retained messages, LWT, watchdog
```

---

## 4. Building and Running

### Prerequisites
- Linux OS with kernel $\ge 2.6.28$ (for `epoll_create1` and `accept4`)
- GCC or Clang supporting C11
- GNU Make
- Python 3 (for running end-to-end integration tests; uses only Python standard library)

### Compilation
Build the broker binary and all test suites:
```bash
make clean
make
```
This produces an optimized `mqtt_broker` executable compiled with:
```bash
gcc -Wall -Wextra -pedantic -std=c11 -O2 -Iinclude -D_POSIX_C_SOURCE=200809L -pthread
```

### Running the Broker
```bash
# Start on default port 1883
./mqtt_broker

# Start with custom port and host
./mqtt_broker --host 0.0.0.0 --port 1883

# Enable verbose debug logging and packet hex dumps
./mqtt_broker -p 1883 -d

# Enable Software ChaCha20 / HMAC-SHA256 Payload Encryption
./mqtt_broker -p 1883 -e -k "nitk-secret-key-32bytes-it365!!"

# Enable Network Traffic Throttling (e.g. 50 pkts/sec limit, burst 100, 1 MB/s bandwidth)
./mqtt_broker -p 1883 -r 50 -b 100 -m 1048576

# Combine all security, networking, and debug options
./mqtt_broker -p 1883 -d -e -k "nitk-secret-key-32bytes-it365!!" -r 100 -b 150
```

---

## 5. Live Demonstration & IoT Traffic Simulation

### 1. Multi-Device IoT Traffic Simulation (`scripts/iot_simulation.py`)
Simulate real-world traffic between dozens of virtual IoT devices in smart cities and industrial environments:
```bash
# Run simulation against running broker (e.g. port 1883)
make sim

# Or customize device counts, duration, and port:
python3 scripts/iot_simulation.py -p 1883 --weather 4 --grid 3 --fleet 3 --camera 2 --duration 15
```
**Simulated IoT Fleet:**
- **Weather Stations (`city/+/weather`):** Ambient temperature, humidity, and barometric pressure sensors publishing periodic telemetry.
- **Power Grid Sub-Stations (`grid/substation/+/status`):** Voltage, frequency, and load power monitoring.
- **Fleet GPS Trackers (`fleet/+/telemetry`):** High-frequency coordinates, speed, and fuel monitoring.
- **Security Cameras with LWT (`facility/camera/+/status`):** Heartbeats and automated **Last Will and Testament (LWT)** failover alarm delivery upon simulated network failure.
- **Central SCADA Control Dashboard (`#`):** Wildcard subscriber ingesting, validating, and calculating aggregate message arrival throughput in real-time.

### 2. Interactive Step-by-Step Demo (`scripts/demo.py`)
```bash
# Interactive menu:
make demo
# Or directly:
python3 scripts/demo.py

# Non-interactive automated presentation run:
make demo-auto
```

The interactive demo menu allows demonstrating:
1. **Full Protocol Walkthrough:** Live `CONNECT`, `PINGREQ`, `PUBLISH` (QoS 0 & 1), `SUBSCRIBE`, and `DISCONNECT`.
2. **Topic Radix Tree Wildcards (`+` and `#`):** Demonstrates dynamic fan-out and subscriber deduplication.
3. **Retained Message Delivery:** Publishes a retained system config and connects a late-joining subscriber to receive it immediately.
4. **Last Will and Testament (LWT):** Simulates ungraceful sensor drop and verifies automated alarm routing.
5. **Persistent Session Resumption:** Queues QoS 1 messages while a client is offline and flushes upon reconnection (`clean_session=0`).
6. **TCP Stream Defragmentation:** Sends a packet **1 byte at a time** with delays to prove application-layer stream reassembly.
7. **C10K High-Concurrency epoll Load:** Connects 30+ simultaneous clients in milliseconds without thread-lock overhead.
8. **Protocol Security & Validation:** Intentionally transmits malformed header flags to verify strict OASIS spec rejection.

---

## 6. Comprehensive Test Suite

The test suite validates both unit-level protocol parsing, tree routing, cryptographic security, rate limiting, and network-level integration.

### Running All Tests
```bash
make test
```

### Test Coverage Breakdown

#### 1. Dynamic Buffer Unit Tests (`tests/test_buffer.c`)
- Basic buffer initialization and memory allocation
- Data appending and exact size tracking
- Partial and complete data draining using linear compaction
- Dynamic capacity growth and reallocation
- Maximum capacity boundaries to prevent memory exhaustion

#### 2. Protocol Parser & Framing Unit Tests (`tests/test_parser.c`)
- **Variable-Byte Remaining Length Decoding & Encoding:** Tested for 1-byte ($0-127$), 2-byte ($128-16,383$), 3-byte ($16,384-2,097,151$), and 4-byte ($2,097,152-268,435,455$) boundary values.
- **Fixed Header Flag Validation:** Validates Table 2.2 of OASIS standard. Strict rejection of reserved flag violations on `CONNECT`, `SUBSCRIBE`, `PUBLISH` (QoS 3 rejection, QoS 0 with DUP rejection), `PINGREQ`, and `DISCONNECT`.
- **Packet Parsers:** Validates `CONNECT`, `PUBLISH`, and `SUBSCRIBE`.
- **TCP Stream Defragmentation:** Byte-by-byte feed verification.
- **Pipelined Multi-Packet Streams:** Sequential extraction of concatenated packets.

#### 3. Topic Radix Tree Unit Tests (`tests/test_trie.c`)
- Exact topic registration, lookup, and unsubscription
- Single-level wildcard (`+`) matching across multiple hierarchy depths
- Multi-level wildcard (`#`) matching and leaf resolution
- Multi-subscriber deduplication (MQTT-3.3.5-1)
- Socket FD updates and complete client teardown on disconnect

#### 4. Session State & Offline Queue Tests (`tests/test_session.c`)
- Clean session (`clean_session == 1`) vs. persistent session (`clean_session == 0`) creation
- Session retrieval and resurrection across socket reconnections
- Bounded FIFO offline message queueing for QoS 1 packets
- Offline message replay and session destruction

#### 5. Token-Bucket Rate Limiter Tests (`tests/test_throttle.c`)
- High-resolution token bucket initialization and refill logic
- Burst capacity handling and burst depletion
- Rate-limiting packet drops and drop accounting
- Dual bandwidth bytes-per-second throttling

#### 6. Cryptography Engine Tests (`tests/test_crypto.c`)
- RFC 6234 standard SHA-256 test vectors
- RFC 8439 ChaCha20 stream cipher XOR keystream verification
- HMAC-SHA256 message authentication code computation
- Authenticated payload encryption and tampering rejection

#### 7. Server Core Integration Tests (`tests/test_server_e2e.py`)
- Automated daemon lifecycle management via `subprocess`
- Real TCP connection establishment over loopback socket
- Full `CONNECT` $\rightarrow$ `CONNACK` handshake verification
- Keep-alive heartbeat: `PINGREQ` $\rightarrow$ `PINGRESP` verification
- QoS 1 publish flow: `PUBLISH` $\rightarrow$ `PUBACK` with matching 16-bit packet identifier
- Subscription flow: `SUBSCRIBE` $\rightarrow$ `SUBACK` with granted QoS array
- TCP Stream Fragmentation (1-byte chunks with delays)
- Concurrency test (30 simultaneous sockets)

#### 8. Advanced Network & Protocol Tests (`tests/test_advanced_e2e.py`)
- Multi-level (`#`) and single-level (`+`) wildcard publish fan-out
- In-memory retained message delivery to late-joining subscribers
- Last Will and Testament (LWT) broadcast upon abrupt socket abort
- Persistent session resumption with automatic offline QoS 1 queue flushing
- Keep-alive 1.5x timeout watchdog disconnection of unresponsive sockets

---

## 7. Implementation Status Matrix

| Component | Description | Status |
|---|---|---|
| **Linux epoll Reactor** | Non-blocking socket I/O multiplexing, `accept4()`, level-triggered event loop | **COMPLETED** |
| **Stream Accumulation Buffer** | Dynamic capacity growth, defragmentation, linear compaction drain | **COMPLETED** |
| **Bitwise Packet Parser** | Fixed header flags (Table 2.2), variable byte length, CONNECT, PUBLISH, SUBSCRIBE, PINGREQ, DISCONNECT | **COMPLETED** |
| **Wire Response Serializer** | CONNACK, SUBACK, PUBACK, PINGRESP, binary PUBLISH fan-out frame serialization | **COMPLETED** |
| **Topic Radix Tree (Trie)** | $O(K)$ hierarchical topic resolution, `+` single-level, `#` multi-level wildcards, deduplication | **COMPLETED** |
| **Session State Registry** | Clean vs Persistent sessions, hash table registry, QoS 1 offline queue and replay | **COMPLETED** |
| **Retained Message Store** | In-memory latest payload retention, immediate dispatch on subscription, clear on 0-len payload | **COMPLETED** |
| **Keep-Alive Watchdog** | $1.5 \times$ keep-alive expiration, background reactor tick, idle socket teardown | **COMPLETED** |
| **Last Will & Testament (LWT)** | Automatic LWT broadcast to subscribers on ungraceful drops, cleared on clean DISCONNECT | **COMPLETED** |
| **ChaCha20 & HMAC-SHA256** | Zero-dependency pure C payload cipher and authenticated encryption | **COMPLETED** |
| **Traffic Shaper & Throttling** | Dual-criterion token-bucket rate limiter (packets/sec and bandwidth bytes/sec) | **COMPLETED** |
| **Live Telemetry & Metrics** | Real-time counters, active connection tracking, throughput statistics | **COMPLETED** |
| **IoT Multi-Device Simulation** | Multi-device simulation harness with smart city sensors, fleet GPS, and SCADA dashboard | **COMPLETED** |
| **Interactive Demo Harness** | Step-by-step menu-driven protocol demonstration with live broker feedback | **COMPLETED** |

---

## 8. References

1. **Banks, A., Gupta, R.** *MQTT Version 3.1.1.* OASIS Standard, 2014.
2. **Stevens, W. R., Fenner, B., Rudoff, A. M.** *UNIX Network Programming: The Sockets Networking API.* Addison-Wesley Professional, 3rd Edition, 2003.
3. **Morrison, D. R.** *PATRICIA - Practical Algorithm To Retrieve Information Coded in Alphanumeric.* Journal of the ACM (JACM), 1968.
4. **Nir, Y., Langley, A.** *ChaCha20 and Poly1305 for IETF Protocols.* RFC 8439, 2018.
5. **Eastlake, D., Hansen, T.** *US Secure Hash Algorithms (SHA and SHA-based HMAC and HKDF).* RFC 6234, 2011.
6. **Kegel, D.** *The C10K problem.* `http://www.kegel.com/c10k.html`, 1999.
