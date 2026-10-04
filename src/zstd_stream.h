/* --- zstd_stream.h ---
 * Growable-output zstd decompression, for payloads with no tight fixed
 * ceiling: TOCLIENT_NODEDEF, TOCLIENT_ITEMDEF, TOCLIENT_ANNOUNCE_MEDIA's
 * name/hash list, and individual media files in TOCLIENT_MEDIA. Unlike
 * MapBlocks (mapblock_decode.c's decompress_zstd_stream(), a fixed-size
 * buffer sized off a known worst case), none of these have a small,
 * confidently-derived ceiling -- a big mod pack's nodedef or a texture
 * pack's largest PNG can legitimately be megabytes. Prefer the
 * mapblock_decode.c sibling instead whenever a tight fixed ceiling really
 * is known; that one is cheaper (no malloc/realloc per call).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Decompresses one complete zstd frame from src[0..src_len) into a
 * malloc'd buffer that grows (starting small, doubling) as the frame
 * turns out to need more room, up to max_output bytes.
 *
 * On success: returns true, *out_data is a malloc'd buffer the caller
 * must free(), *out_len is the decompressed length.
 *
 * Returns false ONLY when the frame would decompress past max_output --
 * this is the one failure mode that's the caller's call to react to
 * (e.g. "this server's nodedef is absurd, refuse it") rather than this
 * project's usual hard-fail-and-exit, because max_output is a
 * caller-chosen policy limit, not evidence of corruption.
 *
 * Every OTHER failure (malformed zstd stream, truncated input, OOM) hard-
 * fails via exit(1), per this project's convention elsewhere -- those are
 * "impossible" states, not something calling code has a sane recovery
 * path for.
 */
bool zstd_stream_decompress(const uint8_t *src, size_t src_len,
                             size_t max_output,
                             uint8_t **out_data, size_t *out_len);
