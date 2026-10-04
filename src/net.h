/* --- net.h ---
 * One job: own the connection lifecycle -- socket, three reliability
 * channels, and the bootstrap/auth handshake state machine -- and expose
 * a tiny, decoupled surface to the rest of the engine: net_poll() each
 * frame, a gameplay-payload callback once connected, net_send_gameplay()
 * to talk back. Nothing here knows about World/Mapblock/mesh.c; the
 * gameplay handler hands back (opcode, payload, len) and the caller
 * decides what to do with it (see net_recreation notes for the
 * TOCLIENT_BLOCKDATA -> world_insert wiring sketched earlier).
 *
 * Scope: logs into an EXISTING account via AUTH_MECHANISM_SRP. Does not
 * implement AUTH_MECHANISM_FIRST_SRP (registration) or
 * AUTH_MECHANISM_LEGACY_PASSWORD (pre-SRP accounts) -- if the server
 * offers only those, net_get_state() lands on NET_CONN_FAILED with a
 * descriptive net_get_last_error().
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    NET_CONN_DISCONNECTED,
    NET_CONN_CONNECTING, /* bootstrap -> hello -> srp -> auth_accept, in progress */
    NET_CONN_INGAME,     /* TOSERVER_INIT2 sent; server may now send world data */
    NET_CONN_FAILED       /* see net_get_last_error() */
} NetConnState;

typedef struct Net Net;

typedef void (*NetGameplayHandler)(uint16_t opcode, const uint8_t *payload, size_t len,
                                    void *user_data);

/* Hard-fails (exit(1)) on address resolution/socket errors, matching this
 * project's convention that a bad server address is a setup bug, not a
 * runtime condition to recover from. Auth failures (wrong password,
 * unsupported auth mechanism, server-side deny) are NOT hard failures --
 * they land in NET_CONN_FAILED for the caller to handle. */
Net *net_create(const char *host, uint16_t port, const char *playername, const char *password);
void net_destroy(Net *net);

void net_set_gameplay_handler(Net *net, NetGameplayHandler handler, void *user_data);

/* Non-blocking. Call once per frame with the frame's dtime in seconds:
 * drains the socket, advances the handshake state machine, resends
 * unacked reliable packets, and fires the gameplay handler for anything
 * received once NET_CONN_INGAME. */
void net_poll(Net *net, float dtime);

NetConnState net_get_state(const Net *net);

/* Valid once net_get_state() == NET_CONN_FAILED. */
const char *net_get_last_error(const Net *net);

/* How often the caller should send TOSERVER_PLAYERPOS (see loop.c's
 * sendPlayerPosPacket() for why this needs sending at all -- without it,
 * the server has no idea where you are and won't stream any blocks).
 * Starts at a sane default and updates once TOCLIENT_AUTH_ACCEPT's real
 * value arrives, matching how a real client paces this. */
float net_get_recommended_send_interval(const Net *net);

/* See net.c's doc comment on the field this returns -- needed by the
 * MapBlock decoder, not by anything in net.c itself. */
uint8_t net_get_server_ser_ver(const Net *net);

/* From TOCLIENT_HELLO -- server's negotiated protocol version. Needed by
 * content.c to pick zstd (>=48) vs zlib for ITEMDEF/NODEDEF/
 * ANNOUNCE_MEDIA payloads, and to decide which optional ContentFeatures/
 * ItemDefinition tail fields a given server will actually send. */
uint16_t net_get_proto_ver(const Net *net);

/* Sends an already-serialized gameplay packet: `payload` is just the
 * opcode-specific body, this prepends the u16 opcode itself. Only valid
 * once NET_CONN_INGAME -- hard-fails otherwise, since sending gameplay
 * packets before auth completes is a caller bug. */
void net_send_gameplay(Net *net, uint16_t opcode, const uint8_t *payload, size_t len,
                        uint8_t channel_num, bool reliable);

/* Free outgoing-reliable slots on `channel_num` right now (see
 * net_channel_count_free_outgoing's doc comment for why this exists) --
 * 0 if channel_num is out of range. */
size_t net_get_free_outgoing_slots(const Net *net, uint8_t channel_num);
