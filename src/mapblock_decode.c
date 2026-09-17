#include "mapblock_decode.h"
#include "net_serialize.h"

#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>

#define MAPBLOCK_NODECOUNT (16 * 16 * 16) /* 4096 */

/* Generous ceiling for one mapblock's decompressed content: bulk node
 * data alone is 4096 * (2-byte content_id + 1-byte param1 + 1-byte
 * param2) = 16384 bytes; the rest (flags/lighting/widths header, node
 * metadata) is normally tiny but node metadata is user-generated content
 * (chest inventories, sign text) with no hard protocol cap, so this is a
 * deliberately generous fixed ceiling rather than a tight guess -- raise
 * it if a real block ever needs more, per this project's
 * hard-fail-over-silent-fallback convention. */
#define MAPBLOCK_DECOMPRESSED_CEILING (256u * 1024u)

/* This decoder only implements the >=29 (zstd-compressed) wire path --
 * see mapblock_decode.h. */
#define MIN_SUPPORTED_SER_VER 29u

/* Mirrors decompressZstd() in Luanti's own serialization.cpp exactly
 * (same ZSTD_DStream/ZSTD_decompressStream loop), rather than a one-shot
 * ZSTD_decompress(): the input can have trailing bytes after the frame
 * ends (deSerializeNetworkSpecific's one reserved byte), so this needs to
 * know precisely how many input bytes the frame itself consumed, not
 * just decompress "the whole buffer" as a single unit. Returns the
 * decompressed length; hard-fails on any zstd error or on genuinely
 * running out of output space (distinct from normal frame completion). */
static size_t decompress_zstd_stream(const uint8_t *src, size_t src_len,
                                      uint8_t *dst, size_t dst_cap) {
    ZSTD_DStream *stream = ZSTD_createDStream();
    if (!stream) {
        fprintf(stderr, "mapblock_decode: ZSTD_createDStream failed\n");
        exit(1);
    }
    ZSTD_initDStream(stream);

    ZSTD_inBuffer in = { src, src_len, 0 };
    ZSTD_outBuffer out = { dst, dst_cap, 0 };

    size_t ret;
    do {
        ret = ZSTD_decompressStream(stream, &out, &in);
        if (ZSTD_isError(ret)) {
            fprintf(stderr, "mapblock_decode: zstd decompress failed: %s\n",
                    ZSTD_getErrorName(ret));
            ZSTD_freeDStream(stream);
            exit(1);
        }
        if (ret != 0 && out.pos == out.size) {
            /* Not done, but no room left -- looping again would just call
             * ZSTD_decompressStream against an already-full output buffer
             * forever. This means the real block exceeded our ceiling. */
            fprintf(stderr, "mapblock_decode: decompressed block exceeds %zu-byte "
                    "ceiling -- raise MAPBLOCK_DECOMPRESSED_CEILING\n", dst_cap);
            ZSTD_freeDStream(stream);
            exit(1);
        }
    } while (ret != 0);

    ZSTD_freeDStream(stream);
    return out.pos;
}

void mapblock_decode_network(const uint8_t *payload, size_t len, uint8_t server_ser_ver,
                              ChunkCoord *out_coord, Mapblock *out_block) {
    if (server_ser_ver < MIN_SUPPORTED_SER_VER) {
        fprintf(stderr, "mapblock_decode: server serialization version %u predates the "
                "only wire format this decoder implements (>=%u, zstd) -- see "
                "MapBlock::deSerialize's pre-29 branch upstream if this ever needs "
                "supporting\n", (unsigned)server_ser_ver, MIN_SUPPORTED_SER_VER);
        exit(1);
    }

    if (len < 6) {
        fprintf(stderr, "mapblock_decode: payload (%zu bytes) shorter than its own "
                "v3s16 position\n", len);
        exit(1);
    }

    NetReader r;
    net_reader_init(&r, payload, len);
    out_coord->x = net_get_s16(&r);
    out_coord->y = net_get_s16(&r);
    out_coord->z = net_get_s16(&r);

    const uint8_t *compressed = payload + 6;
    size_t compressed_len = len - 6;

    /* static, not stack-allocated: 256 KiB per call would be a lot to put
     * on the stack repeatedly. This makes mapblock_decode_network() NOT
     * thread-safe/reentrant -- fine for now (net_poll() is called from
     * the single main loop thread, nothing else touches this), but this
     * needs a real per-call or per-thread buffer the moment Phase 4's
     * multithreaded work (if it ever parallelizes block decoding
     * specifically, not just meshing) enters the picture. */
    static uint8_t decompressed[MAPBLOCK_DECOMPRESSED_CEILING];
    size_t decompressed_len = decompress_zstd_stream(compressed, compressed_len,
                                                       decompressed, sizeof(decompressed));

    NetReader dr;
    net_reader_init(&dr, decompressed, decompressed_len);

    net_get_u8(&dr);  /* flags (is_underground bit) -- not needed yet */
    net_get_u16(&dr); /* lighting_complete -- not needed yet */

    uint8_t content_width = net_get_u8(&dr);
    uint8_t params_width = net_get_u8(&dr);
    if (content_width != 1 && content_width != 2) {
        fprintf(stderr, "mapblock_decode: invalid content_width %u (must be 1 or 2)\n",
                (unsigned)content_width);
        exit(1);
    }
    if (params_width != 2) {
        fprintf(stderr, "mapblock_decode: invalid params_width %u (must be 2)\n",
                (unsigned)params_width);
        exit(1);
    }

    *out_block = mapblock_make_full();

    /* Struct-of-arrays layout, NOT interleaved per-node -- confirmed
     * against MapNode::deSerializeBulk upstream: every node's content_id
     * first, then every node's param1, then every node's param2. Getting
     * this backwards wouldn't crash -- it would silently scramble
     * param1/param2 across unrelated node positions. Wire order
     * (z*16*16 + y*16 + x) matches Node[] order exactly per chunk.h's own
     * comment, so each section reads straight into out_block->nodes[i]
     * with no (x,y,z) reshaping needed. */
    if (content_width == 1) {
        uint8_t param0[MAPBLOCK_NODECOUNT];
        for (int i = 0; i < MAPBLOCK_NODECOUNT; i++)
            param0[i] = net_get_u8(&dr);
        uint8_t param1[MAPBLOCK_NODECOUNT];
        for (int i = 0; i < MAPBLOCK_NODECOUNT; i++)
            param1[i] = net_get_u8(&dr);
        for (int i = 0; i < MAPBLOCK_NODECOUNT; i++) {
            uint8_t param2 = net_get_u8(&dr);
            uint16_t content_id = param0[i];
            /* Legacy 12-bit-content-id-in-1-byte extension trick. Still
             * part of the spec, even though real content_width==1 traffic
             * is essentially extinct with any modern content-heavy game
             * (255 distinct content IDs isn't enough for one). */
            if (content_id > 0x7F) {
                content_id <<= 4;
                content_id |= (param2 & 0xF0) >> 4;
                param2 &= 0x0F;
            }
            out_block->nodes[i].content_id = content_id;
            out_block->nodes[i].param1 = param1[i];
            out_block->nodes[i].param2 = param2;
        }
    } else { /* content_width == 2 -- the only path any modern game actually uses */
        for (int i = 0; i < MAPBLOCK_NODECOUNT; i++)
            out_block->nodes[i].content_id = net_get_u16(&dr);
        for (int i = 0; i < MAPBLOCK_NODECOUNT; i++)
            out_block->nodes[i].param1 = net_get_u8(&dr);
        for (int i = 0; i < MAPBLOCK_NODECOUNT; i++)
            out_block->nodes[i].param2 = net_get_u8(&dr);
    }

    /* Node metadata follows in the decompressed stream, but it's the last
     * field in the network (disk=false) format -- nothing after it that
     * we need, so deliberately not parsed. See mapblock_decode.h. */
}
