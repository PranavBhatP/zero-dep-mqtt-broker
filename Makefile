CC ?= gcc
CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -O2 -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS ?= -pthread

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
BUILD_DIR = build

SRCS = $(SRC_DIR)/buffer.c \
       $(SRC_DIR)/logger.c \
       $(SRC_DIR)/mqtt_parser.c \
       $(SRC_DIR)/mqtt_encoder.c \
       $(SRC_DIR)/trie.c \
       $(SRC_DIR)/session.c \
       $(SRC_DIR)/retained.c \
       $(SRC_DIR)/throttle.c \
       $(SRC_DIR)/crypto.c \
       $(SRC_DIR)/telemetry.c \
       $(SRC_DIR)/server.c

OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
MAIN_OBJ = $(BUILD_DIR)/main.o

TARGET = mqtt_broker
TEST_BUFFER_BIN = $(BUILD_DIR)/test_buffer
TEST_PARSER_BIN = $(BUILD_DIR)/test_parser
TEST_TRIE_BIN = $(BUILD_DIR)/test_trie
TEST_SESSION_BIN = $(BUILD_DIR)/test_session
TEST_THROTTLE_BIN = $(BUILD_DIR)/test_throttle
TEST_CRYPTO_BIN = $(BUILD_DIR)/test_crypto

.PHONY: all clean test run demo demo-auto sim

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(MAIN_OBJ)
	$(CC) $(OBJS) $(MAIN_OBJ) $(LDFLAGS) -o $@
	@echo "Build complete: $(TARGET)"

# Unit Test Targets
$(TEST_BUFFER_BIN): $(TEST_DIR)/test_buffer.c $(BUILD_DIR)/buffer.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< $(BUILD_DIR)/buffer.o $(LDFLAGS) -o $@

$(TEST_PARSER_BIN): $(TEST_DIR)/test_parser.c $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

$(TEST_TRIE_BIN): $(TEST_DIR)/test_trie.c $(BUILD_DIR)/trie.o $(BUILD_DIR)/logger.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< $(BUILD_DIR)/trie.o $(BUILD_DIR)/logger.o $(LDFLAGS) -o $@

$(TEST_SESSION_BIN): $(TEST_DIR)/test_session.c $(BUILD_DIR)/session.o $(BUILD_DIR)/logger.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< $(BUILD_DIR)/session.o $(BUILD_DIR)/logger.o $(LDFLAGS) -o $@

$(TEST_THROTTLE_BIN): $(TEST_DIR)/test_throttle.c $(BUILD_DIR)/throttle.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< $(BUILD_DIR)/throttle.o $(LDFLAGS) -o $@

$(TEST_CRYPTO_BIN): $(TEST_DIR)/test_crypto.c $(BUILD_DIR)/crypto.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< $(BUILD_DIR)/crypto.o $(LDFLAGS) -o $@

test: $(TARGET) $(TEST_BUFFER_BIN) $(TEST_PARSER_BIN) $(TEST_TRIE_BIN) $(TEST_SESSION_BIN) $(TEST_THROTTLE_BIN) $(TEST_CRYPTO_BIN)
	@echo "\n>>> RUNNING BUFFER TESTS <<<"
	./$(TEST_BUFFER_BIN)
	@echo "\n>>> RUNNING PARSER & FRAMING TESTS <<<"
	./$(TEST_PARSER_BIN)
	@echo "\n>>> RUNNING TOPIC RADIX TREE (TRIE) TESTS <<<"
	./$(TEST_TRIE_BIN)
	@echo "\n>>> RUNNING SESSION REGISTRY TESTS <<<"
	./$(TEST_SESSION_BIN)
	@echo "\n>>> RUNNING TRAFFIC THROTTLE TESTS <<<"
	./$(TEST_THROTTLE_BIN)
	@echo "\n>>> RUNNING CRYPTOGRAPHY TESTS <<<"
	./$(TEST_CRYPTO_BIN)
	@echo "\n>>> RUNNING CORE END-TO-END SUITE <<<"
	chmod +x $(TEST_DIR)/test_server_e2e.py
	python3 $(TEST_DIR)/test_server_e2e.py
	@echo "\n>>> RUNNING ADVANCED FULL-FEATURE E2E SUITE <<<"
	chmod +x $(TEST_DIR)/test_advanced_e2e.py
	python3 $(TEST_DIR)/test_advanced_e2e.py
	@echo "\n=============================================="
	@echo "ALL UNIT & INTEGRATION TESTS PASSED (100% OK)!"
	@echo "=============================================="

run: $(TARGET)
	./$(TARGET) -p 1883

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

demo:
	python3 scripts/demo.py -p 1883

demo-auto:
	python3 scripts/demo.py -p 1883 --auto

sim:
	python3 scripts/iot_simulation.py -p 1883
