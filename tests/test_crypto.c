#include "crypto.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void test_sha256(void) {
    printf("[TEST] Testing SHA-256 standard vectors...\n");
    /* SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    uint8_t d[32];
    sha256((const uint8_t *)"", 0, d);
    assert(d[0] == 0xe3 && d[1] == 0xb0 && d[31] == 0x55);

    /* SHA256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad */
    sha256((const uint8_t *)"abc", 3, d);
    assert(d[0] == 0xba && d[1] == 0x78 && d[31] == 0xad);
    printf("  -> PASS: SHA-256 test vectors\n");
}

static void test_chacha20_roundtrip(void) {
    printf("[TEST] Testing ChaCha20 encryption & decryption...\n");
    uint8_t key[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                       17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
    uint8_t nonce[12] = {0x42, 0x13, 0x37, 0, 0, 0, 0, 0, 0, 0, 0, 1};

    const char *plaintext = "Secret IoT Telemetry Data: Temp=24.5C, Voltage=3.3V, Status=OK";
    size_t len = strlen(plaintext);

    uint8_t ciphertext[128];
    uint8_t decrypted[128];

    chacha20_crypt(key, nonce, 1, (const uint8_t *)plaintext, ciphertext, len);
    /* Ciphertext should not match plaintext */
    assert(memcmp(plaintext, ciphertext, len) != 0);

    chacha20_crypt(key, nonce, 1, ciphertext, decrypted, len);
    decrypted[len] = '\0';
    assert(strcmp(plaintext, (char *)decrypted) == 0);
    printf("  -> PASS: ChaCha20 stream roundtrip\n");
}

static void test_authenticated_payload_encryption(void) {
    printf("[TEST] Testing Authenticated Payload Encryption & Tampering Check...\n");
    uint8_t key[32] = "acn-iot-network-master-secret-32";
    const char *msg = "CRITICAL_GRID_CONTROL: OPEN_VALVE_5";
    size_t len = strlen(msg);

    uint8_t enc_buf[256];
    uint8_t dec_buf[256];

    size_t enc_len = crypto_encrypt_payload(key, (const uint8_t *)msg, len, enc_buf, sizeof(enc_buf));
    assert(enc_len == len + 12 + 16);

    /* Normal decryption */
    size_t dec_len = crypto_decrypt_payload(key, enc_buf, enc_len, dec_buf, sizeof(dec_buf));
    assert(dec_len == len);
    dec_buf[dec_len] = '\0';
    assert(strcmp(msg, (char *)dec_buf) == 0);

    /* Tamper with ciphertext */
    enc_buf[15] ^= 0xFF;
    size_t tampered_len = crypto_decrypt_payload(key, enc_buf, enc_len, dec_buf, sizeof(dec_buf));
    assert(tampered_len == 0); /* Tampering rejected! */

    printf("  -> PASS: Authenticated payload encryption & tamper protection\n");
}

int main(void) {
    printf("========================================\n");
    printf("RUNNING CRYPTOGRAPHY TESTS\n");
    printf("========================================\n");

    test_sha256();
    test_chacha20_roundtrip();
    test_authenticated_payload_encryption();

    printf("ALL CRYPTOGRAPHY TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
