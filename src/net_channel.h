/* --- net_channel.h ---
 * One job: the reliability layer sitting on top of raw UDP -- per-channel
 * seqnum tracking, ACK/resend for outgoing PACKET_TYPE_RELIABLE packets,
 * reorder buffering for incoming ones, and reassembly of PACKET_TYPE_SPLIT
 * fragments. Mirrors network/mtp/internal.h's Channel/ReliablePacketBuffer/
 * IncomingSplitBuffer, simplified: fixed capacity throughout (no dynamic
 * growth), and a flat exponential-backoff resend timer instead of upstream's
 * RTT-adaptive one -- tune NET_RESEND_TIMEOUT_BASE if real-world latency
 * needs it.
 *
 * Scope note: outgoing packets in this recreation are never split -- every
 * reliable send here is expected to fit in one datagram (true for the
 * handshake and for small gameplay commands). Incoming split reassembly
 * IS implemented, since TOCLIENT_BLOCKDATA will need it once world syncing
 * is wired up.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "net_types.h"
#include "net_socket.h"

#define NET_MAX_UNACKED_OUTGOING   64u
/* Sized to match NET_MAX_RELIABLE_WINDOW_SIZE_SEND (upstream's real
 * reliable-window size, used below to decide what's acceptable to buffer)
 * rather than an independent guess. A 64-slot buffer accepting anything
 * within a 2048-wide window was the actual bug behind the "reorder buffer
 * full" crash: a single ordinary UDP packet loss during a fast burst (item/
 * node defs, media) means everything arriving after the gap piles up
 * waiting for the sender's resend, and 64 slots fills in a fraction of a
 * second during a large sync. Each slot is a full NET_MAX_DATAGRAM_SIZE
 * buffer, so this costs ~1 MiB per channel (~3 MiB total across the 3
 * channels) -- trivial for a native client, and now sized by the
 * protocol's own real bound instead of a guess. */
#define NET_MAX_INCOMING_REORDER   NET_MAX_RELIABLE_WINDOW_SIZE_SEND
#define NET_MAX_SPLIT_ASSEMBLIES   4u

/* Split messages are sized by whatever the sender declares (chunk_count is
 * a u16, so up to 65535 chunks in principle) -- a fixed NET_MAX_SPLIT_CHUNKS
 * array sized by guesswork just breaks on the next packet type that's
 * bigger than the guess (this is exactly what happened: 256 was picked
 * for mapblocks, then the very first post-handshake packet -- item/node
 * definitions -- needed 275). So this is allocated per-message instead,
 * sized to what the packet actually declares, bounded by this ceiling as
 * a sanity check against a corrupt or hostile chunk_count rather than a
 * guess at real traffic size. Raise it if VoxeLibre-scale media batches
 * need more; 32 MiB comfortably covers node/item defs and most media
 * batches without static allocation.
 */
#define NET_MAX_SPLIT_TOTAL_SIZE (32u * 1024u * 1024u)

#define NET_RESEND_TIMEOUT_BASE 0.5f   /* seconds */
#define NET_RESEND_TIMEOUT_MAX  4.0f
#define NET_RESEND_SCALE_BASE   1.5f
#define NET_MAX_RESEND_ATTEMPTS 12     /* give up -> connection considered dead */

typedef struct {
    bool     in_use;
    uint16_t seqnum;
    uint8_t  data[NET_MAX_DATAGRAM_SIZE];
    size_t   len;
    float    time_since_send;
    uint32_t resend_count;
} NetOutgoingReliable;

typedef struct {
    bool     in_use;
    uint16_t seqnum;
    uint8_t  data[NET_MAX_DATAGRAM_SIZE];
    size_t   len;
} NetIncomingReorder;

typedef struct {
    bool     in_use;
    uint16_t seqnum;       /* split seqnum shared by all chunks of this message */
    uint16_t chunk_count;
    uint32_t chunks_received;

    /* Heap-allocated, sized exactly to this message: chunk_present/chunk_len
     * have chunk_count entries; scratch is chunk_count * NET_MAX_DATAGRAM_SIZE
     * bytes (an upper bound -- only the last chunk may be shorter than that,
     * so writing each chunk at offset chunk_num*NET_MAX_DATAGRAM_SIZE never
     * collides). Allocated when the first chunk for this seqnum arrives
     * (that's when chunk_count first becomes known), freed the moment the
     * message completes and is handed to the ready queue, or on
     * net_channel_destroy() if the connection dies mid-transfer. */
    bool    *chunk_present;
    size_t  *chunk_len;
    uint8_t *scratch;

    float    age;
} NetSplitAssembly;

/* Control-plane events (SET_PEER_ID, DISCO) that net.c's connection state
 * machine needs to see. Kept separate from the application-payload ready
 * queue below -- these are protocol bookkeeping, not gameplay data, and
 * conflating them would make a CONTROL frame indistinguishable from an
 * ORIGINAL payload that happens to start with the same byte. */
typedef enum {
    NET_CTRL_EVENT_SET_PEER_ID,
    NET_CTRL_EVENT_DISCO
} NetControlEventType;

typedef struct {
    NetControlEventType type;
    uint16_t peer_id_new; /* valid for NET_CTRL_EVENT_SET_PEER_ID */
} NetControlEvent;

#define NET_CONTROL_EVENT_QUEUE_DEPTH 4u

/* Heap-allocated, exact size -- ORIGINAL payloads are tiny (handshake/
 * command size) but a reassembled SPLIT payload can be large and its size
 * varies enormously by opcode, so a fixed-size slot here would have the
 * same "sized by guesswork" problem the old split buffer had. Ownership
 * transfers to whoever calls net_channel_poll_ready(): they must free()
 * NetInboundPacket.payload once done with it.
 *
 * This queue is a linked list, not a fixed-depth array, for the same
 * reason NET_MAX_INCOMING_REORDER got resized instead of just raised
 * again: a single net_channel_on_datagram() call can, via the reorder
 * buffer's chained delivery, complete an unbounded number of messages in
 * one shot (draining a large backlog after a resend finally arrives), and
 * separately a burst of several small unrelated gameplay packets can land
 * in one net_poll() call before the caller gets a chance to drain. A fixed
 * depth here would just be another guessed number waiting to be wrong on
 * a bigger burst; this way it can't overflow except true OOM. */
typedef struct NetReadyNode {
    uint8_t *payload;
    size_t   len;
    struct NetReadyNode *next;
} NetReadyNode;

typedef struct {
    uint16_t next_outgoing_seqnum;
    uint16_t next_incoming_seqnum;

    NetOutgoingReliable outgoing[NET_MAX_UNACKED_OUTGOING];
    NetIncomingReorder  incoming[NET_MAX_INCOMING_REORDER];
    NetSplitAssembly    splits[NET_MAX_SPLIT_ASSEMBLIES];

    /* Linked-list FIFO of fully-reassembled application payloads, ready
     * for the caller to consume via net_channel_poll_ready(). */
    NetReadyNode *ready_head;
    NetReadyNode *ready_tail;

    /* FIFO of control-plane events (see NetControlEvent above). */
    NetControlEvent control_events[NET_CONTROL_EVENT_QUEUE_DEPTH];
    size_t control_head;
    size_t control_count;
} NetChannel;

void net_channel_init(NetChannel *ch);

/* Sends `payload` wrapped in a PACKET_TYPE_ORIGINAL header, no reliability.
 * Used for TOSERVER_INIT, which upstream intentionally sends unreliably and
 * relies on the caller re-sending until TOCLIENT_HELLO arrives. */
void net_channel_send_unreliable(NetChannel *ch, NetSocket *sock, uint16_t peer_id,
                                  uint8_t channel_num, const void *payload, size_t len);

/* Sends `payload` wrapped in PACKET_TYPE_RELIABLE(PACKET_TYPE_ORIGINAL(...)),
 * stores it for resend until acked. Hard-fails if the unacked window is full
 * -- that means we're sending faster than the server acks, which for our
 * handshake-and-light-gameplay use is a bug, not a condition to degrade
 * silently into dropped packets. */
void net_channel_send_reliable(NetChannel *ch, NetSocket *sock, uint16_t peer_id,
                                uint8_t channel_num, const void *payload, size_t len);

/* Call once per frame per channel: resends anything past its backoff
 * deadline. `dtime` in seconds. Returns false if a packet exceeded
 * NET_MAX_RESEND_ATTEMPTS (caller should treat the connection as dead). */
bool net_channel_tick(NetChannel *ch, NetSocket *sock, uint16_t peer_id,
                       uint8_t channel_num, float dtime);

/* Feed a raw ACK control packet's seqnum in here to clear the matching
 * outgoing entry. */
void net_channel_handle_ack(NetChannel *ch, uint16_t acked_seqnum);

/* Result of processing one inbound datagram's channel-relative payload
 * (i.e. everything after the 7-byte base header). `payload` is heap-
 * allocated and owned by the CALLER once returned from
 * net_channel_poll_ready() -- call free() on it when done. */
typedef struct {
    uint8_t *payload;
    size_t   len;
} NetInboundPacket;

/* Feeds one datagram's post-base-header bytes into the channel. Handles
 * PACKET_TYPE_CONTROL (ACK bookkeeping), PACKET_TYPE_RELIABLE (ack-send +
 * in-order delivery + reorder buffering), and PACKET_TYPE_SPLIT/ORIGINAL
 * (reassembly / direct pass-through). Any packets that become ready as a
 * result -- the one just received, or ones unblocked from the reorder
 * buffer -- are pushed onto the ready queue; this function itself never
 * returns application data. */
void net_channel_on_datagram(NetChannel *ch, NetSocket *sock, uint16_t peer_id,
                              uint8_t channel_num, const uint8_t *data, size_t len);

/* Pops one ready application payload if available. Call in a loop until it
 * returns false to fully drain the channel each frame. */
bool net_channel_poll_ready(NetChannel *ch, NetInboundPacket *out);

/* Pops one control-plane event (SET_PEER_ID/DISCO) if available. Call in a
 * loop alongside net_channel_poll_ready() to fully drain the channel. */
bool net_channel_poll_control_event(NetChannel *ch, NetControlEvent *out);

/* Frees any heap state this channel is still holding: in-flight split
 * assemblies' scratch buffers, and any ready-queue payloads the caller
 * never polled out. Call once per channel when tearing down the
 * connection (net_destroy()) -- not needed per-frame, net_channel_init()
 * zeroes a channel for reuse but doesn't free anything since a freshly
 * net_channel_init()'d channel never held allocations in the first place. */
void net_channel_destroy(NetChannel *ch);
