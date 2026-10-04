/* --- sha1.h ---
 * One job: FIPS 180-4 SHA-1. Written from scratch against the published
 * spec (no OpenSSL/BoringSSL-derived code, so no advertising-clause
 * obligations to track). Streaming API: init -> update* -> final.
 *
 * SHA-1 is cryptographically broken (chosen-prefix collisions are
 * practical) -- this exists ONLY because the Luanti wire protocol commits
 * to it for media integrity checking (TOCLIENT_ANNOUNCE_MEDIA's 20-byte
 * hashes, TOCLIENT_MEDIA's checkAndLoad()). It is not a security boundary:
 * a malicious server can already serve you anything it wants over this
 * same connection. Do not reach for this anywhere else -- use sha256.c.
 *
 * Test against the standard NIST test vectors ("abc", empty string,
 * "abc"*many for the million-a test) before trusting it against real
 * server media.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[5];
    uint64_t bitlen;
    uint8_t  buffer[64];
    size_t   buffer_len;
} Sha1Ctx;

void sha1_init(Sha1Ctx *ctx);
void sha1_update(Sha1Ctx *ctx, const uint8_t *data, size_t len);
void sha1_final(Sha1Ctx *ctx, uint8_t out[20]);

/* Convenience one-shot. */
void sha1(const uint8_t *data, size_t len, uint8_t out[20]);
