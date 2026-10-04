#include "zstd_stream.h"

#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>

/* Starting guess for the output buffer. Deliberately not "src_len * N" --
 * zstd ratios on the payloads this is for (nodedef text, PNGs) vary too
 * wildly for that to be a better guess than a flat number. Grows from
 * here as needed, so getting this exactly right doesn't matter much;
 * it's just how many reallocs a call thrashes through before settling. */
#define ZSTD_STREAM_INITIAL_CAP (64u * 1024u)

bool zstd_stream_decompress(const uint8_t *src, size_t src_len,
                             size_t max_output,
                             uint8_t **out_data, size_t *out_len) {
    ZSTD_DStream *stream = ZSTD_createDStream();
    if (!stream) {
        fprintf(stderr, "zstd_stream: ZSTD_createDStream failed\n");
        exit(1);
    }
    ZSTD_initDStream(stream);

    size_t cap = ZSTD_STREAM_INITIAL_CAP;
    if (cap > max_output)
        cap = max_output;

    uint8_t *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "zstd_stream: malloc(%zu) failed\n", cap);
        exit(1);
    }

    ZSTD_inBuffer in = { src, src_len, 0 };
    size_t total = 0;

    for (;;) {
        ZSTD_outBuffer out = { buf, cap, total };
        size_t ret = ZSTD_decompressStream(stream, &out, &in);
        total = out.pos;

        if (ZSTD_isError(ret)) {
            fprintf(stderr, "zstd_stream: decompress failed: %s\n",
                    ZSTD_getErrorName(ret));
            free(buf);
            ZSTD_freeDStream(stream);
            exit(1);
        }

        if (ret == 0)
            break; /* frame complete */

        if (out.pos < out.size) {
            /* decompressStream returned "not done" without filling the
             * output buffer -- the only way that happens is it ran out
             * of INPUT first (in.pos == in.size). Since callers always
             * hand this a single complete buffer up front (no streaming
             * input, only streaming output), that means the frame is
             * truncated, not that more input is coming. */
            fprintf(stderr, "zstd_stream: input exhausted before frame "
                    "completed (truncated data?)\n");
            free(buf);
            ZSTD_freeDStream(stream);
            exit(1);
        }

        if (cap >= max_output) {
            /* Output buffer is full, frame isn't done, and we're already
             * at the caller's ceiling -- this is the one case that's the
             * caller's policy decision, not corruption. */
            free(buf);
            ZSTD_freeDStream(stream);
            *out_data = NULL;
            *out_len = 0;
            return false;
        }

        size_t new_cap = cap * 2;
        if (new_cap > max_output)
            new_cap = max_output;

        uint8_t *new_buf = realloc(buf, new_cap);
        if (!new_buf) {
            fprintf(stderr, "zstd_stream: realloc(%zu) failed\n", new_cap);
            free(buf);
            ZSTD_freeDStream(stream);
            exit(1);
        }
        buf = new_buf;
        cap = new_cap;
    }

    ZSTD_freeDStream(stream);
    *out_data = buf;
    *out_len = total;
    return true;
}
