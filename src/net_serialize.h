/* --- net_serialize.h ---
 * One job: read/write big-endian primitives and length-prefixed strings
 * (Luanti's std::string wire format: u16 length prefix, no NUL) into a
 * fixed-capacity cursor buffer. Hard-fails on overflow/underflow rather
 * than silently truncating -- a malformed handshake is a bug or an
 * incompatible server, not something to paper over.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint8_t *data;
    size_t   capacity;
    size_t   pos;
} NetWriter;

typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         pos;
} NetReader;

static inline void net_writer_init(NetWriter *w, uint8_t *buf, size_t capacity) {
    w->data = buf;
    w->capacity = capacity;
    w->pos = 0;
}

static inline void net_reader_init(NetReader *r, const uint8_t *buf, size_t size) {
    r->data = buf;
    r->size = size;
    r->pos = 0;
}

static inline void net_write_require(const NetWriter *w, size_t n) {
    if (w->pos + n > w->capacity) {
        fprintf(stderr, "net_serialize: write of %zu bytes at pos %zu exceeds capacity %zu\n",
                n, w->pos, w->capacity);
        exit(1);
    }
}

static inline void net_read_require(const NetReader *r, size_t n) {
    if (r->pos + n > r->size) {
        fprintf(stderr, "net_serialize: read of %zu bytes at pos %zu exceeds size %zu "
                "(truncated or corrupt packet)\n", n, r->pos, r->size);
        exit(1);
    }
}

static inline void net_put_u8(NetWriter *w, uint8_t v) {
    net_write_require(w, 1);
    w->data[w->pos++] = v;
}

static inline void net_put_u16(NetWriter *w, uint16_t v) {
    net_write_require(w, 2);
    w->data[w->pos++] = (uint8_t)(v >> 8);
    w->data[w->pos++] = (uint8_t)(v & 0xFF);
}

static inline void net_put_u32(NetWriter *w, uint32_t v) {
    net_write_require(w, 4);
    w->data[w->pos++] = (uint8_t)(v >> 24);
    w->data[w->pos++] = (uint8_t)(v >> 16);
    w->data[w->pos++] = (uint8_t)(v >> 8);
    w->data[w->pos++] = (uint8_t)(v & 0xFF);
}

static inline void net_put_u64(NetWriter *w, uint64_t v) {
    net_write_require(w, 8);
    for (int i = 7; i >= 0; --i)
        w->data[w->pos++] = (uint8_t)(v >> (i * 8));
}

static inline void net_put_f32(NetWriter *w, float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    net_put_u32(w, bits);
}

static inline void net_put_bytes(NetWriter *w, const void *src, size_t n) {
    net_write_require(w, n);
    memcpy(w->data + w->pos, src, n);
    w->pos += n;
}

/* Luanti's std::string wire format: u16 length prefix + raw bytes, no NUL. */
static inline void net_put_str16(NetWriter *w, const char *s, size_t len) {
    if (len > 0xFFFFu) {
        fprintf(stderr, "net_serialize: string of length %zu exceeds u16 length prefix\n", len);
        exit(1);
    }
    net_put_u16(w, (uint16_t)len);
    net_put_bytes(w, s, len);
}

static inline uint8_t net_get_u8(NetReader *r) {
    net_read_require(r, 1);
    return r->data[r->pos++];
}

static inline uint16_t net_get_u16(NetReader *r) {
    net_read_require(r, 2);
    uint16_t v = (uint16_t)(r->data[r->pos] << 8 | r->data[r->pos + 1]);
    r->pos += 2;
    return v;
}

static inline uint32_t net_get_u32(NetReader *r) {
    net_read_require(r, 4);
    uint32_t v = ((uint32_t)r->data[r->pos] << 24) | ((uint32_t)r->data[r->pos + 1] << 16) |
                 ((uint32_t)r->data[r->pos + 2] << 8) | (uint32_t)r->data[r->pos + 3];
    r->pos += 4;
    return v;
}

/* Luanti's protocol has plenty of signed fixed-point fields (e.g.
 * TOCLIENT_MOVE_PLAYER's v3f1000 position). Two's complement reinterpret
 * of the u32 read -- universal in practice, and mandated outright as of
 * C23. */
static inline int32_t net_get_s32(NetReader *r) {
    return (int32_t)net_get_u32(r);
}

static inline void net_put_s32(NetWriter *w, int32_t v) {
    net_put_u32(w, (uint32_t)v);
}

/* Signed 16-bit -- e.g. v3s16 block coordinates in TOCLIENT_BLOCKDATA,
 * which do legitimately go negative in every axis. */
static inline int16_t net_get_s16(NetReader *r) {
    return (int16_t)net_get_u16(r);
}

static inline void net_put_s16(NetWriter *w, int16_t v) {
    net_put_u16(w, (uint16_t)v);
}

static inline uint64_t net_get_u64(NetReader *r) {
    net_read_require(r, 8);
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | r->data[r->pos + i];
    r->pos += 8;
    return v;
}

static inline float net_get_f32(NetReader *r) {
    uint32_t bits = net_get_u32(r);
    float v;
    memcpy(&v, &bits, 4);
    return v;
}

static inline void net_get_bytes(NetReader *r, void *dst, size_t n) {
    net_read_require(r, n);
    memcpy(dst, r->data + r->pos, n);
    r->pos += n;
}

/* Returns a pointer into the reader's own buffer plus length; caller must
 * copy out before the underlying datagram buffer is reused. */
static inline const uint8_t *net_get_str16(NetReader *r, uint16_t *out_len) {
    uint16_t len = net_get_u16(r);
    net_read_require(r, len);
    const uint8_t *p = r->data + r->pos;
    r->pos += len;
    *out_len = len;
    return p;
}

static inline size_t net_reader_remaining(const NetReader *r) {
    return r->size - r->pos;
}
