/* --- sha256.h ---
 * One job: FIPS 180-4 SHA-256. Written from scratch against the published
 * spec (no OpenSSL/BoringSSL-derived code, so no advertising-clause
 * obligations to track). Streaming API: init -> update* -> final.
 *
 * Your original SHA-256 (noted as "NIST-verified") wasn't in the tarball,
 * so this is a fresh implementation -- test it against the standard NIST
 * test vectors ("abc", empty string, "abc"*many for the million-a test)
 * before trusting it for the SRP handshake.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[64];
    size_t   buffer_len;
} Sha256Ctx;

void sha256_init(Sha256Ctx *ctx);
void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(Sha256Ctx *ctx, uint8_t out[32]);

/* Convenience one-shot. */
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
