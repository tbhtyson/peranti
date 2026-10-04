/* --- texture_name.h ---
 * Parses a Luanti texture string (what you find in TileDef.name,
 * ItemImageDef.name, etc. -- e.g. "default_stone.png^default_mineral_
 * coal.png^[opacity:127]") into an ordered list of layers to apply left
 * to right. Mirrors imagesource.cpp's ImageSource::generateImage()'s
 * actual algorithm -- traced through that function directly, not
 * guessed from examples -- but stops at the UNIVERSAL grammar (layer
 * splitting on '^', '(...)' grouping, and "'[' means this layer is a
 * named modifier with a raw argument tail"). It deliberately does NOT
 * parse what's inside a modifier's argument string.
 *
 * That stopping point is a real property of the upstream format, not a
 * scope cut of convenience: upstream has no universal modifier-name/
 * argument grammar at all. generateImagePart() is a long if/else chain
 * of literal prefix checks (str_starts_with(part, "[opacity:"),
 * str_starts_with(part, "[combine"), ...), and each modifier parses its
 * own argument tail however it wants -- [combine's arguments are
 * themselves colon-and-equals-separated coordinates each carrying a
 * full nested texture string (which can have its own colons from ITS
 * own modifiers), while [opacity's argument is just one integer. There
 * is no closing ']' requirement anywhere either -- "[opacity:127]"'s
 * trailing ']' is never stripped or validated; stoi("127]") just stops
 * at the first non-digit and returns 127. Replicating that per-modifier
 * variety here would mean re-implementing every modifier's semantics
 * just to parse its syntax, so each modifier's raw argument tail is
 * handed back as an opaque string for whatever implements that specific
 * modifier (texture_compose.c, incrementally) to interpret.
 *
 * Failure handling deliberately differs from this project's network
 * parsers: a malformed texture STRING (unbalanced parentheses) is a
 * mod/server authoring mistake, not a protocol violation -- upstream's
 * own generateImage() logs and returns NULL rather than crashing the
 * client over it. texture_spec_parse() does the same: returns false,
 * never exit(1)s. The caller (texture_compose.c, once it exists) is
 * what decides what a failed/unrecognized texture looks like on screen
 * (a magenta fallback tile, presumably) -- that's a rendering policy
 * decision, not a parsing one.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    TEX_LAYER_IMAGE,    /* a plain filename, e.g. "default_stone.png" */
    TEX_LAYER_GROUP,    /* a "(...)" sub-expression, composited on its own
                          * then blitted onto the running result as one unit --
                          * see this file's doc comment on the one real quirk
                          * this inherits from upstream: a segment is treated
                          * as a group purely by its FIRST and LAST byte being
                          * '(' and ')', so something like "(a)(b)" (two
                          * groups with no separator between them) is
                          * misparsed as one group spanning "a)(b" instead of
                          * two -- upstream has this exact same behavior, not
                          * fixed here because real content never produces
                          * that shape and faithfully matching upstream matters
                          * more than being "more correct" than it. */
    TEX_LAYER_MODIFIER, /* a "[name" or "[name:args" segment */
} TexLayerKind;

typedef struct TexSpec TexSpec;

typedef struct {
    TexLayerKind kind;
    union {
        char *image_name; /* TEX_LAYER_IMAGE: malloc'd, e.g. "default_stone.png" */
        TexSpec *group;   /* TEX_LAYER_GROUP: malloc'd nested spec, owns its own layers */
        struct {
            char *name; /* malloc'd, e.g. "opacity", "combine", "cracko" (the
                         * alphabetic run right after '[' -- see this file's
                         * doc comment on why there's no fixed modifier
                         * vocabulary enforced here) */
            char *args; /* malloc'd, everything after the first ':' verbatim,
                         * or an empty string "" if there was no ':' at all.
                         * NOT further parsed -- see this file's doc comment.
                         * May still contain a stray trailing ']' character;
                         * that is upstream's real behavior, not a bug here. */
        } modifier;
    } as;
} TexLayer;

struct TexSpec {
    TexLayer *layers;
    size_t    layer_count; /* 0 for an empty texture string, or one that's
                             * nothing but empty '^'-separated segments --
                             * both are valid, harmless "no texture" results,
                             * matching upstream's "keep baseimg as NULL" for
                             * an empty part_of_name. */
};

/* Parses `name` (a NUL-terminated texture string) into *out. On success
 * (true), *out is populated and the caller owns it (texture_spec_free()
 * when done). On failure (false) -- unbalanced parentheses only, the one
 * structural error this grammar has -- *out is left zeroed and nothing
 * needs freeing. Never exit(1)s; see this file's doc comment on why a
 * malformed texture string is handled differently from a malformed
 * network packet elsewhere in this project. */
bool texture_spec_parse(const char *name, TexSpec *out);

void texture_spec_free(TexSpec *spec);
