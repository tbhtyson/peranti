#include "net_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "net_serialize.h"

static bool net_seqnum_higher(uint16_t totest, uint16_t base) {
    if (totest > base)
        return (uint16_t)(totest - base) <= (uint16_t)(NET_SEQNUM_MAX / 2);
    return (uint16_t)(base - totest) > (uint16_t)(NET_SEQNUM_MAX / 2);
}

static bool net_seqnum_in_window(uint16_t seqnum, uint16_t next, uint16_t window_size) {
    uint16_t window_start = next;
    uint16_t window_end = (uint16_t)((next + window_size) % (NET_SEQNUM_MAX + 1));
    if (window_start < window_end)
        return seqnum >= window_start && seqnum < window_end;
    return seqnum < window_end || seqnum >= window_start;
}

void net_channel_init(NetChannel *ch) {
    memset(ch, 0, sizeof(*ch));
    ch->next_outgoing_seqnum = NET_SEQNUM_INITIAL;
    ch->next_incoming_seqnum = NET_SEQNUM_INITIAL;
}

void net_channel_destroy(NetChannel *ch) {
    for (size_t i = 0; i < NET_MAX_SPLIT_ASSEMBLIES; ++i) {
        if (ch->splits[i].in_use) {
            free(ch->splits[i].chunk_present);
            free(ch->splits[i].chunk_len);
            free(ch->splits[i].scratch);
        }
    }
    NetReadyNode *node = ch->ready_head;
    while (node) {
        NetReadyNode *next = node->next;
        free(node->payload);
        free(node);
        node = next;
    }
    memset(ch, 0, sizeof(*ch));
}

/* --- outgoing base-header framing --- */

static void net_send_framed(NetSocket *sock, uint16_t peer_id, uint8_t channel_num,
                             const void *content, size_t content_len) {
    uint8_t buf[NET_MAX_DATAGRAM_SIZE];
    NetWriter w;
    net_writer_init(&w, buf, sizeof(buf));
    net_put_u32(&w, NET_PROTOCOL_ID);
    net_put_u16(&w, peer_id);
    net_put_u8(&w, channel_num);
    net_put_bytes(&w, content, content_len);
    net_socket_send(sock, buf, w.pos);
}

static void net_send_ack(NetSocket *sock, uint16_t peer_id, uint8_t channel_num, uint16_t seqnum) {
    uint8_t content[4];
    NetWriter w;
    net_writer_init(&w, content, sizeof(content));
    net_put_u8(&w, NET_PACKET_TYPE_CONTROL);
    net_put_u8(&w, NET_CONTROLTYPE_ACK);
    net_put_u16(&w, seqnum);
    net_send_framed(sock, peer_id, channel_num, content, w.pos);
}

void net_channel_send_unreliable(NetChannel *ch, NetSocket *sock, uint16_t peer_id,
                                  uint8_t channel_num, const void *payload, size_t len) {
    (void)ch;
    uint8_t content[NET_MAX_DATAGRAM_SIZE];
    NetWriter w;
    net_writer_init(&w, content, sizeof(content));
    net_put_u8(&w, NET_PACKET_TYPE_ORIGINAL);
    net_put_bytes(&w, payload, len);
    net_send_framed(sock, peer_id, channel_num, content, w.pos);
}

void net_channel_send_reliable(NetChannel *ch, NetSocket *sock, uint16_t peer_id,
                                uint8_t channel_num, const void *payload, size_t len) {
    NetOutgoingReliable *slot = NULL;
    for (size_t i = 0; i < NET_MAX_UNACKED_OUTGOING; ++i) {
        if (!ch->outgoing[i].in_use) {
            slot = &ch->outgoing[i];
            break;
        }
    }
    if (!slot) {
        fprintf(stderr, "net_channel: outgoing reliable window full (%u unacked) -- "
                "server isn't acking, or NET_MAX_UNACKED_OUTGOING is too small\n",
                NET_MAX_UNACKED_OUTGOING);
        exit(1);
    }

    NetWriter w;
    net_writer_init(&w, slot->data, sizeof(slot->data));
    net_put_u8(&w, NET_PACKET_TYPE_RELIABLE);
    net_put_u16(&w, ch->next_outgoing_seqnum);
    net_put_u8(&w, NET_PACKET_TYPE_ORIGINAL);
    net_put_bytes(&w, payload, len);

    slot->in_use = true;
    slot->seqnum = ch->next_outgoing_seqnum;
    slot->len = w.pos;
    slot->time_since_send = 0.0f;
    slot->resend_count = 0;

    net_send_framed(sock, peer_id, channel_num, slot->data, slot->len);

    ch->next_outgoing_seqnum = (uint16_t)((ch->next_outgoing_seqnum + 1) % (NET_SEQNUM_MAX + 1));
}

bool net_channel_tick(NetChannel *ch, NetSocket *sock, uint16_t peer_id,
                       uint8_t channel_num, float dtime) {
    for (size_t i = 0; i < NET_MAX_UNACKED_OUTGOING; ++i) {
        NetOutgoingReliable *slot = &ch->outgoing[i];
        if (!slot->in_use)
            continue;

        slot->time_since_send += dtime;
        float deadline = NET_RESEND_TIMEOUT_BASE * powf(NET_RESEND_SCALE_BASE, (float)slot->resend_count);
        if (deadline > NET_RESEND_TIMEOUT_MAX)
            deadline = NET_RESEND_TIMEOUT_MAX;

        if (slot->time_since_send >= deadline) {
            if (slot->resend_count >= NET_MAX_RESEND_ATTEMPTS)
                return false; /* connection considered dead */

            net_send_framed(sock, peer_id, channel_num, slot->data, slot->len);
            slot->time_since_send = 0.0f;
            slot->resend_count++;
        }
    }
    return true;
}

void net_channel_handle_ack(NetChannel *ch, uint16_t acked_seqnum) {
    for (size_t i = 0; i < NET_MAX_UNACKED_OUTGOING; ++i) {
        if (ch->outgoing[i].in_use && ch->outgoing[i].seqnum == acked_seqnum) {
            ch->outgoing[i].in_use = false;
            return;
        }
    }
    /* Ack for something we already cleared (duplicate) or never sent
     * (stale/foreign) -- not an error, just ignore. */
}

/* --- ready queue --- */

static void net_channel_push_ready_owned(NetChannel *ch, uint8_t *owned_data, size_t len) {
    NetReadyNode *node = malloc(sizeof(NetReadyNode));
    if (!node) {
        fprintf(stderr, "net_channel: OOM allocating ready-queue node\n");
        exit(1);
    }
    node->payload = owned_data;
    node->len = len;
    node->next = NULL;

    if (ch->ready_tail)
        ch->ready_tail->next = node;
    else
        ch->ready_head = node;
    ch->ready_tail = node;
}

/* Copies `data` into a fresh heap allocation and pushes it, for the common
 * case (ORIGINAL payloads) where the source isn't already something we can
 * hand off ownership of directly. */
static void net_channel_push_ready_copy(NetChannel *ch, const uint8_t *data, size_t len) {
    uint8_t *owned = malloc(len > 0 ? len : 1);
    if (!owned) {
        fprintf(stderr, "net_channel: OOM allocating %zu-byte ready payload\n", len);
        exit(1);
    }
    memcpy(owned, data, len);
    net_channel_push_ready_owned(ch, owned, len);
}

bool net_channel_poll_ready(NetChannel *ch, NetInboundPacket *out) {
    if (!ch->ready_head)
        return false;
    NetReadyNode *node = ch->ready_head;
    out->payload = node->payload;
    out->len = node->len;

    ch->ready_head = node->next;
    if (!ch->ready_head)
        ch->ready_tail = NULL;
    free(node); /* frees the list node only -- out->payload ownership has
                 * transferred to the caller, who must free() it */
    return true;
}

static void net_channel_push_control_event(NetChannel *ch, NetControlEvent ev) {
    if (ch->control_count >= NET_CONTROL_EVENT_QUEUE_DEPTH) {
        fprintf(stderr, "net_channel: control event queue full -- caller isn't draining "
                "net_channel_poll_control_event() every frame\n");
        exit(1);
    }
    size_t slot = (ch->control_head + ch->control_count) % NET_CONTROL_EVENT_QUEUE_DEPTH;
    ch->control_events[slot] = ev;
    ch->control_count++;
}

bool net_channel_poll_control_event(NetChannel *ch, NetControlEvent *out) {
    if (ch->control_count == 0)
        return false;
    *out = ch->control_events[ch->control_head];
    ch->control_head = (ch->control_head + 1) % NET_CONTROL_EVENT_QUEUE_DEPTH;
    ch->control_count--;
    return true;
}

/* Handles a raw CONTROL frame's bytes, whether it arrived at the top level
 * or nested inside a RELIABLE wrapper. ACKs are pure bookkeeping (never
 * surfaced); SET_PEER_ID/DISCO go on the control-event queue for net.c;
 * PING needs no reply and isn't surfaced. */
static void net_channel_handle_control(NetChannel *ch, const uint8_t *data, size_t len) {
    if (len < 2) {
        fprintf(stderr, "net_channel: truncated control frame\n");
        exit(1);
    }
    uint8_t controltype = data[1];
    switch (controltype) {
        case NET_CONTROLTYPE_ACK:
            if (len < 4) {
                fprintf(stderr, "net_channel: truncated ACK\n");
                exit(1);
            }
            net_channel_handle_ack(ch, (uint16_t)((data[2] << 8) | data[3]));
            break;
        case NET_CONTROLTYPE_SET_PEER_ID: {
            if (len < 4) {
                fprintf(stderr, "net_channel: truncated SET_PEER_ID\n");
                exit(1);
            }
            NetControlEvent ev = { .type = NET_CTRL_EVENT_SET_PEER_ID,
                                    .peer_id_new = (uint16_t)((data[2] << 8) | data[3]) };
            net_channel_push_control_event(ch, ev);
            break;
        }
        case NET_CONTROLTYPE_DISCO: {
            NetControlEvent ev = { .type = NET_CTRL_EVENT_DISCO, .peer_id_new = 0 };
            net_channel_push_control_event(ch, ev);
            break;
        }
        case NET_CONTROLTYPE_PING:
            /* No reply expected/needed. */
            break;
        default:
            fprintf(stderr, "net_channel: unknown controltype %u\n", (unsigned)controltype);
            exit(1);
    }
}

/* --- split reassembly --- */

static void net_channel_free_assembly(NetSplitAssembly *asm_) {
    free(asm_->chunk_present);
    free(asm_->chunk_len);
    free(asm_->scratch);
    memset(asm_, 0, sizeof(*asm_));
}

static void net_channel_handle_split(NetChannel *ch, const uint8_t *data, size_t len) {
    if (len < NET_SPLIT_HEADER_SIZE) {
        fprintf(stderr, "net_channel: split packet shorter than its own header\n");
        exit(1);
    }
    NetReader r;
    net_reader_init(&r, data, len);
    net_get_u8(&r); /* type, already known */
    uint16_t seqnum = net_get_u16(&r);
    uint16_t chunk_count = net_get_u16(&r);
    uint16_t chunk_num = net_get_u16(&r);
    const uint8_t *chunk_data = data + NET_SPLIT_HEADER_SIZE;
    size_t chunk_len = len - NET_SPLIT_HEADER_SIZE;

    if (chunk_count == 0 || chunk_num >= chunk_count) {
        fprintf(stderr, "net_channel: malformed split header (count=%u num=%u)\n",
                chunk_count, chunk_num);
        exit(1);
    }
    if (chunk_len > NET_MAX_DATAGRAM_SIZE) {
        fprintf(stderr, "net_channel: split chunk of %zu bytes exceeds datagram cap\n", chunk_len);
        exit(1);
    }
    size_t scratch_needed = (size_t)chunk_count * NET_MAX_DATAGRAM_SIZE;
    if (scratch_needed > NET_MAX_SPLIT_TOTAL_SIZE) {
        fprintf(stderr, "net_channel: split message declares chunk_count=%u -> %zu bytes, "
                "exceeds NET_MAX_SPLIT_TOTAL_SIZE (%u) -- corrupt/hostile header, or raise "
                "the ceiling if this is genuine traffic\n",
                chunk_count, scratch_needed, NET_MAX_SPLIT_TOTAL_SIZE);
        exit(1);
    }

    NetSplitAssembly *asm_ = NULL;
    for (size_t i = 0; i < NET_MAX_SPLIT_ASSEMBLIES; ++i) {
        if (ch->splits[i].in_use && ch->splits[i].seqnum == seqnum) {
            asm_ = &ch->splits[i];
            break;
        }
    }
    if (!asm_) {
        for (size_t i = 0; i < NET_MAX_SPLIT_ASSEMBLIES; ++i) {
            if (!ch->splits[i].in_use) {
                asm_ = &ch->splits[i];
                memset(asm_, 0, sizeof(*asm_));
                asm_->in_use = true;
                asm_->seqnum = seqnum;
                asm_->chunk_count = chunk_count;
                asm_->chunk_present = calloc(chunk_count, sizeof(bool));
                asm_->chunk_len = calloc(chunk_count, sizeof(size_t));
                asm_->scratch = malloc(scratch_needed);
                if (!asm_->chunk_present || !asm_->chunk_len || !asm_->scratch) {
                    fprintf(stderr, "net_channel: OOM allocating split assembly "
                            "(chunk_count=%u -> %zu bytes)\n", chunk_count, scratch_needed);
                    exit(1);
                }
                break;
            }
        }
    }
    if (!asm_) {
        fprintf(stderr, "net_channel: split assembly table full (%u in flight) -- "
                "raise NET_MAX_SPLIT_ASSEMBLIES\n", NET_MAX_SPLIT_ASSEMBLIES);
        exit(1);
    }

    if (asm_->chunk_count != chunk_count) {
        fprintf(stderr, "net_channel: split seqnum %u chunk_count mismatch (%u vs %u)\n",
                seqnum, asm_->chunk_count, chunk_count);
        exit(1);
    }

    if (!asm_->chunk_present[chunk_num]) {
        memcpy(asm_->scratch + (size_t)chunk_num * NET_MAX_DATAGRAM_SIZE, chunk_data, chunk_len);
        asm_->chunk_len[chunk_num] = chunk_len;
        asm_->chunk_present[chunk_num] = true;
        asm_->chunks_received++;
    }

    if (asm_->chunks_received == asm_->chunk_count) {
        size_t flat_len = 0;
        for (uint16_t i = 0; i < asm_->chunk_count; ++i)
            flat_len += asm_->chunk_len[i];

        uint8_t *flat = malloc(flat_len > 0 ? flat_len : 1);
        if (!flat) {
            fprintf(stderr, "net_channel: OOM allocating %zu-byte reassembled payload\n", flat_len);
            exit(1);
        }
        size_t pos = 0;
        for (uint16_t i = 0; i < asm_->chunk_count; ++i) {
            memcpy(flat + pos, asm_->scratch + (size_t)i * NET_MAX_DATAGRAM_SIZE, asm_->chunk_len[i]);
            pos += asm_->chunk_len[i];
        }

        net_channel_free_assembly(asm_); /* also clears in_use */
        net_channel_push_ready_owned(ch, flat, flat_len);
    }
}

/* Delivers one fully-ordered inner packet (already stripped of any
 * RELIABLE header): hands it to the ready queue (ORIGINAL), feeds the
 * split reassembler (SPLIT), or routes it to control-event handling
 * (CONTROL -- upstream does allow a bare empty-payload bootstrap packet's
 * RELIABLE wrapper to carry a nested CONTROL/SET_PEER_ID reply). */
static void net_channel_deliver_inner(NetChannel *ch, const uint8_t *data, size_t len) {
    if (len < 1) {
        fprintf(stderr, "net_channel: zero-length inner packet\n");
        exit(1);
    }
    uint8_t type = data[0];
    if (type == NET_PACKET_TYPE_ORIGINAL) {
        net_channel_push_ready_copy(ch, data + NET_ORIGINAL_HEADER_SIZE, len - NET_ORIGINAL_HEADER_SIZE);
    } else if (type == NET_PACKET_TYPE_SPLIT) {
        net_channel_handle_split(ch, data, len);
    } else if (type == NET_PACKET_TYPE_CONTROL) {
        net_channel_handle_control(ch, data, len);
    } else {
        fprintf(stderr, "net_channel: unexpected nested packet type %u (a RELIABLE packet "
                "cannot itself carry another RELIABLE frame)\n", (unsigned)type);
        exit(1);
    }
}

void net_channel_on_datagram(NetChannel *ch, NetSocket *sock, uint16_t peer_id,
                              uint8_t channel_num, const uint8_t *data, size_t len) {
    if (len < 1) {
        fprintf(stderr, "net_channel: empty datagram content\n");
        exit(1);
    }
    uint8_t type = data[0];

    if (type == NET_PACKET_TYPE_CONTROL) {
        net_channel_handle_control(ch, data, len);
        return;
    }

    if (type == NET_PACKET_TYPE_RELIABLE) {
        if (len < NET_RELIABLE_HEADER_SIZE) {
            fprintf(stderr, "net_channel: truncated reliable header\n");
            exit(1);
        }
        uint16_t seqnum = (uint16_t)((data[1] << 8) | data[2]);
        const uint8_t *inner = data + NET_RELIABLE_HEADER_SIZE;
        size_t inner_len = len - NET_RELIABLE_HEADER_SIZE;

        net_send_ack(sock, peer_id, channel_num, seqnum);

        if (seqnum == ch->next_incoming_seqnum) {
            net_channel_deliver_inner(ch, inner, inner_len);
            ch->next_incoming_seqnum = (uint16_t)((ch->next_incoming_seqnum + 1) % (NET_SEQNUM_MAX + 1));

            /* drain anything already buffered that is now next-in-line */
            bool advanced = true;
            while (advanced) {
                advanced = false;
                for (size_t i = 0; i < NET_MAX_INCOMING_REORDER; ++i) {
                    if (ch->incoming[i].in_use && ch->incoming[i].seqnum == ch->next_incoming_seqnum) {
                        net_channel_deliver_inner(ch, ch->incoming[i].data, ch->incoming[i].len);
                        ch->incoming[i].in_use = false;
                        ch->next_incoming_seqnum = (uint16_t)((ch->next_incoming_seqnum + 1) % (NET_SEQNUM_MAX + 1));
                        advanced = true;
                        break;
                    }
                }
            }
        } else if (net_seqnum_higher(seqnum, ch->next_incoming_seqnum) &&
                   net_seqnum_in_window(seqnum, ch->next_incoming_seqnum, NET_MAX_RELIABLE_WINDOW_SIZE_SEND)) {
            bool already_buffered = false;
            NetIncomingReorder *slot = NULL;
            for (size_t i = 0; i < NET_MAX_INCOMING_REORDER; ++i) {
                if (ch->incoming[i].in_use && ch->incoming[i].seqnum == seqnum) {
                    already_buffered = true;
                    break;
                }
                if (!slot && !ch->incoming[i].in_use)
                    slot = &ch->incoming[i];
            }
            if (!already_buffered) {
                if (!slot) {
                    fprintf(stderr, "net_channel: incoming reorder buffer full (%u) -- "
                            "raise NET_MAX_INCOMING_REORDER\n", NET_MAX_INCOMING_REORDER);
                    exit(1);
                }
                if (inner_len > sizeof(slot->data)) {
                    fprintf(stderr, "net_channel: reorder-buffered packet too large\n");
                    exit(1);
                }
                slot->in_use = true;
                slot->seqnum = seqnum;
                slot->len = inner_len;
                memcpy(slot->data, inner, inner_len);
            }
        }
        /* else: stale duplicate of something already delivered -- the ACK
         * we just resent is the correct response, nothing else to do. */
        return;
    }

    /* ORIGINAL or SPLIT arriving with no RELIABLE wrapper at all (upstream
     * allows this, e.g. TOSERVER_INIT). */
    net_channel_deliver_inner(ch, data, len);
}
