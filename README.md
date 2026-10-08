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
|  |     server.[c/h]       | ---> |    buffer.[c/h]    | ---> | mqtt_parser.c |  |
|  | - Master listen socket |      | - Dynamic rx_buf   |      | - Fixed header|  |
|  | - accept4() non-block  |      | - Defragmentation  |      | - Rem. length |  |
|  | - Client session map   |      | - Compaction drain |      | - Bit flags   |  |
|  +------------------------+      +--------------------+      +-------+-------+  |
|                                                                      |          |
|                                                                      v          |
|  +------------------------+      +--------------------+      +---------------+  |
|  |      session.[c/h]     | <--- |     trie.[c/h]     | <--- | mqtt_encoder.c|  |
|  | - Client states        |      | - Radix Tree       |      | - CONNACK     |  |
|  | - Keep-alive timers    |      | - Wildcards (+, #) |      | - SUBACK      |  |
|  | - Thread-safe mutex    |      | - O(K) lookup      |      | - PUBACK      |  |
|  +------------------------+      +--------------------+      +---------------+  |
+---------------------------------------------------------------------------------+
```

### Core Components
- **Non-blocking TCP Server (`src/server.c`, `include/server.h`):**
  Uses `socket()`, `setsockopt(SO_REUSEADDR | SO_REUSEPORT)`, `fcntl(O_NONBLOCK)`, `setsockopt(TCP_NODELAY)`, and `epoll_create1()`. Disables Nagle's algorithm for low-latency transmission of small control packets.
- **TCP Stream Buffer (`src/buffer.c`, `include/buffer.h`):**
  Dynamic linear byte buffer with dynamic capacity doubling and `memmove()`-based compaction. Accommodates partial packet fragments and pipelines multiple packets in a single TCP read.
- **Bitwise Protocol Decoder (`src/mqtt_parser.c`, `include/mqtt_parser.h`):**
  Strict decoder for OASIS MQTT 3.1.1 control packets (`CONNECT`, `PUBLISH`, `SUBSCRIBE`, `PINGREQ`, `DISCONNECT`). Decodes variable-byte remaining lengths (1 to 4 bytes) and enforces Table 2.2 reserved flag rules.
- **Binary Response Serializer (`src/mqtt_encoder.c`, `include/mqtt_encoder.h`):**
  Constructs wire-format responses (`CONNACK`, `PINGRESP`, `SUBACK`, `PUBACK`).

---

## 3. Project Directory Structure

```
acn-course-project/
├── IT365_MidSem_Presentation.pdf   # Mid-Semester Evaluation Presentation (16:9 Beamer, 17 slides)
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
│   ├── server.h                   # Non-blocking TCP server & epoll reactor definitions
│   └── logger.h                   # Colorized, formatted logging utilities
├── src/
│   ├── main.c                     # Broker CLI entrypoint, argument parsing, signal handling
│   ├── server.c                   # Linux epoll event loop, client management, dispatch
│   ├── buffer.c                   # Stream accumulation buffer implementation
│   ├── mqtt_parser.c              # Strict bitwise packet decoder implementation
│   ├── mqtt_encoder.c             # Binary packet serializers
│   └── logger.c                   # Timestamped logging implementation
└── tests/
    ├── test_buffer.c              # Unit tests for stream buffer growth, drain, boundaries
    ├── test_parser.c              # Unit tests for bitwise decoder, variable integers, framing
    └── test_server_e2e.py         # End-to-end integration tests using pure Python sockets
```

---

## 4. Building and Running

### Prerequisites
- Linux OS with kernel $\ge 2.6.28$ (for `epoll_create1` and `accept4`)
- GCC or Clang supporting C11
- GNU Make
- Python 3 (for running end-to-end integration tests; uses only Python standard library)

### Compilation
Build the broker binary:
```bash
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
```

---

## 5. Live Demonstration Guide

To demo the broker for evaluations or presentations, you can run the broker with debug logging in one terminal and the interactive demo in a second terminal:

### Terminal 1: Launch Broker with Debug Logs
```bash
./mqtt_broker -d -p 1883
```
This prints live incoming TCP connections, hex dumps of binary stream chunks, and decoded MQTT fields.

### Terminal 2: Run the Interactive Demo
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
2. **TCP Stream Defragmentation:** Sends a packet **1 byte at a time** with delays to prove application-layer stream reassembly.
3. **C10K High-Concurrency epoll Load:** Connects 30+ simultaneous clients in milliseconds without thread-lock overhead.
4. **Protocol Security & Validation:** Intentionally transmits malformed header flags to verify strict OASIS spec rejection.

---

## 6. Comprehensive Test Suite

The test suite validates both unit-level protocol parsing and network-level integration.

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
- **Packet Parsers:** Validates `CONNECT` (protocol name `"MQTT"`, protocol level `0x04`, CleanSession, KeepAlive, ClientID), `PUBLISH` (Topic name, wildcards rejection, QoS 0/1, payload extraction), and `SUBSCRIBE` (multiple topic filters, requested QoS).
- **TCP Stream Defragmentation:** Simulates worst-case network behavior by feeding raw packets byte-by-byte into `byte_buffer_t`, verifying that the parser signals `MQTT_NEED_MORE_DATA` until the exact terminating byte is delivered.
- **Pipelined Multi-Packet Streams:** Verifies that multiple concatenated packets arriving in a single `recv()` chunk are parsed and drained sequentially.

#### 3. End-to-End Network Integration Tests (`tests/test_server_e2e.py`)
- Automated daemon lifecycle management via `subprocess`
- Real TCP connection establishment over loopback socket
- Full `CONNECT` $\rightarrow$ `CONNACK` handshake verification
- Keep-alive heartbeat: `PINGREQ` $\rightarrow$ `PINGRESP` verification
- QoS 1 publish flow: `PUBLISH` $\rightarrow$ `PUBACK` with matching 16-bit packet identifier
- Subscription flow: `SUBSCRIBE` $\rightarrow$ `SUBACK` with granted QoS array
- **TCP Stream Fragmentation:** Sends a valid packet 1 byte at a time with sleep intervals, verifying broker stream reassembly
- **Concurrency Test:** Establishes 30 concurrent client sockets simultaneously, performing handshakes and heartbeats across all sessions in parallel without drops

### Test Output
```
>>> RUNNING BUFFER TESTS <<<
./build/test_buffer
========================================
RUNNING BUFFER UNIT TESTS
========================================
[TEST] Running test_buffer_basic...
  -> PASS: test_buffer_basic
[TEST] Running test_buffer_growth...
  -> PASS: test_buffer_growth
ALL BUFFER TESTS PASSED SUCCESSFULLY!

>>> RUNNING PARSER & FRAMING TESTS <<<
./build/test_parser
========================================
RUNNING MQTT PARSER & FRAMING UNIT TESTS
========================================
[TEST] Running test_remaining_length_encoding_decoding...
  -> PASS: test_remaining_length_encoding_decoding
[TEST] Running test_fixed_header_flags...
  -> PASS: test_fixed_header_flags
[TEST] Running test_connect_packet_parsing...
  -> PASS: test_connect_packet_parsing
[TEST] Running test_publish_packet_parsing...
  -> PASS: test_publish_packet_parsing
[TEST] Running test_subscribe_packet_parsing...
  -> PASS: test_subscribe_packet_parsing
[TEST] Running test_stream_defragmentation (byte-by-byte TCP stream accumulation)...
  -> PASS: test_stream_defragmentation
[TEST] Running test_packet_pipelining (multiple packets in single TCP chunk)...
  -> PASS: test_packet_pipelining
ALL MQTT PARSER TESTS PASSED SUCCESSFULLY!

>>> RUNNING END-TO-END INTEGRATION TESTS <<<
python3 tests/test_server_e2e.py
==================================================
STARTING END-TO-END INTEGRATION TEST SUITE
Target Broker: 127.0.0.1:11883
==================================================
  -> Testing CONNECT handshake...
     [PASS] CONNACK received successfully (Accepted)
  -> Testing PINGREQ heartbeat...
     [PASS] PINGRESP received successfully
  -> Testing PUBLISH QoS 1 and PUBACK...
     [PASS] PUBACK received with matching PacketID 1337
  -> Testing SUBSCRIBE and SUBACK...
     [PASS] SUBACK received for 2 topics with matching PacketID 99
  -> Testing TCP Stream Fragmentation (sending 1 byte at a time with delays)...
     [PASS] Fragmented TCP stream properly defragmented and accepted
  -> Testing Concurrent Client Handling (30 simultaneous sockets)...
     [PASS] Successfully handled 30 concurrent client sessions

==================================================
ALL END-TO-END INTEGRATION TESTS PASSED (100% OK)
==================================================

==============================================
ALL PHASE 1 TESTS PASSED SUCCESSFULLY!
==============================================
```

---

## 7. Implementation Status & Next Phases

For the complete end-to-end architectural roadmap covering all phases, refer to [`PLAN.md`](PLAN.md).

| Phase | Description | Status |
|---|---|---|
| **Phase 1: Foundation** | Non-blocking TCP server (epoll), dynamic stream buffer (`byte_buffer_t`), bitwise MQTT 3.1.1 packet parser, response encoders, and integration test suite. | **COMPLETED** |
| **Phase 2: Handshake & Routing** | CONNECT/CONNACK state logic, Clean vs Persistent session store, Topic Radix Tree (Trie) for O(K) lookup, wildcard matching (`+` and `#`). | Planned |
| **Phase 3: Payload Fan-out & Resilience** | Trie traversal on PUBLISH with binary payload fan-out to subscriber sockets, PINGREQ keep-alive timeout watchdog, ungraceful teardown, and LWT delivery. | Planned |

---

## 8. References

1. **Banks, A., Gupta, R.** *MQTT Version 3.1.1.* OASIS Standard, 2014.
2. **Stevens, W. R., Fenner, B., Rudoff, A. M.** *UNIX Network Programming: The Sockets Networking API.* Addison-Wesley Professional, 3rd Edition, 2003.
3. **Morrison, D. R.** *PATRICIA - Practical Algorithm To Retrieve Information Coded in Alphanumeric.* Journal of the ACM (JACM), 1968.
4. **Kegel, D.** *The C10K problem.* `http://www.kegel.com/c10k.html`, 1999.
