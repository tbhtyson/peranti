#include "texture_name.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_bytes(const char *src, size_t len) {
    char *s = malloc(len + 1);
    if (!s) {
        fprintf(stderr, "texture_name: out of memory duplicating a %zu-byte string\n", len);
        exit(1);
    }
    if (len > 0)
        memcpy(s, src, len);
    s[len] = '\0';
    return s;
}

static bool is_modifier_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static void texture_layer_free(TexLayer *layer) {
    switch (layer->kind) {
    case TEX_LAYER_IMAGE:
        free(layer->as.image_name);
        break;
    case TEX_LAYER_GROUP:
        if (layer->as.group) {
            texture_spec_free(layer->as.group);
            free(layer->as.group);
        }
        break;
    case TEX_LAYER_MODIFIER:
        free(layer->as.modifier.name);
        free(layer->as.modifier.args);
        break;
    }
}

void texture_spec_free(TexSpec *spec) {
    for (size_t i = 0; i < spec->layer_count; ++i)
        texture_layer_free(&spec->layers[i]);
    free(spec->layers);
    spec->layers = NULL;
    spec->layer_count = 0;
}

static bool parse_spec(const char *name, size_t len, TexSpec *out);

/* Classifies one '^'-delimited segment (NOT including the '^' itself) --
 * empty, a "(...)" group, a "[..." modifier, or a plain filename. Returns
 * false only if a nested group's own parse_spec() call fails (unbalanced
 * parens inside it); the caller has already validated overall paren
 * balance before segments are even cut, so that's the only failure path
 * left at this level. */
static bool classify_segment(const char *seg, size_t len, TexLayer *layer) {
    if (len >= 2 && seg[0] == '(' && seg[len - 1] == ')') {
        /* See texture_name.h's doc comment on the "(a)(b)" quirk this
         * single first/last-byte check inherits from upstream, on
         * purpose. */
        layer->kind = TEX_LAYER_GROUP;
        layer->as.group = malloc(sizeof(TexSpec));
        if (!layer->as.group) {
            fprintf(stderr, "texture_name: out of memory\n");
            exit(1);
        }
        if (!parse_spec(seg + 1, len - 2, layer->as.group)) {
            free(layer->as.group);
            layer->as.group = NULL;
            return false;
        }
        return true;
    }

    if (len >= 1 && seg[0] == '[') {
        layer->kind = TEX_LAYER_MODIFIER;
        size_t name_len = 1;
        while (name_len < len && is_modifier_name_char(seg[name_len]))
            name_len++;
        layer->as.modifier.name = dup_bytes(seg + 1, name_len - 1);
        if (name_len < len && seg[name_len] == ':') {
            size_t args_start = name_len + 1;
            layer->as.modifier.args = dup_bytes(seg + args_start, len - args_start);
        } else {
            layer->as.modifier.args = dup_bytes("", 0);
        }
        return true;
    }

    /* Plain image filename -- this also covers the empty-segment case
     * (len == 0 produces an empty-string "filename"); parse_spec() below
     * filters those out before they ever reach here, so in practice this
     * path always sees len >= 1, but there's nothing unsafe about it
     * handling len == 0 too. */
    layer->kind = TEX_LAYER_IMAGE;
    layer->as.image_name = dup_bytes(seg, len);
    return true;
}

/* Finds every top-level (paren-balance == 0, not backslash-escaped) '^'
 * in name[0..len), appending each one's byte offset to *bounds (growable,
 * realloc'd). Returns false on unbalanced parentheses -- an unmatched
 * ')' (balance would go negative) or leftover unmatched '(' at the end
 * (balance != 0 when the scan finishes). An escaped character (preceded
 * by an unescaped '\\') never affects paren balance OR separator
 * counting, matching upstream exactly -- including upstream's own
 * simplistic escaping (a single backslash lookback, no doubled-backslash
 * handling for "a literal backslash followed by a real separator"). */
static bool find_top_level_carets(const char *name, size_t len,
                                   size_t **bounds, size_t *bound_count) {
    size_t cap = 8;
    size_t count = 0;
    size_t *b = malloc(sizeof(size_t) * cap);
    if (!b) {
        fprintf(stderr, "texture_name: out of memory\n");
        exit(1);
    }

    int paren_bal = 0;
    for (size_t i = 0; i < len; ++i) {
        bool escaped = (i > 0 && name[i - 1] == '\\');
        if (escaped)
            continue;

        char c = name[i];
        if (c == '(') {
            paren_bal++;
        } else if (c == ')') {
            paren_bal--;
            if (paren_bal < 0) {
                free(b);
                return false; /* extraneous ')' */
            }
        } else if (c == '^' && paren_bal == 0) {
            if (count == cap) {
                cap *= 2;
                size_t *nb = realloc(b, sizeof(size_t) * cap);
                if (!nb) {
                    fprintf(stderr, "texture_name: out of memory\n");
                    exit(1);
                }
                b = nb;
            }
            b[count++] = i;
        }
    }

    if (paren_bal != 0) {
        free(b); /* missing matching ')' for some earlier '(' */
        return false;
    }

    *bounds = b;
    *bound_count = count;
    return true;
}

static bool parse_spec(const char *name, size_t len, TexSpec *out) {
    out->layers = NULL;
    out->layer_count = 0;

    size_t *bounds;
    size_t bound_count;
    if (!find_top_level_carets(name, len, &bounds, &bound_count)) {
        fprintf(stderr, "texture_name: unbalanced parentheses in texture string '%.*s'\n",
                (int)len, name);
        return false;
    }

    size_t seg_count = bound_count + 1;
    TexLayer *layers = malloc(sizeof(TexLayer) * seg_count);
    if (!layers) {
        fprintf(stderr, "texture_name: out of memory\n");
        exit(1);
    }

    size_t out_count = 0;
    size_t seg_start = 0;
    bool ok = true;
    for (size_t s = 0; s < seg_count && ok; ++s) {
        size_t seg_end = (s < bound_count) ? bounds[s] : len;
        size_t seg_len = seg_end - seg_start;

        /* An empty segment is a harmless no-op layer upstream (keeps
         * baseimg as whatever it already was) -- simplest to just not
         * emit a layer for it at all, rather than giving callers an
         * "empty filename" layer to special-case. */
        if (seg_len > 0) {
            if (!classify_segment(name + seg_start, seg_len, &layers[out_count])) {
                ok = false;
                break;
            }
            out_count++;
        }

        seg_start = seg_end + 1; /* skip the '^' itself */
    }

    free(bounds);

    if (!ok) {
        for (size_t k = 0; k < out_count; ++k)
            texture_layer_free(&layers[k]);
        free(layers);
        return false;
    }

    out->layers = layers;
    out->layer_count = out_count;
    return true;
}

bool texture_spec_parse(const char *name, TexSpec *out) {
    return parse_spec(name, strlen(name), out);
}
