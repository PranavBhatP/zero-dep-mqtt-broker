#include "buffer.h"
#include <stdlib.h>
#include <string.h>

bool byte_buffer_init(byte_buffer_t *buf, size_t initial_capacity, size_t max_capacity) {
    if (!buf) return false;
    if (initial_capacity == 0) initial_capacity = 256;
    if (max_capacity < initial_capacity) max_capacity = initial_capacity;

    buf->data = (uint8_t *)malloc(initial_capacity);
    if (!buf->data) {
        buf->size = 0;
        buf->capacity = 0;
        buf->max_capacity = 0;
        return false;
    }

    buf->size = 0;
    buf->capacity = initial_capacity;
    buf->max_capacity = max_capacity;
    return true;
}

void byte_buffer_free(byte_buffer_t *buf) {
    if (!buf) return;
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->size = 0;
    buf->capacity = 0;
    buf->max_capacity = 0;
}

bool byte_buffer_reserve(byte_buffer_t *buf, size_t additional) {
    if (!buf || !buf->data) return false;

    size_t needed = buf->size + additional;
    if (needed <= buf->capacity) return true;
    if (needed > buf->max_capacity) return false;

    size_t new_cap = buf->capacity * 2;
    if (new_cap < needed) new_cap = needed;
    if (new_cap > buf->max_capacity) new_cap = buf->max_capacity;

    uint8_t *new_data = (uint8_t *)realloc(buf->data, new_cap);
    if (!new_data) return false;

    buf->data = new_data;
    buf->capacity = new_cap;
    return true;
}

bool byte_buffer_append(byte_buffer_t *buf, const uint8_t *src, size_t len) {
    if (!buf || !src || len == 0) return false;

    if (!byte_buffer_reserve(buf, len)) {
        return false;
    }

    memcpy(buf->data + buf->size, src, len);
    buf->size += len;
    return true;
}

void byte_buffer_drain(byte_buffer_t *buf, size_t count) {
    if (!buf || count == 0) return;

    if (count >= buf->size) {
        buf->size = 0;
        return;
    }

    size_t remaining = buf->size - count;
    memmove(buf->data, buf->data + count, remaining);
    buf->size = remaining;
}

void byte_buffer_clear(byte_buffer_t *buf) {
    if (!buf) return;
    buf->size = 0;
}
