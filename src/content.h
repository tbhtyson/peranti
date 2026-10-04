/* --- content.h ---
 * Dispatch point for the "content" packets: TOCLIENT_ITEMDEF, TOCLIENT_
 * NODEDEF, TOCLIENT_ANNOUNCE_MEDIA (and, once media_sync.c exists,
 * TOCLIENT_MEDIA). Exists so onGameplayPacket() in init.c doesn't grow a
 * hand-rolled switch for this whole area -- one call site, one
 * true/false return for "did you handle this opcode".
 *
 * Owns the connection's NodeDefTable, ItemDefTable, and MediaSync (see
 * nodedef.h/itemdef.h/media_sync.h) -- content_on_packet() decompresses
 * and parses into them directly; content_get_nodedef()/
 * content_get_itemdef()/content_get_media_sync() are how the mesher and
 * anything else downstream reads them back. TOCLIENT_MEDIA and
 * TOCLIENT_ANNOUNCE_MEDIA are both handled here now, forwarded straight
 * to media_sync.c's parser -- content.c itself doesn't touch their
 * payloads beyond opcode dispatch.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "net.h"
#include "nodedef.h"
#include "itemdef.h"
#include "media_sync.h"

/* Returns true if opcode was one of this module's, false otherwise (in
 * which case the caller should keep trying other dispatch points, same
 * convention as everything else onGameplayPacket delegates to).
 *
 * Hard-fails (exit(1)) if the server's protocol version predates zstd-
 * compressed defs (<48) -- the zlib fallback isn't implemented -- and if
 * a decompressed blob exceeds this module's ceiling. Both are "raise the
 * ceiling / add zlib" situations for a human, not something to silently
 * paper over. */
bool content_on_packet(Net *net, uint16_t opcode, const uint8_t *payload, size_t len);

/* Call once when a connection is torn down (whether cleanly or not) --
 * frees both tables and leaves content.c ready for a fresh connection.
 * Without this, reconnecting to a different server would leak the old
 * one's tables when content_on_packet() re-parses fresh NODEDEF/ITEMDEF
 * packets (nodedef_parse()/itemdef_parse() each free their own table
 * before repopulating it, but only content_reset() frees them when
 * NO new packet is coming, i.e. on disconnect). */
void content_reset(void);

/* Valid any time after the corresponding NODEDEF/ITEMDEF packet has been
 * processed; empty-but-valid (zero entries) before that, so callers
 * don't need to special-case "not connected yet" -- a lookup against an
 * empty table just returns NULL/not-found, same as a lookup for an id
 * that was never defined. Never NULL themselves. */
const NodeDefTable *content_get_nodedef(void);
const ItemDefTable *content_get_itemdef(void);
const MediaSync *content_get_media_sync(void);
