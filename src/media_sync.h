/* --- media_sync.h ---
 * Everything between "server announced these files exist" and "verified
 * bytes are sitting on disk": TOCLIENT_ANNOUNCE_MEDIA parsing, on-disk
 * cache lookup (ClientMediaDownloader::tryLoadFromCache's equivalent),
 * batched TOSERVER_REQUEST_MEDIA for whatever's missing, TOCLIENT_MEDIA
 * receipt, and SHA-1 verification (checkAndLoad's equivalent) before a
 * file is trusted and cached. Deliberately stops there -- parsing what a
 * texture *string* means (the ^/[ modifier grammar) and turning bytes
 * into a GPU texture are texture_name.c/texture_compose.c's job, not
 * this file's. Nothing here decodes PNG/model/sound contents; it only
 * gets verified raw bytes onto disk at a path keyed by their hash.
 *
 * One deliberate scope cut from upstream: no remote-media-server (HTTP)
 * support. Upstream's ClientMediaDownloader checks a libcurl-fetched
 * remote server first and falls back to the conventional protocol path
 * only for files that failed there (or when there are no remote servers,
 * or curl isn't compiled in). This always goes straight to the
 * conventional path -- cache check, then TOSERVER_REQUEST_MEDIA for
 * whatever's missing -- which is also exactly what upstream itself does
 * whenever remote servers are unavailable, so this is a strict subset of
 * upstream behavior, not a protocol deviation. The remote-server URL
 * list at the end of TOCLIENT_ANNOUNCE_MEDIA is still read (so the
 * packet reader ends up correctly positioned) but its contents are
 * discarded.
 *
 * The other deliberate deviation FROM upstream, forced by this project's
 * own architecture rather than a scope cut: upstream sends every missing
 * filename in ONE TOSERVER_REQUEST_MEDIA packet and relies on its own
 * outgoing packet splitter for anything too big for one UDP datagram.
 * This project's net_channel.c never splits outgoing packets (see its
 * scope note) and the outgoing-reliable window is only
 * NET_MAX_UNACKED_OUTGOING slots wide, so requests here are batched into
 * many datagram-sized packets and paced via
 * net_get_free_outgoing_slots() rather than fired in one burst -- see
 * media_sync_tick()'s doc comment.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "net.h"
#include "net_serialize.h"

typedef struct {
    char    *name;     /* malloc'd, e.g. "default_stone.png" */
    uint8_t  sha1[20];
    bool     satisfied; /* verified present on disk, from cache or freshly received */
} MediaEntry;

/* One pre-serialized TOSERVER_REQUEST_MEDIA body (u16 count + that many
 * string16 names), already kept under the 499-byte budget this
 * project's send path allows (see media_sync.c's REQUEST_BODY_BUDGET
 * comment for exactly how that number is derived) -- built once at
 * announce time, sent as-is once a slot is free. */
typedef struct {
    uint8_t body[499];
    size_t  len;
} MediaRequestBatch;

typedef struct {
    MediaEntry *entries;
    size_t      entry_count;
    size_t      entry_capacity;
    size_t      satisfied_count; /* cheap running total -- see media_sync_is_complete() */

    MediaRequestBatch *pending_batches;
    size_t             pending_count;
    size_t             next_pending; /* index of the next not-yet-sent batch (FIFO cursor) */

    bool announced; /* false until the first ANNOUNCE_MEDIA has been processed */
} MediaSync;

void media_sync_init(MediaSync *m);
void media_sync_free(MediaSync *m);

/* False before any ANNOUNCE_MEDIA has been processed. True once every
 * announced file has been verified present on disk (cache hit or
 * successfully received and checksummed) -- NOT true just because every
 * request batch has been sent; a file can still be in flight. */
bool media_sync_is_complete(const MediaSync *m);

/* Called by content.c when TOCLIENT_ANNOUNCE_MEDIA arrives. `r` must be
 * positioned right at the start of the packet body (right after the
 * opcode) -- this reads the whole packet itself, including the trailing
 * remote-server list (discarded, see this file's doc comment).
 *
 * Replaces whatever table was there before (matches upstream's one-shot
 * ClientMediaDownloader design -- a second ANNOUNCE_MEDIA on the same
 * connection isn't a real server's behavior, but if it happens, this
 * just starts over rather than merging, which would need to reconcile
 * stale in-flight requests against the old file list).
 *
 * Checks the on-disk cache for every announced file, then queues and
 * immediately starts sending (bounded by available window -- see
 * media_sync_tick()) TOSERVER_REQUEST_MEDIA batches for whatever's
 * missing. Hard-fails on structural packet corruption; tolerates and
 * logs-but-skips an individual bad entry (illegal filename, duplicate
 * name) the same way upstream does. */
void media_sync_on_announce(MediaSync *m, Net *net, NetReader *r);

/* Called by content.c when TOCLIENT_MEDIA arrives, same positioning
 * convention as media_sync_on_announce(). Decompresses and SHA-1-
 * verifies each file in the bunch; a checksum mismatch or an unannounced
 * filename is logged and that one file is skipped, not a hard failure
 * (matches upstream's checkAndLoad -- a bad file from a flaky or
 * malicious server shouldn't take down the whole client). Verified files
 * are written to the on-disk cache and marked satisfied; their bytes are
 * NOT kept in memory here -- texture_name.c/texture_compose.c (or
 * whatever else wants media bytes) reads them back via
 * media_sync_cache_path(). */
void media_sync_on_media(MediaSync *m, Net *net, NetReader *r);

/* Call periodically (e.g. once per loop.c tick, alongside
 * net_channel_tick) to drain any still-pending request batches as the
 * outgoing-reliable window frees up. Safe to call with nothing pending.
 * media_sync_on_announce() already calls this once itself so the first
 * wave goes out immediately rather than waiting for the caller's next
 * scheduled tick. */
void media_sync_tick(MediaSync *m, Net *net);

/* Builds the on-disk cache path for a file with this SHA-1 into
 * out_path (at least 256 bytes recommended -- a $HOME plus a fixed
 * suffix and a 40-char hex name comfortably fits). Returns false if
 * out_cap was too small or $HOME isn't set; otherwise the path is
 * built (NOT a promise the file exists -- combine with a normal fopen
 * to check that, same as media_sync.c's own cache lookup does). */
bool media_sync_cache_path(const uint8_t sha1[20], char *out_path, size_t out_cap);
