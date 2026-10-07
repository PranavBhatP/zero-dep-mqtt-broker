#include "buffer.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_buffer_basic(void) {
    printf("[TEST] Running test_buffer_basic...\n");
    byte_buffer_t buf;
    assert(byte_buffer_init(&buf, 16, 1024) == true);
    assert(buf.size == 0);
    assert(buf.capacity >= 16);

    const char *msg = "Hello MQTT";
    assert(byte_buffer_append(&buf, (const uint8_t *)msg, strlen(msg)) == true);
    assert(buf.size == strlen(msg));
    assert(memcmp(buf.data, msg, strlen(msg)) == 0);

    /* Partial drain */
    byte_buffer_drain(&buf, 6); /* Drain "Hello " */
    assert(buf.size == 4);
    assert(memcmp(buf.data, "MQTT", 4) == 0);

    /* Over drain */
    byte_buffer_drain(&buf, 10);
    assert(buf.size == 0);

    byte_buffer_free(&buf);
    assert(buf.data == NULL);
    printf("  -> PASS: test_buffer_basic\n");
}

void test_buffer_growth(void) {
    printf("[TEST] Running test_buffer_growth...\n");
    byte_buffer_t buf;
    assert(byte_buffer_init(&buf, 8, 256) == true);

    uint8_t large_data[100];
    for (int i = 0; i < 100; i++) large_data[i] = (uint8_t)(i & 0xFF);

    assert(byte_buffer_append(&buf, large_data, sizeof(large_data)) == true);
    assert(buf.size == 100);
    assert(buf.capacity >= 100);
    assert(memcmp(buf.data, large_data, 100) == 0);

    /* Test max capacity boundary */
    uint8_t too_large[200];
    assert(byte_buffer_append(&buf, too_large, sizeof(too_large)) == false); /* 100 + 200 > 256 */
    assert(buf.size == 100); /* Size unchanged */

    byte_buffer_free(&buf);
    printf("  -> PASS: test_buffer_growth\n");
}

int main(void) {
    printf("========================================\n");
    printf("RUNNING BUFFER UNIT TESTS\n");
    printf("========================================\n");
    test_buffer_basic();
    test_buffer_growth();
    printf("ALL BUFFER TESTS PASSED SUCCESSFULLY!\n\n");
    return 0;
}
