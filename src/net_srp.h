/* --- net_srp.h ---
 * One job: SRP-6a client-side math for logging into an existing Luanti
 * account (AUTH_MECHANISM_SRP path -- not the first-registration
 * FIRST_SRP path, which just uploads a verifier and never runs this).
 *
 * Recreated directly from util/srp.{h,cpp} in the Luanti source tree
 * (their fork of csrp), using GMP instead of csrp's own bignum shim since
 * GMP is already vendored in third_party/gmp. Verified against upstream
 * byte-for-byte for: the 2048-bit RFC5054 N/g group, k = H(N | pad(g)),
 * x = H(s | H(lower(I) ":" P)), u = H(pad(A) | pad(B)), S, K = H(S),
 * and M = H( H(N)^H(g) | H(I) | s | A | B | K ). Luanti's client never
 * checks the server's H(A|M|K) proof back -- it just waits for
 * TOCLIENT_AUTH_ACCEPT -- so this doesn't implement that check either.
 *
 * Uses GMP (mpz_t) directly; requires -lgmp (or the vendored
 * third_party/gmp target) and SHA-256 (reuses the project's existing
 * from-scratch, NIST-verified implementation -- see net_srp.c's #include).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define NET_SRP_HASH_LEN 32u /* SHA-256 */

typedef struct NetSrpClient NetSrpClient;

/* `username` is used case-preserved for the M-transcript hash (I);
 * lowercased internally for the x derivation, matching upstream exactly.
 * `password` is the raw account password bytes -- never hashed by the
 * caller; based_on=1 (password-based) is always what this recreation
 * sends, matching the AUTH_MECHANISM_SRP branch (not LEGACY_PASSWORD). */
NetSrpClient *net_srp_client_new(const char *username, const char *password);
void net_srp_client_free(NetSrpClient *c);

/* Step 1: generates a random private exponent `a`, computes A = g^a mod N.
 * `out_A`/`out_A_len` point into memory owned by `c`, valid until free. */
void net_srp_client_start(NetSrpClient *c, const uint8_t **out_A, size_t *out_A_len);

/* Step 2: given the server's salt `s` and public value `B` (both from
 * TOCLIENT_SRP_BYTES_S_B), computes the shared key and the client proof M
 * to send back in TOSERVER_SRP_BYTES_M. Returns false if the SRP-6a safety
 * check fails (B == 0 mod N, or u == 0) -- treat that as a hard auth
 * failure, not something to retry. */
bool net_srp_client_process_challenge(NetSrpClient *c,
                                       const uint8_t *salt, size_t salt_len,
                                       const uint8_t *B, size_t B_len,
                                       const uint8_t **out_M, size_t *out_M_len);
