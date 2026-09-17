#ifndef PERANTI_MAPBLOCK_DECODE_H
#define PERANTI_MAPBLOCK_DECODE_H

#include "chunk.h"
#include "world.h"
#include <stdint.h>
#include <stddef.h>

/* Decodes one TOCLIENT_BLOCKDATA payload: v3s16 block position followed by
 * a serialized MapBlock in the NETWORK wire format specifically -- i.e.
 * MapBlock::deSerialize(..., disk=false) upstream, confirmed against
 * Client::handleCommand_BlockData and MapBlock::deSerializeUncompressed,
 * not assumed from the on-disk map.sqlite format used elsewhere in this
 * project. The two formats genuinely differ:
 *   - No leading version byte. The on-disk format has one; this one
 *     doesn't -- the version is established once via TOCLIENT_HELLO and
 *     held for the whole session (net_get_server_ser_ver()). Pass that in
 *     as `server_ser_ver`. Treating this format as if it had a leading
 *     version byte (i.e. reusing the disk-format assumption here) would
 *     eat one real byte of the zstd stream and corrupt every block.
 *   - No timestamp, no per-block NameIdMapping, no static objects, no
 *     node timers. Those are disk-only fields; content IDs are instead
 *     negotiated once for the whole session via TOCLIENT_NODEDEF before
 *     any blocks arrive, so no per-block ID translation happens here.
 *
 * Only implements the version>=29 (zstd) wire path -- this project's own
 * NET_SER_FMT_VER_HIGHEST_READ is 29, so a real server will never
 * negotiate anything older; the pre-29 path upstream is a genuinely
 * different, older format this decoder doesn't attempt.
 *
 * Hard-fails (exit(1)) on any malformed or unsupported input, matching
 * this project's convention elsewhere in the net_* module: from our own
 * trusted dev server, a corrupt block indicates a bug in this decoder,
 * not a condition to route around silently.
 *
 * `*out_block` is left in MAPBLOCK_FULL state (heap-allocated nodes) on
 * success; caller owns it and must mapblock_destroy() it eventually
 * (world_insert() takes ownership if inserted, world_destroy() then frees
 * it along with everything else in the table).
 */
void mapblock_decode_network(const uint8_t *payload, size_t len, uint8_t server_ser_ver,
                              ChunkCoord *out_coord, Mapblock *out_block);

#endif
