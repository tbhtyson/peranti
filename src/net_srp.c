/* --- net_srp.c ---
 * See net_srp.h for provenance notes. Requires GMP (mpz_t) and this
 * project's sha256.h.
 */
#define _POSIX_C_SOURCE 200809L /* strdup */

#include "net_srp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <gmp.h>

#include "sha256.h"

/* RFC 5054 Appendix A, 2048-bit group. */
static const char *SRP_N_HEX =
    "AC6BDB41324A9A9BF166DE5E1389582FAF72B6651987EE07FC319294"
    "3DB56050A37329CBB4A099ED8193E0757767A13DD52312AB4B03310D"
    "CD7F48A9DA04FD50E8083969EDB767B0CF6095179A163AB3661A05FB"
    "D5FAAAE82918A9962F0B93B855F97993EC975EEAA80D740ADBF4FF74"
    "7359D041D5C33EA71D281E446B14773BCA97B43A23FB801676BD207A"
    "436C6481F1D2B9078717461A5B9D32E688F87748544523B524B0D57D"
    "5EA77A2775D2ECFA032CFBDBF52FB3786160279004E57AE6AF874E73"
    "03CE53299CCC041C7BC308D82A5698F3A8D0C38271AE35F8E9DBFBB6"
    "94B5C803D89F7AE435DE236D525F54759B65E372FCD68EF20FA7111F"
    "9E4AFF73";
static const char *SRP_G_HEX = "2";

struct NetSrpClient {
    mpz_t N, g;
    mpz_t a, A;
    mpz_t S;

    char *username;       /* case-preserved, used for H(I) in M */
    char *username_lower;  /* used for x = H(s | H(I_lower ":" P)) */
    uint8_t *password;
    size_t password_len;

    uint8_t A_bytes[512]; /* 2048-bit N -> A fits in 256 bytes; headroom kept */
    size_t A_len;

    uint8_t M[NET_SRP_HASH_LEN];
    uint8_t session_key[NET_SRP_HASH_LEN];
};

static void mpz_to_bytes(const mpz_t v, uint8_t *out, size_t *out_len) {
    size_t count = 0;
    mpz_export(out, &count, 1, 1, 1, 0, v);
    *out_len = count;
}

static void mpz_from_bytes(mpz_t v, const uint8_t *data, size_t len) {
    mpz_import(v, len, 1, 1, 1, 0, data);
}

static size_t mpz_byte_len(const mpz_t v) {
    return (mpz_sizeinbase(v, 2) + 7) / 8;
}

/* H_nn: zero-pad n1 and n2 to the byte length of N, concatenate, SHA-256,
 * reinterpret digest as a big integer. Used for both k = H_nn(N,N,g) and
 * u = H_nn(N,A,B). */
static void H_nn(mpz_t result, const mpz_t N, const mpz_t n1, const mpz_t n2) {
    size_t len_N = mpz_byte_len(N);
    size_t len_n1 = mpz_byte_len(n1);
    size_t len_n2 = mpz_byte_len(n2);
    if (len_n1 > len_N || len_n2 > len_N) {
        fprintf(stderr, "net_srp: H_nn operand longer than N -- corrupt server value\n");
        exit(1);
    }

    uint8_t *buf = calloc(len_N * 2, 1);
    if (!buf) { fprintf(stderr, "net_srp: OOM\n"); exit(1); }

    size_t n1_written = 0, n2_written = 0;
    mpz_export(buf + (len_N - len_n1), &n1_written, 1, 1, 1, 0, n1);
    mpz_export(buf + (len_N * 2 - len_n2), &n2_written, 1, 1, 1, 0, n2);

    uint8_t digest[32];
    sha256(buf, len_N * 2, digest);
    free(buf);

    mpz_from_bytes(result, digest, sizeof(digest));
}

/* x = H(s | H(lower(I) ":" P)) */
static void calculate_x(mpz_t x, const uint8_t *salt, size_t salt_len,
                         const char *username_lower, const uint8_t *password, size_t password_len) {
    Sha256Ctx ctx;
    uint8_t inner[32];
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)username_lower, strlen(username_lower));
    sha256_update(&ctx, (const uint8_t *)":", 1);
    sha256_update(&ctx, password, password_len);
    sha256_final(&ctx, inner);

    uint8_t *buf = malloc(salt_len + sizeof(inner));
    if (!buf) { fprintf(stderr, "net_srp: OOM\n"); exit(1); }
    memcpy(buf, salt, salt_len);
    memcpy(buf + salt_len, inner, sizeof(inner));

    uint8_t digest[32];
    sha256(buf, salt_len + sizeof(inner), digest);
    free(buf);

    mpz_from_bytes(x, digest, sizeof(digest));
}

/* M = H( H(N) xor H(g) | H(I) | s | A | B | K ) */
static void calculate_M(uint8_t out[32], const mpz_t N, const mpz_t g,
                         const char *username, const uint8_t *salt, size_t salt_len,
                         const mpz_t A, const mpz_t B, const uint8_t K[32]) {
    uint8_t N_bytes[256], g_bytes[256];
    size_t N_len, g_len;
    mpz_to_bytes(N, N_bytes, &N_len);
    mpz_to_bytes(g, g_bytes, &g_len);

    uint8_t H_N[32], H_g[32], H_I[32], H_xor[32];
    sha256(N_bytes, N_len, H_N);
    sha256(g_bytes, g_len, H_g);
    sha256((const uint8_t *)username, strlen(username), H_I);
    for (int i = 0; i < 32; ++i)
        H_xor[i] = H_N[i] ^ H_g[i];

    uint8_t A_bytes[256], B_bytes[256];
    size_t A_len, B_len;
    mpz_to_bytes(A, A_bytes, &A_len);
    mpz_to_bytes(B, B_bytes, &B_len);

    Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, H_xor, sizeof(H_xor));
    sha256_update(&ctx, H_I, sizeof(H_I));
    sha256_update(&ctx, salt, salt_len);
    sha256_update(&ctx, A_bytes, A_len);
    sha256_update(&ctx, B_bytes, B_len);
    sha256_update(&ctx, K, 32);
    sha256_final(&ctx, out);
}

static char *dup_lowercase(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (!out) { fprintf(stderr, "net_srp: OOM\n"); exit(1); }
    for (size_t i = 0; i < len; ++i)
        out[i] = (char)tolower((unsigned char)s[i]);
    out[len] = '\0';
    return out;
}

NetSrpClient *net_srp_client_new(const char *username, const char *password) {
    NetSrpClient *c = calloc(1, sizeof(NetSrpClient));
    if (!c) { fprintf(stderr, "net_srp: OOM\n"); exit(1); }

    mpz_init_set_str(c->N, SRP_N_HEX, 16);
    mpz_init_set_str(c->g, SRP_G_HEX, 16);
    mpz_init(c->a);
    mpz_init(c->A);
    mpz_init(c->S);

    c->username = strdup(username);
    c->username_lower = dup_lowercase(username);
    c->password_len = strlen(password);
    c->password = malloc(c->password_len);
    memcpy(c->password, password, c->password_len);

    return c;
}

void net_srp_client_free(NetSrpClient *c) {
    if (!c) return;
    mpz_clear(c->N); mpz_clear(c->g);
    mpz_clear(c->a); mpz_clear(c->A); mpz_clear(c->S);
    free(c->username);
    free(c->username_lower);
    if (c->password) {
        memset(c->password, 0, c->password_len);
        free(c->password);
    }
    memset(c, 0, sizeof(*c));
    free(c);
}

void net_srp_client_start(NetSrpClient *c, const uint8_t **out_A, size_t *out_A_len) {
    /* Private exponent `a`: 256 bits of CSPRNG-quality randomness is the
     * conventional choice for a 2048-bit SRP group. Seed GMP's RNG from
     * /dev/urandom rather than trusting its default seeding. */
    gmp_randstate_t rng;
    gmp_randinit_default(rng);

    FILE *urandom = fopen("/dev/urandom", "rb");
    if (!urandom) {
        fprintf(stderr, "net_srp: cannot open /dev/urandom for seeding\n");
        exit(1);
    }
    unsigned long seed_bytes[4];
    if (fread(seed_bytes, sizeof(seed_bytes), 1, urandom) != 1) {
        fprintf(stderr, "net_srp: short read from /dev/urandom\n");
        fclose(urandom);
        exit(1);
    }
    fclose(urandom);
    mpz_t seed;
    mpz_init(seed);
    mpz_import(seed, 4, 1, sizeof(unsigned long), 0, 0, seed_bytes);
    gmp_randseed(rng, seed);
    mpz_clear(seed);

    mpz_urandomb(c->a, rng, 256);
    gmp_randclear(rng);

    mpz_powm(c->A, c->g, c->a, c->N);

    mpz_to_bytes(c->A, c->A_bytes, &c->A_len);
    *out_A = c->A_bytes;
    *out_A_len = c->A_len;
}

bool net_srp_client_process_challenge(NetSrpClient *c,
                                       const uint8_t *salt, size_t salt_len,
                                       const uint8_t *B_bytes, size_t B_len,
                                       const uint8_t **out_M, size_t *out_M_len) {
    mpz_t B, u, x, k, v, tmp1, tmp2, tmp3, tmp4;
    mpz_init(B); mpz_init(u); mpz_init(x); mpz_init(k); mpz_init(v);
    mpz_init(tmp1); mpz_init(tmp2); mpz_init(tmp3); mpz_init(tmp4);

    mpz_from_bytes(B, B_bytes, B_len);

    H_nn(u, c->N, c->A, B);
    calculate_x(x, salt, salt_len, c->username_lower, c->password, c->password_len);
    H_nn(k, c->N, c->N, c->g);

    bool ok = (mpz_sgn(B) != 0) && (mpz_sgn(u) != 0);
    if (ok) {
        /* v = g^x mod N (the verifier, recomputed client-side) */
        mpz_powm(v, c->g, x, c->N);

        /* S = (B - k*g^x)^(a + u*x) mod N */
        mpz_mul(tmp1, u, x);                       /* tmp1 = u*x */
        mpz_add(tmp2, c->a, tmp1);                 /* tmp2 = a + u*x */
        mpz_powm(tmp1, c->g, x, c->N);              /* tmp1 = g^x */
        mpz_mul(tmp3, k, tmp1);
        mpz_mod(tmp3, tmp3, c->N);                  /* tmp3 = k*g^x mod N */
        mpz_sub(tmp1, B, tmp3);
        mpz_mod(tmp1, tmp1, c->N);                  /* tmp1 = (B - k*g^x) mod N */
        mpz_powm(c->S, tmp1, tmp2, c->N);

        uint8_t S_bytes[256];
        size_t S_len;
        mpz_to_bytes(c->S, S_bytes, &S_len);
        sha256(S_bytes, S_len, c->session_key);

        calculate_M(c->M, c->N, c->g, c->username, salt, salt_len, c->A, B, c->session_key);

        *out_M = c->M;
        *out_M_len = sizeof(c->M);
    } else {
        *out_M = NULL;
        *out_M_len = 0;
    }

    mpz_clear(B); mpz_clear(u); mpz_clear(x); mpz_clear(k); mpz_clear(v);
    mpz_clear(tmp1); mpz_clear(tmp2); mpz_clear(tmp3); mpz_clear(tmp4);
    return ok;
}
