#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file buffer.h
 * @brief Dynamic byte accumulation buffer for TCP stream framing and defragmentation
 */

typedef struct {
    uint8_t *data;
    size_t   size;         /* Current number of valid bytes */
    size_t   capacity;     /* Allocated memory capacity */
    size_t   max_capacity; /* Hard ceiling to prevent DoS memory exhaustion */
} byte_buffer_t;

/**
 * Initialize buffer with initial and maximum capacity
 */
bool byte_buffer_init(byte_buffer_t *buf, size_t initial_capacity, size_t max_capacity);

/**
 * Free all memory associated with buffer
 */
void byte_buffer_free(byte_buffer_t *buf);

/**
 * Append raw bytes to the end of the buffer
 */
bool byte_buffer_append(byte_buffer_t *buf, const uint8_t *src, size_t len);

/**
 * Drain (consume) the first `count` bytes from the buffer, shifting remaining bytes down
 */
void byte_buffer_drain(byte_buffer_t *buf, size_t count);

/**
 * Clear the buffer without freeing memory
 */
void byte_buffer_clear(byte_buffer_t *buf);

/**
 * Ensure buffer has enough free space to append `additional` bytes
 */
bool byte_buffer_reserve(byte_buffer_t *buf, size_t additional);

#endif /* BUFFER_H */
