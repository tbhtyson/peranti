#include "media_sync.h"
#include "zstd_stream.h"
#include "sha1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define NET_TOSERVER_REQUEST_MEDIA 0x40u

/* Ceiling for the decompressed ANNOUNCE_MEDIA name table. Even an
 * enormous modpack's file list is tiny text, so this is generous on
 * purpose, matching content.c's own CONTENT_DECOMPRESSED_CEILING
 * reasoning for the same kind of blob. */
#define NAMES_DECOMPRESSED_CEILING (16u * 1024u * 1024u)

/* Ceiling for a single decompressed media file. Textures are small but
 * models/sounds can be a few MB; this is independent from the names
 * ceiling above since it bounds a very different kind of payload. */
#define FILE_DECOMPRESSED_CEILING (64u * 1024u * 1024u)

/* Sanity cap on ANNOUNCE_MEDIA's claimed file count -- defends the
 * upcoming malloc(sizeof(uint16_t) * count) against a corrupt or hostile
 * count field. No real server needs anywhere near this many files. */
#define MAX_ANNOUNCED_NAMES 1000000u

/* Largest TOSERVER_REQUEST_MEDIA body this project's send path can
 * carry in one packet: NET_MAX_DATAGRAM_SIZE(512) minus net_send_framed's
 * envelope (protocol_id u32 + peer_id u16 + channel u8 = 7) minus the
 * RELIABLE wrapper (type u8 + seqnum u16 = 3) minus the ORIGINAL wrapper
 * (type u8 = 1) minus the opcode net_send_gameplay prepends (u16 = 2):
 * 512 - 7 - 3 - 1 - 2 = 499. Traced through net_send_framed/
 * net_channel_send_reliable/net_send_opcode's actual buffers, not
 * guessed -- see media_sync.h's doc comment on why this project needs to
 * batch at all when upstream doesn't. */
#define REQUEST_BODY_BUDGET 499u

/* ---------- small helpers ---------- */

static bool name_is_allowed(const uint8_t *name, size_t len) {
    if (len == 0)
        return false;
    for (size_t i = 0; i < len; ++i) {
        uint8_t c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!ok)
            return false;
    }
    return true;
}

static MediaEntry *find_entry(MediaSync *m, const uint8_t *name, size_t len) {
    for (size_t i = 0; i < m->entry_count; ++i) {
        if (strlen(m->entries[i].name) == len && memcmp(m->entries[i].name, name, len) == 0)
            return &m->entries[i];
    }
    return NULL;
}

static void table_append_entry(MediaSync *m, const uint8_t *name, size_t name_len,
                                const uint8_t hash[20]) {
    if (m->entry_count == m->entry_capacity) {
        size_t new_cap = m->entry_capacity == 0 ? 64 : m->entry_capacity * 2;
        MediaEntry *new_entries = realloc(m->entries, sizeof(MediaEntry) * new_cap);
        if (!new_entries) {
            fprintf(stderr, "media_sync: out of memory growing entry table to %zu\n", new_cap);
            exit(1);
        }
        m->entries = new_entries;
        m->entry_capacity = new_cap;
    }
    MediaEntry *e = &m->entries[m->entry_count++];
    e->name = malloc(name_len + 1);
    if (!e->name) {
        fprintf(stderr, "media_sync: out of memory duplicating a %zu-byte name\n", name_len);
        exit(1);
    }
    memcpy(e->name, name, name_len);
    e->name[name_len] = '\0';
    memcpy(e->sha1, hash, 20);
    e->satisfied = false;
}

static void free_entries(MediaSync *m) {
    for (size_t i = 0; i < m->entry_count; ++i)
        free(m->entries[i].name);
    free(m->entries);
    m->entries = NULL;
    m->entry_count = 0;
    m->entry_capacity = 0;
    m->satisfied_count = 0;
}

static void free_pending(MediaSync *m) {
    free(m->pending_batches);
    m->pending_batches = NULL;
    m->pending_count = 0;
    m->next_pending = 0;
}

void media_sync_init(MediaSync *m) {
    memset(m, 0, sizeof(*m));
}

void media_sync_free(MediaSync *m) {
    free_entries(m);
    free_pending(m);
    memset(m, 0, sizeof(*m));
}

bool media_sync_is_complete(const MediaSync *m) {
    return m->announced && m->satisfied_count == m->entry_count;
}

/* ---------- on-disk cache ---------- */

bool media_sync_cache_path(const uint8_t sha1_bytes[20], char *out_path, size_t out_cap) {
    const char *home = getenv("HOME");
    if (!home)
        return false;
    char hex[41];
    for (int i = 0; i < 20; ++i)
        snprintf(hex + i * 2, 3, "%02x", sha1_bytes[i]);
    int n = snprintf(out_path, out_cap, "%s/.cache/peranti/media/%s", home, hex);
    return n > 0 && (size_t)n < out_cap;
}

static void mkdir_tolerant(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "media_sync: mkdir('%s') failed: %s\n", path, strerror(errno));
        exit(1);
    }
}

static void ensure_cache_dir(void) {
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "media_sync: $HOME not set, cannot create media cache directory\n");
        exit(1);
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/.cache", home);
    mkdir_tolerant(path);
    snprintf(path, sizeof(path), "%s/.cache/peranti", home);
    mkdir_tolerant(path);
    snprintf(path, sizeof(path), "%s/.cache/peranti/media", home);
    mkdir_tolerant(path);
}

/* Re-verifies the SHA-1 of a cache hit before trusting it, same as
 * upstream's checkAndLoad() does even for is_from_cache == true --
 * defense against a corrupted or tampered-with cache directory, not
 * just against the network. */
static bool try_load_from_cache(const MediaEntry *e) {
    char path[512];
    if (!media_sync_cache_path(e->sha1, path, sizeof(path)))
        return false;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return false;
    }
    rewind(f);

    uint8_t *buf = malloc((size_t)size);
    if (!buf) {
        fprintf(stderr, "media_sync: out of memory reading cached file (%ld bytes)\n", size);
        exit(1);
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(buf);
        return false;
    }

    uint8_t actual[20];
    sha1(buf, got, actual);
    bool match = memcmp(actual, e->sha1, 20) == 0;
    free(buf);
    return match;
}

/* ---------- outgoing request batching ---------- */

static void append_pending_batch(MediaSync *m, const uint8_t *body, size_t len) {
    MediaRequestBatch *new_batches =
        realloc(m->pending_batches, sizeof(MediaRequestBatch) * (m->pending_count + 1));
    if (!new_batches) {
        fprintf(stderr, "media_sync: out of memory queuing a request batch\n");
        exit(1);
    }
    m->pending_batches = new_batches;
    memcpy(m->pending_batches[m->pending_count].body, body, len);
    m->pending_batches[m->pending_count].len = len;
    m->pending_count++;
}

static void build_request_batches(MediaSync *m) {
    uint8_t names_buf[REQUEST_BODY_BUDGET - 2]; /* -2 for the u16 count prefix */
    size_t names_used = 0;
    uint16_t names_count = 0;

    for (size_t i = 0; i < m->entry_count; ++i) {
        if (m->entries[i].satisfied)
            continue;

        size_t name_len = strlen(m->entries[i].name);
        size_t cost = 2 + name_len; /* string16: u16 length + bytes */

        if (cost > sizeof(names_buf)) {
            /* A single filename that could never fit in any batch on its
             * own. Upstream has no length limit on media names beyond
             * string16's 65535 bytes, so a pathological server could hit
             * this. Dropping just this one file (logged) is consistent
             * with how a checksum mismatch is handled below -- one bad
             * file shouldn't block every other file from syncing. */
            fprintf(stderr, "media_sync: NOTE: filename '%s' (%zu bytes) can never fit in a "
                    "request batch, skipping it\n", m->entries[i].name, name_len);
            continue;
        }

        if (names_used + cost > sizeof(names_buf)) {
            uint8_t body[REQUEST_BODY_BUDGET];
            NetWriter w;
            net_writer_init(&w, body, sizeof(body));
            net_put_u16(&w, names_count);
            net_put_bytes(&w, names_buf, names_used);
            append_pending_batch(m, body, w.pos);
            names_used = 0;
            names_count = 0;
        }

        NetWriter nw;
        net_writer_init(&nw, names_buf + names_used, sizeof(names_buf) - names_used);
        net_put_str16(&nw, m->entries[i].name, name_len);
        names_used += nw.pos;
        names_count++;
    }

    if (names_count > 0) {
        uint8_t body[REQUEST_BODY_BUDGET];
        NetWriter w;
        net_writer_init(&w, body, sizeof(body));
        net_put_u16(&w, names_count);
        net_put_bytes(&w, names_buf, names_used);
        append_pending_batch(m, body, w.pos);
    }
}

void media_sync_tick(MediaSync *m, Net *net) {
    while (m->next_pending < m->pending_count) {
        if (net_get_free_outgoing_slots(net, /*channel=*/1) == 0)
            break;
        MediaRequestBatch *b = &m->pending_batches[m->next_pending];
        net_send_gameplay(net, NET_TOSERVER_REQUEST_MEDIA, b->body, b->len,
                           /*channel=*/1, /*reliable=*/true);
        m->next_pending++;
    }
}

/* ---------- packet handlers ---------- */

void media_sync_on_announce(MediaSync *m, Net *net, NetReader *r) {
    /* Matches upstream's one-shot ClientMediaDownloader design -- see
     * media_sync.h's doc comment on why starting over rather than
     * merging is the right call if this ever fires twice. */
    free_entries(m);
    free_pending(m);
    m->announced = true;

    uint32_t compressed_len;
    const uint8_t *compressed = net_get_str32(r, &compressed_len);
    uint8_t *names_data;
    size_t names_len;
    if (!zstd_stream_decompress(compressed, compressed_len, NAMES_DECOMPRESSED_CEILING,
                                 &names_data, &names_len)) {
        fprintf(stderr, "media_sync: ANNOUNCE_MEDIA name table exceeds %u-byte ceiling\n",
                (unsigned)NAMES_DECOMPRESSED_CEILING);
        exit(1);
    }

    NetReader nr;
    net_reader_init(&nr, names_data, names_len);

    /* deserializeString16Array's actual layout (checked against
     * util/serialize.cpp, not assumed): u32 count, then ALL `count`
     * u16 lengths first, THEN all the raw name bytes concatenated --
     * NOT interleaved length+bytes pairs the way a normal string16
     * array would read field-by-field. */
    uint32_t count = net_get_u32(&nr);
    if (count > MAX_ANNOUNCED_NAMES) {
        fprintf(stderr, "media_sync: ANNOUNCE_MEDIA claims %u names, refusing as absurd "
                "(raise MAX_ANNOUNCED_NAMES if a real server legitimately needs this)\n",
                (unsigned)count);
        exit(1);
    }

    uint16_t *lengths = NULL;
    if (count > 0) {
        lengths = malloc(sizeof(uint16_t) * count);
        if (!lengths) {
            fprintf(stderr, "media_sync: out of memory allocating %u name lengths\n",
                    (unsigned)count);
            exit(1);
        }
        for (uint32_t i = 0; i < count; ++i)
            lengths[i] = net_get_u16(&nr);
    }

    for (uint32_t i = 0; i < count; ++i) {
        net_read_require(&nr, lengths[i]);
        const uint8_t *name_bytes = nr.data + nr.pos;
        nr.pos += lengths[i];

        /* The raw 20-byte SHA-1 for this name comes from the OUTER
         * packet reader `r`, not the decompressed names blob -- two
         * different streams at this point in the packet (see this
         * file's doc comment / content.c's framing notes). Read
         * unconditionally so a rejected name below still consumes its
         * hash and the stream stays in sync for the next name. */
        uint8_t hash[20];
        net_get_bytes(r, hash, sizeof(hash));

        if (!name_is_allowed(name_bytes, lengths[i])) {
            fprintf(stderr, "media_sync: NOTE: ignoring illegal media filename from server\n");
            continue;
        }
        if (find_entry(m, name_bytes, lengths[i])) {
            fprintf(stderr, "media_sync: NOTE: ignoring duplicate media announcement\n");
            continue;
        }
        table_append_entry(m, name_bytes, lengths[i], hash);
    }
    free(lengths);
    free(names_data);

    /* Trailing remote-server list -- read so the reader ends up
     * correctly positioned; contents discarded (no HTTP remote-media
     * support, see this file's doc comment). */
    uint16_t remote_len;
    (void)net_get_str16(r, &remote_len);

    for (size_t i = 0; i < m->entry_count; ++i) {
        if (try_load_from_cache(&m->entries[i])) {
            m->entries[i].satisfied = true;
            m->satisfied_count++;
        }
    }

    build_request_batches(m);
    media_sync_tick(m, net);

    printf("[media_sync] announced %zu files, %zu already cached, %zu queued across %zu "
           "request batch(es)\n", m->entry_count, m->satisfied_count,
           m->entry_count - m->satisfied_count, m->pending_count);
}

void media_sync_on_media(MediaSync *m, Net *net, NetReader *r) {
    (void)net; /* not currently needed; kept for symmetry with media_sync_on_announce */

    uint16_t total_bunches = net_get_u16(r);
    uint16_t bunch_index = net_get_u16(r);
    uint32_t nfiles = net_get_u32(r);

    for (uint32_t i = 0; i < nfiles; ++i) {
        uint16_t name_len;
        const uint8_t *name_bytes = net_get_str16(r, &name_len);

        uint32_t compressed_len;
        const uint8_t *compressed = net_get_str32(r, &compressed_len);
        uint8_t *data;
        size_t data_len;
        if (!zstd_stream_decompress(compressed, compressed_len, FILE_DECOMPRESSED_CEILING,
                                     &data, &data_len)) {
            fprintf(stderr, "media_sync: NOTE: a file in bunch %u/%u exceeds the %u-byte "
                    "ceiling, skipping it\n", (unsigned)(bunch_index + 1),
                    (unsigned)total_bunches, (unsigned)FILE_DECOMPRESSED_CEILING);
            continue;
        }

        MediaEntry *e = find_entry(m, name_bytes, name_len);
        if (!e) {
            fprintf(stderr, "media_sync: NOTE: server sent an unannounced file, ignoring\n");
            free(data);
            continue;
        }
        if (e->satisfied) {
            /* Already have it -- duplicate delivery, matches upstream's
             * tolerance for this exact case. */
            free(data);
            continue;
        }

        uint8_t actual[20];
        sha1(data, data_len, actual);
        if (memcmp(actual, e->sha1, 20) != 0) {
            fprintf(stderr, "media_sync: NOTE: checksum mismatch for '%s', discarding "
                    "(matches upstream's checkAndLoad -- not fatal)\n", e->name);
            free(data);
            continue;
        }

        ensure_cache_dir();
        char path[512];
        if (media_sync_cache_path(e->sha1, path, sizeof(path))) {
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(data, 1, data_len, f);
                fclose(f);
            } else {
                fprintf(stderr, "media_sync: NOTE: failed to write cache file for '%s'\n",
                        e->name);
            }
        }

        e->satisfied = true;
        m->satisfied_count++;
        free(data);
    }

    printf("[media_sync] TOCLIENT_MEDIA bunch %u/%u: %u files, %zu/%zu total satisfied\n",
           (unsigned)(bunch_index + 1), (unsigned)total_bunches, (unsigned)nfiles,
           m->satisfied_count, m->entry_count);
}
