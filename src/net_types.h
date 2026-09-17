/* --- net_types.h ---
 * Wire-format constants for the Luanti UDP protocol (network/mtp/internal.h,
 * network/networkprotocol.h upstream). One job: define the wire layout.
 * No behavior lives here.
 */
#pragma once

#include <stdint.h>

/* Differentiates this protocol from random UDP noise. */
#define NET_PROTOCOL_ID 0x4f457403u

#define NET_PEER_ID_INEXISTENT 0u
#define NET_PEER_ID_SERVER     1u

#define NET_CHANNEL_COUNT 3u

/* Base header: every packet starts with this.
 *   [0] u32 protocol_id
 *   [4] u16 sender_peer_id
 *   [6] u8  channel
 */
#define NET_BASE_HEADER_SIZE 7u

typedef enum {
    NET_PACKET_TYPE_CONTROL  = 0,
    NET_PACKET_TYPE_ORIGINAL = 1,
    NET_PACKET_TYPE_SPLIT    = 2,
    NET_PACKET_TYPE_RELIABLE = 3,
    NET_PACKET_TYPE_MAX
} NetPacketType;

typedef enum {
    NET_CONTROLTYPE_ACK          = 0,
    NET_CONTROLTYPE_SET_PEER_ID  = 1,
    NET_CONTROLTYPE_PING         = 2,
    NET_CONTROLTYPE_DISCO        = 3
} NetControlType;

/* PACKET_TYPE_ORIGINAL header: [0] u8 type */
#define NET_ORIGINAL_HEADER_SIZE 1u

/* PACKET_TYPE_SPLIT header:
 *   [0] u8  type
 *   [1] u16 seqnum
 *   [3] u16 chunk_count
 *   [5] u16 chunk_num
 */
#define NET_SPLIT_HEADER_SIZE 7u

/* PACKET_TYPE_RELIABLE header:
 *   [0] u8  type
 *   [1] u16 seqnum
 */
#define NET_RELIABLE_HEADER_SIZE 3u

#define NET_SEQNUM_INITIAL 65500u
#define NET_SEQNUM_MAX     65535u

#define NET_START_RELIABLE_WINDOW_SIZE 64u
#define NET_MIN_RELIABLE_WINDOW_SIZE   32u
#define NET_MAX_RELIABLE_WINDOW_SIZE_SEND 2048u

/* Practical UDP payload ceiling we send in a single datagram before
 * splitting; matches upstream's conservative MTU assumption. */
#define NET_MAX_DATAGRAM_SIZE 512u

/* --- Handshake / auth opcodes we actually need for connect + login.
 * Full ToClientCommand / ToServerCommand enums have ~100 entries for
 * gameplay; we only enumerate what the handshake state machine touches.
 * Everything else is handed to the caller as an opaque (opcode, payload). */
#define NET_TOCLIENT_HELLO           0x02u
#define NET_TOCLIENT_AUTH_ACCEPT     0x03u
#define NET_TOCLIENT_ACCEPT_SUDO_MODE 0x04u
#define NET_TOCLIENT_DENY_SUDO_MODE  0x05u
#define NET_TOCLIENT_ACCESS_DENIED   0x0Au
#define NET_TOCLIENT_SRP_BYTES_S_B   0x60u

#define NET_TOSERVER_INIT            0x02u
#define NET_TOSERVER_INIT2           0x11u
#define NET_TOSERVER_SRP_BYTES_A     0x51u
#define NET_TOSERVER_SRP_BYTES_M     0x52u

#define NET_AUTH_MECHANISM_NONE            0u
#define NET_AUTH_MECHANISM_LEGACY_PASSWORD (1u << 0)
#define NET_AUTH_MECHANISM_SRP             (1u << 1)
#define NET_AUTH_MECHANISM_FIRST_SRP       (1u << 2)

/* current_login_based_on field in TOSERVER_SRP_BYTES_A */
#define NET_SRP_LOGIN_BASED_ON_LEGACY_HASH 0u
#define NET_SRP_LOGIN_BASED_ON_PASSWORD    1u

#define NET_SER_FMT_VER_HIGHEST_READ 29u
#define NET_CLIENT_PROTOCOL_VERSION_MIN 37u
#define NET_LATEST_PROTOCOL_VERSION     53u
