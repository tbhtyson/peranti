/* --- net.c --- see net.h */
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "net_types.h"
#include "net_serialize.h"
#include "net_socket.h"
#include "net_channel.h"
#include "net_srp.h"

#define NET_PLAYERNAME_MAX 64u
#define NET_TOSERVER_INIT_RESEND_INTERVAL 0.5f /* seconds; unreliable, we retry ourselves */

/* Temporary diagnostic instrumentation for the CONTROLTYPE_DISCO-after-
 * TOSERVER_INIT investigation. Set to 0 once this is understood; it's
 * noisy by design (every byte of every send/recv) so it isn't meant to
 * stay on permanently. */
#define NET_DEBUG_TRACE 1

#if NET_DEBUG_TRACE
static void net_trace_hex(const char *label, const uint8_t *data, size_t len) {
    fprintf(stderr, "[net-trace] %s (%zu bytes):", label, len);
    for (size_t i = 0; i < len; ++i)
        fprintf(stderr, " %02x", data[i]);
    fprintf(stderr, "\n");
}
#endif

typedef enum {
    HS_BOOTSTRAP,        /* sent empty reliable packet, awaiting SET_PEER_ID */
    HS_AWAIT_HELLO,       /* peer id assigned, sent TOSERVER_INIT, awaiting TOCLIENT_HELLO */
    HS_AWAIT_SRP_S_B,     /* sent TOSERVER_SRP_BYTES_A, awaiting TOCLIENT_SRP_BYTES_S_B */
    HS_AWAIT_AUTH_ACCEPT, /* sent TOSERVER_SRP_BYTES_M, awaiting TOCLIENT_AUTH_ACCEPT */
    HS_INGAME
} HandshakeStage;

struct Net {
    NetSocket sock;
    NetChannel channels[NET_CHANNEL_COUNT];
    uint16_t peer_id;

    NetConnState state;
    HandshakeStage stage;
    char error[256];

    char playername[NET_PLAYERNAME_MAX];
    NetSrpClient *srp;

    uint8_t init_payload[2 + 1 + 2 + 2 + 2 + 2 + NET_PLAYERNAME_MAX]; /* opcode, ser_ver,
        unused, min_proto, max_proto, str16 len prefix, playername */
    size_t init_payload_len;
    float init_resend_timer;

    /* From TOCLIENT_AUTH_ACCEPT -- how often a real client throttles its
     * own TOSERVER_PLAYERPOS sends. Previously parsed and discarded; now
     * kept since the caller needs it to pace position updates (see
     * net_get_recommended_send_interval()). Defaults to something sane in
     * case a server ever sends 0 or omits pacing intent. */
    float recommendedSendInterval;

    /* From TOCLIENT_HELLO -- see net_get_server_ser_ver()'s doc comment
     * in net.h for why this matters for MapBlock decoding specifically. */
    uint8_t serverSerVer;

    NetGameplayHandler handler;
    void *handler_user_data;
};

static void net_fail(Net *net, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(net->error, sizeof(net->error), fmt, ap);
    va_end(ap);
    net->state = NET_CONN_FAILED;
    fprintf(stderr, "[net] connection failed: %s\n", net->error);
}

/* Sends `opcode` + `body` reliably or not, on the given channel, prefixing
 * the u16 opcode the caller doesn't have to. Used both internally for
 * handshake packets and by net_send_gameplay(). */
static void net_send_opcode(Net *net, uint8_t channel_num, bool reliable,
                             uint16_t opcode, const void *body, size_t body_len) {
    uint8_t buf[NET_MAX_DATAGRAM_SIZE];
    NetWriter w;
    net_writer_init(&w, buf, sizeof(buf));
    net_put_u16(&w, opcode);
    net_put_bytes(&w, body, body_len);

#if NET_DEBUG_TRACE
    fprintf(stderr, "[net-trace] sending opcode=0x%04x channel=%u reliable=%d peer_id=%u\n",
            (unsigned)opcode, (unsigned)channel_num, (int)reliable, (unsigned)net->peer_id);
    net_trace_hex("  opcode+body", buf, w.pos);
#endif

    if (reliable)
        net_channel_send_reliable(&net->channels[channel_num], &net->sock, net->peer_id,
                                   channel_num, buf, w.pos);
    else
        net_channel_send_unreliable(&net->channels[channel_num], &net->sock, net->peer_id,
                                     channel_num, buf, w.pos);
}

static void net_build_init_payload(Net *net) {
    NetWriter w;
    net_writer_init(&w, net->init_payload, sizeof(net->init_payload));
    net_put_u16(&w, NET_TOSERVER_INIT); /* the missing opcode prefix -- see
                                          * NET_DEBUG_TRACE investigation */
    net_put_u8(&w, (uint8_t)NET_SER_FMT_VER_HIGHEST_READ);
    net_put_u16(&w, 0); /* unused: network compression, never implemented upstream */
    net_put_u16(&w, (uint16_t)NET_CLIENT_PROTOCOL_VERSION_MIN);
    net_put_u16(&w, (uint16_t)NET_LATEST_PROTOCOL_VERSION);
    net_put_str16(&w, net->playername, strlen(net->playername));
    net->init_payload_len = w.pos;
}

Net *net_create(const char *host, uint16_t port, const char *playername, const char *password) {
    if (strlen(playername) == 0 || strlen(playername) >= NET_PLAYERNAME_MAX) {
        fprintf(stderr, "net: playername must be 1..%u bytes\n", NET_PLAYERNAME_MAX - 1);
        exit(1);
    }

    Net *net = calloc(1, sizeof(Net));
    if (!net) { fprintf(stderr, "net: OOM\n"); exit(1); }

    net_socket_open(&net->sock, host, port); /* hard-fails on address/socket errors */

    for (size_t i = 0; i < NET_CHANNEL_COUNT; ++i)
        net_channel_init(&net->channels[i]);

    net->peer_id = NET_PEER_ID_INEXISTENT;
    net->state = NET_CONN_CONNECTING;
    net->stage = HS_BOOTSTRAP;

    /* Real clients default to something reasonable before the server's own
     * TOCLIENT_AUTH_ACCEPT value is known; 0.2s matches upstream's typical
     * ballpark default. */
    net->recommendedSendInterval = 0.2f;

    strncpy(net->playername, playername, sizeof(net->playername) - 1);
    net->srp = net_srp_client_new(playername, password);

    net_build_init_payload(net);

    /* Bootstrap: an empty reliable ORIGINAL packet, peer_id=0, channel 0.
     * The server replies with CONTROLTYPE_SET_PEER_ID (also wrapped in
     * RELIABLE) assigning us a real peer id. */
#if NET_DEBUG_TRACE
    fprintf(stderr, "[net-trace] sending bootstrap packet (empty reliable, peer_id=0, channel=0)\n");
#endif
    net_channel_send_reliable(&net->channels[0], &net->sock, NET_PEER_ID_INEXISTENT, 0, NULL, 0);

    return net;
}

void net_destroy(Net *net) {
    if (!net) return;
    for (size_t i = 0; i < NET_CHANNEL_COUNT; ++i)
        net_channel_destroy(&net->channels[i]);
    net_srp_client_free(net->srp);
    net_socket_close(&net->sock);
    free(net);
}

void net_set_gameplay_handler(Net *net, NetGameplayHandler handler, void *user_data) {
    net->handler = handler;
    net->handler_user_data = user_data;
}

NetConnState net_get_state(const Net *net) { return net->state; }
const char *net_get_last_error(const Net *net) { return net->error; }

/* From TOCLIENT_AUTH_ACCEPT (or a sane default before that arrives) -- how
 * often the caller should send TOSERVER_PLAYERPOS, matching how a real
 * client throttles it (see net.h's note on why we send it at all). */
float net_get_recommended_send_interval(const Net *net) { return net->recommendedSendInterval; }

/* From TOCLIENT_HELLO. TOCLIENT_BLOCKDATA's serialized MapBlock has no
 * leading version byte of its own -- unlike the on-disk map.sqlite format,
 * which does -- so this cached, session-wide value is the only way to know
 * which MapBlock::deSerialize path applies. Decoding a block without this
 * (e.g. assuming the disk format's leading-byte convention instead) eats
 * one real byte of the zstd stream and corrupts everything after it. */
uint8_t net_get_server_ser_ver(const Net *net) { return net->serverSerVer; }

static void net_handle_control_event(Net *net, NetControlEvent ev) {
    if (ev.type == NET_CTRL_EVENT_DISCO) {
        net_fail(net, "server sent CONTROLTYPE_DISCO");
        return;
    }
    /* NET_CTRL_EVENT_SET_PEER_ID */
    if (net->stage != HS_BOOTSTRAP) {
        fprintf(stderr, "net: unexpected SET_PEER_ID mid-session (had=%u new=%u), ignoring\n",
                (unsigned)net->peer_id, (unsigned)ev.peer_id_new);
        return;
    }
    net->peer_id = ev.peer_id_new;
    net->stage = HS_AWAIT_HELLO;
    net->init_resend_timer = NET_TOSERVER_INIT_RESEND_INTERVAL; /* fire immediately below */
    printf("[net] peer_id assigned: %u\n", (unsigned)net->peer_id);
}

static void net_handle_hello(Net *net, NetReader *r) {
    uint8_t deployed_ser_ver = net_get_u8(r);
    net_get_u16(r); /* unused */
    uint16_t deployed_proto_ver = net_get_u16(r);
    uint32_t auth_mechs = net_get_u32(r);

    /* This is the ONLY place the serialization version is ever
     * communicated for network traffic -- unlike the on-disk map.sqlite
     * format, TOCLIENT_BLOCKDATA carries no leading version byte of its
     * own (confirmed against Client::handleCommand_BlockData, which
     * passes m_server_ser_ver -- cached from here -- into deSerialize()).
     * Getting this confused with the disk format's leading version byte
     * would eat one real byte of the zstd stream and corrupt every block. */
    net->serverSerVer = deployed_ser_ver;

    if (deployed_proto_ver < NET_CLIENT_PROTOCOL_VERSION_MIN) {
        fprintf(stderr, "net: server protocol version %u is below our minimum %u\n",
                deployed_proto_ver, NET_CLIENT_PROTOCOL_VERSION_MIN);
        exit(1);
    }

    if (!(auth_mechs & NET_AUTH_MECHANISM_SRP)) {
        net_fail(net, "server does not offer AUTH_MECHANISM_SRP (only legacy/first-time "
                       "registration) -- unsupported by this client");
        return;
    }

    const uint8_t *A;
    size_t A_len;
    net_srp_client_start(net->srp, &A, &A_len);

    uint8_t body[NET_MAX_DATAGRAM_SIZE];
    NetWriter w;
    net_writer_init(&w, body, sizeof(body));
    net_put_str16(&w, (const char *)A, A_len);
    net_put_u8(&w, NET_SRP_LOGIN_BASED_ON_PASSWORD);

    net_send_opcode(net, 1, true, NET_TOSERVER_SRP_BYTES_A, body, w.pos);
    net->stage = HS_AWAIT_SRP_S_B;
    printf("[net] TOCLIENT_HELLO received (proto=%u) -- starting SRP-6a auth\n",
           (unsigned)deployed_proto_ver);
}

static void net_handle_srp_s_b(Net *net, NetReader *r) {
    uint16_t s_len, B_len;
    const uint8_t *s = net_get_str16(r, &s_len);
    const uint8_t *B = net_get_str16(r, &B_len);

    const uint8_t *M;
    size_t M_len;
    bool ok = net_srp_client_process_challenge(net->srp, s, s_len, B, B_len, &M, &M_len);
    if (!ok) {
        net_fail(net, "SRP-6a safety check failed (malformed server challenge, or wrong "
                       "username/password)");
        return;
    }

    uint8_t body[NET_MAX_DATAGRAM_SIZE];
    NetWriter w;
    net_writer_init(&w, body, sizeof(body));
    net_put_str16(&w, (const char *)M, M_len);

    net_send_opcode(net, 1, true, NET_TOSERVER_SRP_BYTES_M, body, w.pos);
    net->stage = HS_AWAIT_AUTH_ACCEPT;
    printf("[net] SRP challenge verified -- sending proof, awaiting server accept\n");
}

static void net_handle_auth_accept(Net *net, NetReader *r) {
    /* v3f unused */
    net_get_f32(r); net_get_f32(r); net_get_f32(r);
    uint64_t map_seed = net_get_u64(r);
    float send_interval = net_get_f32(r);
    if (send_interval > 0.0f)
        net->recommendedSendInterval = send_interval;
    /* else: keep whatever net_create() defaulted it to -- a server sending
     * 0 or garbage here shouldn't make us spam position updates every
     * frame. */
    /* sudo_auth_mechs (u32) may or may not be present depending on server
     * version; we don't need it to finish the handshake. */

    net_send_opcode(net, 1, true, NET_TOSERVER_INIT2, NULL, 0);
    net->stage = HS_INGAME;
    net->state = NET_CONN_INGAME;
    printf("[net] auth accepted -- connected as peer_id=%u, map_seed=%llu\n",
           (unsigned)net->peer_id, (unsigned long long)map_seed);
}

static void net_handle_access_denied(Net *net, NetReader *r) {
    uint8_t reason = net_get_u8(r);
    uint16_t msg_len;
    const uint8_t *msg = net_get_str16(r, &msg_len);
    net_fail(net, "server denied access (reason=%u): %.*s", (unsigned)reason,
              (int)msg_len, (const char *)msg);
}

static void net_handle_app_payload(Net *net, const uint8_t *data, size_t len) {
    if (len < 2) {
        fprintf(stderr, "net: application payload shorter than its own opcode\n");
        exit(1);
    }
    NetReader r;
    net_reader_init(&r, data, len);
    uint16_t opcode = net_get_u16(&r);

    if (net->stage == HS_INGAME) {
        if (net->handler)
            net->handler(opcode, data + 2, len - 2, net->handler_user_data);
        return;
    }

    if (opcode == NET_TOCLIENT_ACCESS_DENIED) {
        net_handle_access_denied(net, &r);
        return;
    }

    switch (net->stage) {
        case HS_AWAIT_HELLO:
            if (opcode == NET_TOCLIENT_HELLO) net_handle_hello(net, &r);
            break;
        case HS_AWAIT_SRP_S_B:
            if (opcode == NET_TOCLIENT_SRP_BYTES_S_B) net_handle_srp_s_b(net, &r);
            break;
        case HS_AWAIT_AUTH_ACCEPT:
            if (opcode == NET_TOCLIENT_AUTH_ACCEPT) net_handle_auth_accept(net, &r);
            break;
        default:
            break;
    }
    /* Unrecognized opcodes for the current stage are logged and dropped
     * rather than treated as fatal -- unlike malformed framing, an
     * out-of-sequence but well-formed opcode is a protocol-level surprise
     * we can just ignore and keep waiting for what we actually expect. */
}

void net_poll(Net *net, float dtime) {
    if (net->state == NET_CONN_DISCONNECTED || net->state == NET_CONN_FAILED)
        return;

    uint8_t buf[NET_MAX_DATAGRAM_SIZE];
    for (;;) {
        int n = net_socket_recv(&net->sock, buf, sizeof(buf));
        if (n <= 0)
            break;
        if ((size_t)n < NET_BASE_HEADER_SIZE) {
            fprintf(stderr, "net: datagram shorter than base header, dropping\n");
            continue;
        }
#if NET_DEBUG_TRACE
        net_trace_hex("recv raw datagram", buf, (size_t)n);
#endif
        NetReader r;
        net_reader_init(&r, buf, (size_t)n);
        uint32_t protocol_id = net_get_u32(&r);
        if (protocol_id != NET_PROTOCOL_ID) {
            fprintf(stderr, "net: bad protocol_id 0x%08x (wrong server, or garbage)\n",
                    protocol_id);
            exit(1);
        }
        net_get_u16(&r); /* sender_peer_id: trust the connected UDP socket's source instead */
        uint8_t channel_num = net_get_u8(&r);
        if (channel_num >= NET_CHANNEL_COUNT) {
            fprintf(stderr, "net: channel %u out of range\n", channel_num);
            exit(1);
        }

        net_channel_on_datagram(&net->channels[channel_num], &net->sock, net->peer_id,
                                 channel_num, buf + NET_BASE_HEADER_SIZE,
                                 (size_t)n - NET_BASE_HEADER_SIZE);
    }

    for (size_t i = 0; i < NET_CHANNEL_COUNT; ++i) {
        NetControlEvent ev;
        while (net_channel_poll_control_event(&net->channels[i], &ev))
            net_handle_control_event(net, ev);
    }

    for (size_t i = 0; i < NET_CHANNEL_COUNT; ++i) {
        NetInboundPacket pkt;
        while (net_channel_poll_ready(&net->channels[i], &pkt)) {
            net_handle_app_payload(net, pkt.payload, pkt.len);
            free(pkt.payload); /* we now own this -- see net_channel.h's
                                 * ownership note on NetInboundPacket */
        }
    }

    if (net->state == NET_CONN_FAILED)
        return;

    for (size_t i = 0; i < NET_CHANNEL_COUNT; ++i) {
        if (!net_channel_tick(&net->channels[i], &net->sock, net->peer_id, (uint8_t)i, dtime)) {
            net_fail(net, "connection timed out (peer stopped acking)");
            return;
        }
    }

    if (net->stage == HS_AWAIT_HELLO) {
        net->init_resend_timer += dtime;
        if (net->init_resend_timer >= NET_TOSERVER_INIT_RESEND_INTERVAL) {
            net->init_resend_timer = 0.0f;
            net_channel_send_unreliable(&net->channels[1], &net->sock, net->peer_id, 1,
                                         net->init_payload, net->init_payload_len);
        }
    }
}

void net_send_gameplay(Net *net, uint16_t opcode, const uint8_t *payload, size_t len,
                        uint8_t channel_num, bool reliable) {
    if (net->state != NET_CONN_INGAME) {
        fprintf(stderr, "net: net_send_gameplay() called before NET_CONN_INGAME\n");
        exit(1);
    }
    if (channel_num >= NET_CHANNEL_COUNT) {
        fprintf(stderr, "net: channel %u out of range\n", channel_num);
        exit(1);
    }
    net_send_opcode(net, channel_num, reliable, opcode, payload, len);
}
