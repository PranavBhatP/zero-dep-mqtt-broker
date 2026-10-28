#include "crypto.h"
#include <string.h>
#include <stdlib.h>

/* ========================================================================= */
/* SHA-256 (RFC 6234)                                                        */
/* ========================================================================= */

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIG0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIG1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define OM0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define OM1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];

    for (int i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    for (int i = 16; i < 64; ++i)
        m[i] = OM1(m[i - 2]) + m[i - 7] + OM0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        t1 = h + SIG1(e) + CH(e, f, g) + K[i] + m[i];
        t2 = SIG0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    size_t i = (size_t)(ctx->count % 64);
    ctx->count += len;
    while (len > 0) {
        size_t part = 64 - i;
        if (len < part) part = len;
        memcpy(&ctx->buf[i], data, part);
        i = (i + part) % 64;
        data += part;
        len -= part;
        if (i == 0) sha256_transform(ctx, ctx->buf);
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]) {
    uint8_t pad[64] = { 0x80 };
    uint64_t bits = ctx->count * 8;
    uint8_t len_bytes[8];
    for (int i = 0; i < 8; i++) {
        len_bytes[7 - i] = (uint8_t)(bits >> (i * 8));
    }

    size_t pad_len = (ctx->count % 64 < 56) ? (56 - ctx->count % 64) : (120 - ctx->count % 64);
    sha256_update(ctx, pad, pad_len);
    sha256_update(ctx, len_bytes, 8);

    for (int i = 0; i < 8; i++) {
        digest[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

void sha256(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t out[SHA256_DIGEST_SIZE]) {
    uint8_t k_pad[SHA256_BLOCK_SIZE] = {0};
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, k_pad);
    } else {
        memcpy(k_pad, key, key_len);
    }

    uint8_t i_key_pad[SHA256_BLOCK_SIZE];
    uint8_t o_key_pad[SHA256_BLOCK_SIZE];
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        i_key_pad[i] = k_pad[i] ^ 0x36;
        o_key_pad[i] = k_pad[i] ^ 0x5c;
    }

    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, i_key_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, o_key_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out);
}

/* ========================================================================= */
/* ChaCha20 (RFC 7539 / RFC 8439)                                            */
/* ========================================================================= */

#define CHACHA_ROTL(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define CHACHA_QR(a, b, c, d) \
    a += b; d ^= a; d = CHACHA_ROTL(d, 16); \
    c += d; b ^= c; b = CHACHA_ROTL(b, 12); \
    a += b; d ^= a; d = CHACHA_ROTL(d, 8);  \
    c += d; b ^= c; b = CHACHA_ROTL(b, 7);

static void chacha20_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    memcpy(x, in, sizeof(x));

    for (int i = 0; i < 10; i++) {
        /* Column round */
        CHACHA_QR(x[0], x[4], x[8],  x[12]);
        CHACHA_QR(x[1], x[5], x[9],  x[13]);
        CHACHA_QR(x[2], x[6], x[10], x[14]);
        CHACHA_QR(x[3], x[7], x[11], x[15]);
        /* Diagonal round */
        CHACHA_QR(x[0], x[5], x[10], x[15]);
        CHACHA_QR(x[1], x[6], x[11], x[12]);
        CHACHA_QR(x[2], x[7], x[8],  x[13]);
        CHACHA_QR(x[3], x[4], x[9],  x[14]);
    }

    for (int i = 0; i < 16; i++) {
        uint32_t val = x[i] + in[i];
        out[i * 4]     = (uint8_t)(val & 0xFF);
        out[i * 4 + 1] = (uint8_t)((val >> 8) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((val >> 16) & 0xFF);
        out[i * 4 + 3] = (uint8_t)((val >> 24) & 0xFF);
    }
}

void chacha20_crypt(const uint8_t key[CHACHA20_KEY_SIZE],
                    const uint8_t nonce[CHACHA20_NONCE_SIZE],
                    uint32_t counter,
                    const uint8_t *in,
                    uint8_t *out,
                    size_t len) {
    uint32_t state[16];
    /* Constants "expand 32-byte k" */
    state[0] = 0x61707865;
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;

    /* Key */
    for (int i = 0; i < 8; i++) {
        state[4 + i] = ((uint32_t)key[i * 4]) |
                       (((uint32_t)key[i * 4 + 1]) << 8) |
                       (((uint32_t)key[i * 4 + 2]) << 16) |
                       (((uint32_t)key[i * 4 + 3]) << 24);
    }

    /* Counter */
    state[12] = counter;

    /* Nonce */
    for (int i = 0; i < 3; i++) {
        state[13 + i] = ((uint32_t)nonce[i * 4]) |
                        (((uint32_t)nonce[i * 4 + 1]) << 8) |
                        (((uint32_t)nonce[i * 4 + 2]) << 16) |
                        (((uint32_t)nonce[i * 4 + 3]) << 24);
    }

    uint8_t block[64];
    size_t offset = 0;

    while (offset < len) {
        chacha20_block(state, block);
        state[12]++;

        size_t block_len = len - offset;
        if (block_len > 64) block_len = 64;

        for (size_t i = 0; i < block_len; i++) {
            out[offset + i] = in[offset + i] ^ block[i];
        }
        offset += block_len;
    }
}

size_t crypto_encrypt_payload(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_max_len) {
    /* Wire layout: [12 bytes Nonce] [in_len bytes Ciphertext] [16 bytes HMAC Tag] */
    size_t needed = CHACHA20_NONCE_SIZE + in_len + 16;
    if (out_max_len < needed) return 0;

    /* Generate pseudo-random nonce */
    uint8_t nonce[CHACHA20_NONCE_SIZE];
    for (size_t i = 0; i < CHACHA20_NONCE_SIZE; i++) {
        nonce[i] = (uint8_t)(rand() & 0xFF);
    }
    memcpy(out, nonce, CHACHA20_NONCE_SIZE);

    /* Encrypt in-place into out buffer */
    chacha20_crypt(key, nonce, 1, in, out + CHACHA20_NONCE_SIZE, in_len);

    /* Calculate HMAC over Nonce + Ciphertext */
    uint8_t hmac[SHA256_DIGEST_SIZE];
    hmac_sha256(key, CHACHA20_KEY_SIZE, out, CHACHA20_NONCE_SIZE + in_len, hmac);

    /* Append first 16 bytes of HMAC as tag */
    memcpy(out + CHACHA20_NONCE_SIZE + in_len, hmac, 16);

    return needed;
}

size_t crypto_decrypt_payload(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_max_len) {
    if (in_len < CHACHA20_NONCE_SIZE + 16) return 0;
    size_t ciphertext_len = in_len - CHACHA20_NONCE_SIZE - 16;
    if (out_max_len < ciphertext_len) return 0;

    const uint8_t *nonce = in;
    const uint8_t *ciphertext = in + CHACHA20_NONCE_SIZE;
    const uint8_t *tag = in + CHACHA20_NONCE_SIZE + ciphertext_len;

    /* Verify HMAC */
    uint8_t hmac[SHA256_DIGEST_SIZE];
    hmac_sha256(key, CHACHA20_KEY_SIZE, in, CHACHA20_NONCE_SIZE + ciphertext_len, hmac);

    /* Constant-time tag comparison */
    int diff = 0;
    for (int i = 0; i < 16; i++) {
        diff |= (tag[i] ^ hmac[i]);
    }
    if (diff != 0) {
        return 0; /* Authentication failed or payload tampered */
    }

    /* Decrypt ciphertext */
    chacha20_crypt(key, nonce, 1, ciphertext, out, ciphertext_len);
    return ciphertext_len;
}
