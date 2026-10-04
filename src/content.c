#include "content.h"
#include "zstd_stream.h"
#include "net_serialize.h"
#include "media_sync.h"

#include <stdio.h>
#include <stdlib.h>

/* Sourced against networkprotocol.h upstream, same convention as
 * init.c's own opcode #defines (see its comment on why the full
 * ToClientCommand enum doesn't live in net_types.h). */
#define TOCLIENT_NODEDEF 0x3au
#define TOCLIENT_ANNOUNCE_MEDIA 0x3cu
#define TOCLIENT_ITEMDEF 0x3du
#define TOCLIENT_MEDIA 0x38u

/* Client::handleCommand_{NodeDef,ItemDef,AnnounceMedia} upstream all
 * branch on m_proto_ver >= 48 for zstd vs. a zlib fallback
 * (decompressZlib). That fallback isn't implemented here -- hard-fail
 * per this project's convention rather than silently misreading an
 * older server's payload as zstd. */
#define CONTENT_MIN_PROTO_VER 48u

/* Ceiling for decompressed ITEMDEF/NODEDEF blobs and the ANNOUNCE_MEDIA
 * name table. A large modpack's nodedef can legitimately run into low
 * megabytes; this leaves an order of magnitude of headroom above that
 * without being unbounded (see zstd_stream.h on why this matters: these
 * payloads have no protocol-level cap the way a MapBlock does). Raise it
 * if a real server ever needs more -- this project hard-fails rather
 * than silently truncating, so you'll know immediately if it does. */
#define CONTENT_DECOMPRESSED_CEILING (16u * 1024u * 1024u)

/* Connection-lifetime state this module owns -- see content.h's doc
 * comment on content_reset()/content_get_nodedef()/content_get_itemdef().
 * Zero-initialized statics are already valid, empty tables (NodeDefTable
 * and ItemDefTable's all-zero state IS their "nothing parsed yet" state,
 * same as nodedef_table_init()/itemdef_table_init() produce), so there's
 * no separate "not connected" flag to track. */
static NodeDefTable g_nodedef;
static ItemDefTable g_itemdef;
static MediaSync g_media;
static bool g_tables_initialized = false;

static void ensure_tables_initialized(void) {
    if (g_tables_initialized)
        return;
    nodedef_table_init(&g_nodedef);
    itemdef_table_init(&g_itemdef);
    media_sync_init(&g_media);
    g_tables_initialized = true;
}

void content_reset(void) {
    if (!g_tables_initialized)
        return;
    nodedef_table_free(&g_nodedef);
    itemdef_table_free(&g_itemdef);
    media_sync_free(&g_media);
    g_tables_initialized = false;
}

const NodeDefTable *content_get_nodedef(void) {
    ensure_tables_initialized();
    return &g_nodedef;
}

const ItemDefTable *content_get_itemdef(void) {
    ensure_tables_initialized();
    return &g_itemdef;
}

const MediaSync *content_get_media_sync(void) {
    ensure_tables_initialized();
    return &g_media;
}

/* Decompresses one u32-length-prefixed ("long string") zstd blob at the
 * reader's current position and returns it (malloc'd, caller frees).
 * Shared by ITEMDEF, NODEDEF, and the ANNOUNCE_MEDIA name table -- all
 * three are this same shape at this point in their packet. */
static uint8_t *decompress_blob(const char *label, NetReader *r, size_t *out_len) {
    uint32_t compressed_len;
    const uint8_t *compressed = net_get_str32(r, &compressed_len);

    uint8_t *out;
    bool ok = zstd_stream_decompress(compressed, compressed_len,
                                      CONTENT_DECOMPRESSED_CEILING, &out, out_len);
    if (!ok) {
        fprintf(stderr, "[content] %s: decompressed size exceeds %u-byte ceiling -- "
                "raise CONTENT_DECOMPRESSED_CEILING if this is a real server, not an "
                "attacker\n", label, (unsigned)CONTENT_DECOMPRESSED_CEILING);
        exit(1);
    }
    return out;
}

bool content_on_packet(Net *net, uint16_t opcode, const uint8_t *payload, size_t len) {
    if (opcode != TOCLIENT_ITEMDEF && opcode != TOCLIENT_NODEDEF &&
        opcode != TOCLIENT_ANNOUNCE_MEDIA && opcode != TOCLIENT_MEDIA)
        return false;

    uint16_t proto_ver = net_get_proto_ver(net);
    if (proto_ver < CONTENT_MIN_PROTO_VER) {
        fprintf(stderr, "[content] server protocol %u predates zstd-compressed content "
                "defs (need >=%u) -- the zlib fallback isn't implemented here (see "
                "Client::handleCommand_NodeDef's decompressZlib branch upstream if this "
                "ever needs supporting)\n", (unsigned)proto_ver,
                (unsigned)CONTENT_MIN_PROTO_VER);
        exit(1);
    }

    NetReader r;
    net_reader_init(&r, payload, len);

    switch (opcode) {
    case TOCLIENT_ITEMDEF: {
        ensure_tables_initialized();
        size_t out_len;
        uint8_t *out = decompress_blob("TOCLIENT_ITEMDEF", &r, &out_len);
        itemdef_parse(&g_itemdef, out, out_len, proto_ver);
        printf("[content] TOCLIENT_ITEMDEF: parsed %zu items, %zu aliases\n",
               g_itemdef.item_count, g_itemdef.alias_count);
        free(out);
        return true;
    }

    case TOCLIENT_NODEDEF: {
        ensure_tables_initialized();
        size_t out_len;
        uint8_t *out = decompress_blob("TOCLIENT_NODEDEF", &r, &out_len);
        nodedef_parse(&g_nodedef, out, out_len);
        printf("[content] TOCLIENT_NODEDEF: parsed, table capacity now %zu\n",
               g_nodedef.capacity);
        free(out);
        return true;
    }

    case TOCLIENT_ANNOUNCE_MEDIA:
        ensure_tables_initialized();
        media_sync_on_announce(&g_media, net, &r);
        return true;

    case TOCLIENT_MEDIA:
        ensure_tables_initialized();
        media_sync_on_media(&g_media, net, &r);
        return true;

    default:
        return false; /* unreachable given the guard above */
    }
}
