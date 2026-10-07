# End-to-End Implementation Plan: Zero-Dependency MQTT Broker

**Course:** IT365 Advanced Computer Networks  
**Institution:** National Institute of Technology Karnataka (NITK), Surathkal  
**Authors:**
- Pranav Bhat (231IT049)
- Pranav Vinodh (231IT050)
- Rijul Raman (231IT056)

---

## 1. Executive Summary & Project Vision

The **Zero-Dependency MQTT Broker** is an implementation of an OASIS MQTT Version 3.1.1 compliant message broker written entirely in standard C (C11) from first principles. It relies solely on native Linux kernel socket primitives (`sys/socket.h`, `sys/epoll.h`, `netinet/in.h`, `fcntl.h`) and standard POSIX system interfaces.

### Core Philosophy
1. **Zero External Dependencies:** No third-party network event libraries (e.g., `libuv`, `libevent`, `Boost.Asio`), no protocol parsers, and no third-party data structure libraries.
2. **Deterministic High Performance (C10K Capable):** Utilizing Linux `epoll` I/O event demultiplexing to handle thousands of concurrent client connections with minimal CPU overhead and zero thread-context switching overhead in the I/O path.
3. **Strict Bit-Level Protocol Conformance:** Implementing exact binary wire-format decoding, bit-shifted flags, variable-length integer decoding, and strict error handling conforming to the OASIS MQTT 3.1.1 specification.
4. **Algorithmic Efficiency:** Utilizing a Radix Tree (Topic Trie) data structure for $O(K)$ hierarchical topic resolution and wildcard matching (`+` and `#`).

```
+-----------------------------------------------------------------------------------+
|                        Zero-Dependency MQTT 3.1.1 Broker                          |
|                                                                                   |
|  +-----------------------------------------------------------------------------+  |
|  |                 Non-Blocking Network Layer (Linux epoll)                   |  |
|  |   - socket(), bind(), listen(), accept4(), epoll_create1(), epoll_wait()   |  |
|  +---------------------------------------+-------------------------------------+  |
|                                          |                                        |
|                                          v                                        |
|  +-----------------------------------------------------------------------------+  |
|  |          TCP Stream Accumulation & Compaction Buffer (byte_buffer_t)        |  |
|  |            - Defragments TCP stream segments & reassembles frames           |  |
|  +---------------------------------------+-------------------------------------+  |
|                                          |                                        |
|                                          v                                        |
|  +-----------------------------------------------------------------------------+  |
|  |             Bitwise MQTT 3.1.1 Packet Decoder (mqtt_parser.c)               |  |
|  |   - Variable-length integer decoding (1-4 bytes)                            |  |
|  |   - Fixed Header flag validation matrix (Table 2.2)                         |  |
|  |   - CONNECT / PUBLISH / SUBSCRIBE / PINGREQ / DISCONNECT binary parsing     |  |
|  +---------------------------------------+-------------------------------------+  |
|                                          |                                        |
|                     +--------------------+--------------------+                   |
|                     |                                         |                   |
|                     v                                         v                   |
|  +-------------------------------------+   +-----------------------------------+  |
|  |   Topic Radix Tree / Topic Trie     |   |   Client & Session Registry       |  |
|  |  - Path splitting by '/'            |   |  - ClientID & clean_session state |  |
|  |  - Exact & Wildcard matching (+, #) |   |  - Keep-alive heartbeat tracking  |  |
|  |  - O(K) lookup & subscription table |   |  - Thread-safe registry mutex     |  |
|  +-------------------------------------+   +-----------------------------------+  |
+-----------------------------------------------------------------------------------+
```

---

## 2. Theoretical Grounding & Literature Survey

The architecture directly synthesizes core principles from canonical literature in computer systems and network programming:

1. **OASIS Standard (MQTT Version 3.1.1, 2014 - Banks & Gupta):**
   - Defines strict binary packet structures, 4-bit control packet types, variable-length remaining length fields, and quality-of-service guarantees.
   - Mandates strict error actions (e.g., closing TCP connections upon receiving malformed reserved header flags).

2. **UNIX Network Programming (Stevens, Fenner, Rudoff, 2003):**
   - Non-blocking I/O semantics (`O_NONBLOCK`).
   - Mitigating TCP stream fragmentation via application-layer framing buffers.
   - Disabling Nagle’s algorithm via `TCP_NODELAY` to prevent 40ms delayed-ACK penalties on small control packets.
   - Port reusability via `SO_REUSEADDR` and `SO_REUSEPORT`.
   - Handling `SIGPIPE` to prevent broker crashes on premature socket drops.

3. **PATRICIA - Practical Algorithm To Retrieve Information Coded in Alphanumeric (Morrison, 1968; Jiang et al.):**
   - Standard hash tables fail on MQTT wildcard topic routing (`sensor/+/temperature` or `home/#`).
   - A multi-way prefix tree (Trie / Radix Tree) provides $O(K)$ resolution where $K$ is the number of path segments in the published topic, invariant to the total number of subscriptions.

4. **The C10K Problem (Kegel, 1999):**
   - Traditional thread-per-connection architectures collapse under memory exhaustion and context-switching overhead when scaling to thousands of concurrent IoT clients.
   - Linux `epoll` provides $O(1)$ event notifications, enabling a single reactor thread to sustain high throughput and low latency.

---

## 3. High-Level 3-Phase Project Roadmap

```
+-------------------------------------------------------------------------------+
| PHASE 1: FOUNDATION [STATUS: FULLY IMPLEMENTED & VERIFIED]                    |
| - Non-blocking TCP server architecture using Linux epoll reactor              |
| - Dynamic TCP stream accumulation buffer for stream defragmentation           |
| - Bitwise OASIS MQTT 3.1.1 packet parser for fixed and variable headers       |
| - Binary response serializer (CONNACK, PINGRESP, SUBACK, PUBACK)              |
| - Comprehensive unit test suite and Python end-to-end integration tests       |
+---------------------------------------+---------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 2: HANDSHAKE & ROUTING ENGINE                                           |
| - Formal CONNECT / CONNACK state machine and client session establishment     |
| - Clean Session vs. Persistent Session state management                       |
| - Topic Radix Tree (Trie) construction with hierarchical node splitting       |
| - Wildcard subscription resolution (+ single-level and # multi-level)         |
| - SUBSCRIBE / SUBACK and UNSUBSCRIBE / UNSUBACK handlers                      |
+---------------------------------------+---------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
| PHASE 3: PAYLOAD FAN-OUT & RESILIENCE                                         |
| - Trie traversal on PUBLISH with binary payload fan-out to matched sockets   |
| - Non-blocking write queuing (tx_buffer) with EPOLLOUT edge handling          |
| - QoS 0 and QoS 1 delivery flows with Packet Identifier tracking & PUBACK     |
| - PINGREQ keep-alive watchdog timer with 1.5x timeout expiration              |
| - Socket disconnection handling and Last Will and Testament (LWT) publishing  |
+-------------------------------------------------------------------------------+
```

---

## 4. Phase 1: Foundation (Detailed Specification & Completed Work)

Phase 1 provides the core networking and protocol parsing engine upon which all subsequent broker logic operates.

### 4.1 Native Non-Blocking TCP Server Architecture
- **Socket Primitives:** Uses `socket(AF_INET, SOCK_STREAM, 0)`.
- **Socket Configuration:**
  - `SO_REUSEADDR` and `SO_REUSEPORT` set on the listening socket.
  - `O_NONBLOCK` applied to both listening and accepted client sockets via `fcntl()`.
  - `TCP_NODELAY` applied to all accepted sockets to disable Nagle's algorithm.
- **Linux epoll Reactor:**
  - Initialized with `epoll_create1(EPOLL_CLOEXEC)`.
  - Listening socket registered with `EPOLLIN`.
  - New connections accepted inside a non-blocking `accept4()` loop until `EAGAIN` / `EWOULDBLOCK` is encountered.
  - Client sockets registered with `EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP`.
  - Level-triggered I/O with continuous drain loop ensures no incoming bytes remain buffered in kernel space.
- **Signal Handling:**
  - Intercepts `SIGINT` and `SIGTERM` for clean teardown.
  - Explicitly ignores `SIGPIPE` (`signal(SIGPIPE, SIG_IGN)`).

### 4.2 Stream Accumulation & Compaction Buffer (`byte_buffer_t`)
TCP does not preserve application message boundaries. A single `recv()` call can yield:
1. Partial fixed header bytes.
2. Partial variable length integer bytes.
3. Partial payload bytes.
4. Multiple concatenated MQTT packets in a single segment.

The `byte_buffer_t` data structure solves this:
```c
typedef struct {
    uint8_t *data;
    size_t   size;         /* Current number of valid bytes */
    size_t   capacity;     /* Current allocated memory */
    size_t   max_capacity; /* Bounded ceiling (512 KB) to prevent memory exhaustion */
} byte_buffer_t;
```
- **Operations:**
  - `byte_buffer_append()`: Dynamically grows capacity (doubling) up to `max_capacity`.
  - `byte_buffer_drain()`: Uses `memmove()` to slide remaining unparsed bytes to index 0 after a complete packet is consumed.

### 4.3 Bitwise OASIS MQTT 3.1.1 Packet Decoder
1. **Fixed Header Byte 1 Extraction:**
   - Packet Type: `(byte >> 4) & 0x0F`
   - Flags: `byte & 0x0F`
   - Strict validation matrix against Table 2.2 of the OASIS standard:
     - `CONNECT`, `CONNACK`, `PUBACK`, `PUBREC`, `PUBCOMP`, `SUBACK`, `UNSUBACK`, `PINGREQ`, `PINGRESP`, `DISCONNECT` **MUST** have flags `0000`.
     - `PUBREL`, `SUBSCRIBE`, `UNSUBSCRIBE` **MUST** have flags `0010`.
     - `PUBLISH`: DUP (bit 3), QoS (bits 2-1), RETAIN (bit 0). QoS 3 is rejected; QoS 0 with DUP=1 is rejected.
2. **Variable-Byte Remaining Length Decoding (Section 2.2.3):**
   - Supports 1 to 4 bytes (values 0 up to 268,435,455).
   - Algorithm iterates up to 4 bytes accumulating `(encoded_byte & 0x7F) * multiplier`.
   - Returns `MQTT_NEED_MORE_DATA` if more bytes are needed.
   - Returns `MQTT_ERR_MALFORMED` if continuation bit (bit 7) remains set on the 4th byte.
3. **Variable Header & Payload Parsing:**
   - `CONNECT`: Verifies protocol name `"MQTT"`, protocol level `0x04`, checks reserved bit 0 in connect flags, decodes Keep-Alive, Client Identifier, Will parameters, Username, and Password.
   - `PUBLISH`: Decodes Topic Name (rejecting any topic containing `+` or `#`), Packet ID (if QoS > 0), and extracts raw payload pointer and length.
   - `SUBSCRIBE`: Decodes Packet ID, iterates topic filters and requested QoS (validating reserved bits 7-2 are 0).
   - `PINGREQ` & `DISCONNECT`: Validates remaining length is exactly 0.

### 4.4 Binary Packet Encoder
- `mqtt_encode_connack()`: Encodes 4-byte CONNACK (0x20, 0x02, session_present, return_code).
- `mqtt_encode_pingresp()`: Encodes 2-byte PINGRESP (0xD0, 0x00).
- `mqtt_encode_suback()`: Encodes SUBACK with Packet ID and granted QoS array.
- `mqtt_encode_puback()`: Encodes 4-byte PUBACK with Packet ID.

---

## 5. Phase 2: Handshake & Routing Engine (Detailed Plan)

Phase 2 will implement the complete stateful session logic and the hierarchical topic routing engine.

### 5.1 CONNECT / CONNACK State Machine & Session Registry

```
                    +--------------------+
                    |  TCP Connected     |
                    | (CONN_STATE_NEW)   |
                    +---------+----------+
                              |
                      CONNECT Packet Received
                              |
               +--------------+--------------+
               |                             |
      Protocol Valid?                  Protocol Invalid?
               |                             |
        [Yes]  |                      [No]   |
               v                             v
      Clean Session?                  Send CONNACK (0x01/0x02)
       /          \                   Close TCP Socket
      v            v
 Clean=1         Clean=0
(Discard Old)   (Resume Old Session)
      \            /
       v          v
   Send CONNACK (0x00)
State -> CONN_STATE_CONNECTED
```

#### Detailed Tasks:
1. **Client Identity Verification:**
   - Validate UTF-8 client identifier syntax.
   - If `client_id` is zero-length, verify `clean_session == 1`. Automatically generate a unique server-side client ID (`anon-<fd>-<timestamp>`). If `clean_session == 0` with empty ID, reject with CONNACK return code `0x02` (Identifier Rejected).
2. **Duplicate Connection Handling:**
   - If an existing active connection exists with the same `client_id`, the broker must disconnect the older connection with a log notice and assign the session to the new socket (MQTT-3.1.4-2).
3. **Session Store (`session_t`):**
   - Structure tracking:
     * `client_id`
     * `socket_fd`
     * `clean_session` flag
     * `subscriptions`: dynamic list of subscribed topic filters and granted QoS
     * `offline_message_queue`: message buffer for QoS 1 messages arriving while client is disconnected (when `clean_session == 0`)
     * `will_message`: retained will topic, payload, and QoS
4. **Thread-Safe Session Map:**
   - Implement a hash map keyed by `client_id` with `pthread_rwlock_t` or `pthread_mutex_t` for thread safety.

### 5.2 Topic Radix Tree (Topic Trie) Architecture

Standard string lookups using hash maps fail because MQTT allows wildcard subscriptions:
- `+` (Single-Level Wildcard): Matches exactly one topic level (e.g., `sport/+/player`).
- `#` (Multi-Level Wildcard): Matches any number of topic levels at the end of the filter (e.g., `sport/tennis/#`).

#### 5.2.1 Trie Node Definition
```c
typedef struct trie_subscriber {
    char            client_id[MQTT_MAX_CLIENT_ID_LEN];
    int             socket_fd;
    uint8_t         qos;
    struct trie_subscriber *next;
} trie_subscriber_t;

typedef struct trie_node {
    char               *topic_level;       /* e.g., "sport", "tennis", "+", "#" */
    trie_subscriber_t  *subscribers;       /* Clients subscribed at this exact node */
    
    struct trie_node   *children;          /* Linked list or array of child nodes */
    struct trie_node   *next_sibling;
    
    struct trie_node   *wildcard_plus;     /* Shortcut to '+' child if present */
    struct trie_node   *wildcard_hash;     /* Shortcut to '#' child if present */
} trie_node_t;
```

#### 5.2.2 Subscription Insertion Algorithm
```
Function: trie_insert(root, topic_filter, client_id, socket_fd, qos)
1. Split topic_filter by '/' delimiter into array of levels: [L_0, L_1, ..., L_{n-1}].
2. curr_node = root.
3. For each level L_i in levels:
   a. If L_i == '+':
      - Search or create child node for '+' under curr_node.
   b. Else if L_i == '#':
      - Verify L_i is the last level (MQTT-4.7.1-2).
      - Search or create child node for '#' under curr_node.
   c. Else:
      - Search child nodes of curr_node for matching level string.
      - If not found, create new child node with topic_level = L_i.
   d. curr_node = matched_or_created_child.
4. Under curr_node, add client_id and socket_fd to subscribers linked list (or update QoS if already subscribed).
```

#### 5.2.3 Subscription Deletion Algorithm (UNSUBSCRIBE)
- Traverse Trie matching the filter levels.
- Remove client from `subscribers` list.
- Prune empty leaf nodes recursively to prevent memory leaks.

---

## 6. Phase 3: Payload Fan-Out & Resilience (Detailed Plan)

Phase 3 implements message routing from publishers to subscribers, keep-alive watchdog timers, and fault tolerance.

### 6.1 Trie Traversal & Message Fan-out Algorithm
When a `PUBLISH` packet arrives for topic `P = "home/kitchen/temperature"`:

```
                  Published Topic: "home/kitchen/temperature"
                                     |
                                  [root]
                                 /   |   \
                           "home"   "+"   "#"  --> (Collect '#' subscribers immediately)
                            /
                      "kitchen"
                       /     \
                "temperature" "+"
                     |
         (Collect exact subscribers
           and '+' subscribers)
```

1. **Recursive Traversal:**
   - At each level $i$ with topic segment $S_i$:
     - Check exact child matching $S_i$: recurse to level $i+1$.
     - Check `+` child: recurse to level $i+1$.
     - Check `#` child: collect all subscribers registered on `#` (matches remainder of hierarchy).
   - At the leaf level ($i = n$): collect all subscribers registered on this node.
2. **Subscriber Deduplication:**
   - A client may subscribe to both `home/kitchen/+` and `home/#`. Per MQTT-3.3.5-1, the client **MUST NOT** receive duplicate messages.
   - Maintain a local matched client set for the current publish event to ensure each unique `client_id` receives the message exactly once, with the maximum granted QoS.
3. **Payload Fan-out:**
   - Construct binary PUBLISH packet once.
   - For each matched subscriber, write bytes to their non-blocking socket or queue in their `tx_buffer`.

### 6.2 Keep-Alive Watchdog & Heartbeat Timers
- **Specification Rule (MQTT-3.1.2-10):**
  If Keep Alive is non-zero, the client must send a packet within $1.5 \times \text{keep\_alive}$ seconds.
- **Implementation:**
  - On receiving any packet (`PINGREQ`, `PUBLISH`, `SUBSCRIBE`), update `client->last_active_time = time(NULL)`.
  - In the epoll reactor loop (or via a timer tick every 1 second):
    - Iterate active clients.
    - If `now - client->last_active_time > 1.5 * client->keep_alive_sec`:
      - Log keep-alive timeout.
      - Trigger Last Will and Testament (LWT) message publishing (if configured).
      - Close socket and unregister from epoll.

### 6.3 Last Will and Testament (LWT) & Disconnect Handling
- **Graceful DISCONNECT:**
  - Client sends `DISCONNECT` (0xE0 0x00).
  - Broker clears the client's Will message (MQTT-3.14.4-1).
  - Broker closes connection cleanly.
- **Ungraceful Disconnection:**
  - Caused by TCP connection reset (`ECONNRESET`), socket error, or keep-alive timeout.
  - If `will_flag == 1`, broker immediately publishes the stored `will_message` to `will_topic` using the topic routing engine.

---

## 7. Concurrency, Thread Safety & Memory Model

### 7.1 Single-Reactor Event-Driven Model
The primary network I/O loop executes in a single reactor thread:
- **No lock contention in fast path:** All epoll read/write events, packet parsing, and buffer draining occur without mutex locking during single-threaded execution.
- **Thread-safe Registry:** For background watchdog timers or multi-threaded scaling, the client session registry and topic trie are guarded by `pthread_mutex_t` and `pthread_rwlock_t`.

### 7.2 Memory Safety & Buffer Bounds
- **Bounded Buffers:** Every client connection has a hard ceiling `MQTT_MAX_BUFFER_SIZE` (512 KB). Any client attempting to flood memory is disconnected immediately (`MQTT_ERR_BUFFER_OVERFLOW`).
- **Zero Memory Leaks:** Every allocated buffer is reclaimed on client disconnect, verified via Valgrind and unit test teardowns.

---

## 8. Verification, Testing & Deliverables Matrix

| Phase | Milestone / Feature | Verification Method | Status |
|---|---|---|---|
| **Phase 1** | Non-blocking TCP Server (Linux epoll) | `test_server_e2e.py` concurrent socket tests | **COMPLETED** |
| **Phase 1** | Dynamic stream accumulation buffer (`byte_buffer_t`) | `tests/test_buffer.c` unit test suite | **COMPLETED** |
| **Phase 1** | Variable-byte integer decoder (1-4 bytes) | `tests/test_parser.c` (values 0 to 268,435,455) | **COMPLETED** |
| **Phase 1** | Fixed Header flag validation matrix (Table 2.2) | `tests/test_parser.c` (invalid flags rejection) | **COMPLETED** |
| **Phase 1** | CONNECT / PUBLISH / SUBSCRIBE / PINGREQ parsing | `tests/test_parser.c` & `test_server_e2e.py` | **COMPLETED** |
| **Phase 1** | Stream Defragmentation (1-byte chunk arrival) | `tests/test_parser.c` & `test_server_e2e.py` | **COMPLETED** |
| **Phase 1** | Multi-packet pipelining in single TCP segment | `tests/test_parser.c` pipelining test | **COMPLETED** |
| **Phase 2** | Clean Session vs Persistent Session state store | Unit tests for session resumption | Planned |
| **Phase 2** | Radix Tree (Topic Trie) data structure | Unit tests for topic insertion & removal | Planned |
| **Phase 2** | Single (`+`) and Multi-level (`#`) Wildcards | Unit tests for wildcard matching | Planned |
| **Phase 3** | Trie traversal & multi-subscriber fan-out | Integration test with 1 pub & $N$ subs | Planned |
| **Phase 3** | Keep-Alive 1.5x timeout watchdog | Integration test with simulated idle drop | Planned |
| **Phase 3** | Last Will and Testament (LWT) delivery | Integration test with ungraceful TCP drop | Planned |

---

## 9. References

1. **Banks, A., Gupta, R.** *MQTT Version 3.1.1.* OASIS Standard, 2014.
2. **Stevens, W. R., Fenner, B., Rudoff, A. M.** *UNIX Network Programming: The Sockets Networking API.* Addison-Wesley Professional, 3rd Edition, 2003.
3. **Morrison, D. R.** *PATRICIA - Practical Algorithm To Retrieve Information Coded in Alphanumeric.* Journal of the ACM (JACM), 1968.
4. **Kegel, D.** *The C10K problem.* `http://www.kegel.com/c10k.html`, 1999.
5. **Jiang, H., et al.** *Lightweight and High-Performance Topic Routing Algorithms for MQTT.* IEEE Internet of Things Journal.
