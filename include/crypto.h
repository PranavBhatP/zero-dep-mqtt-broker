#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file crypto.h
 * @brief Zero-dependency cryptographic primitives:
 *        - ChaCha20 stream cipher for payload encryption/decryption
 *        - SHA-256 and HMAC-SHA256 for message integrity verification
 * 
 * Course: IT365 Advanced Computer Networks
 * Project: Zero-Dependency MQTT Broker
 */

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32
#define CHACHA20_KEY_SIZE  32
#define CHACHA20_NONCE_SIZE 12

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t out[SHA256_DIGEST_SIZE]);

/**
 * ChaCha20 stream encryption/decryption (symmetric stream XOR)
 */
void chacha20_crypt(const uint8_t key[CHACHA20_KEY_SIZE],
                    const uint8_t nonce[CHACHA20_NONCE_SIZE],
                    uint32_t counter,
                    const uint8_t *in,
                    uint8_t *out,
                    size_t len);

/**
 * Helper to encrypt an MQTT payload: prepends 12-byte nonce, encrypts payload with ChaCha20,
 * and appends 16-byte HMAC-SHA256 tag.
 * Out buffer must be at least in_len + 28 bytes.
 */
size_t crypto_encrypt_payload(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_max_len);

/**
 * Helper to decrypt and verify an MQTT payload.
 * Out buffer must be at least in_len - 28 bytes.
 * Returns decrypted length, or 0 if verification failed.
 */
size_t crypto_decrypt_payload(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_max_len);

#endif /* CRYPTO_H */

