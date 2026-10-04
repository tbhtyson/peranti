/* --- nodedef.h ---
 * Parses TOCLIENT_NODEDEF's payload (once content.c has decompressed it)
 * into a table of ContentFeatures indexed by content id, matching
 * Luanti's nodedef.cpp CONTENTFEATURES_VERSION 13 wire format field-for-
 * field. Every struct/enum here is named after its upstream counterpart
 * (ContentFeatures, TileDef, NodeBox, SoundSpec, ...) on purpose -- if
 * something looks wrong, grepping the real luanti source for that exact
 * name is the fastest way to check.
 *
 * Two things make this format friendlier than it looks:
 *  1. The whole record table is one string32 blob, and EACH individual
 *     ContentFeatures inside it is ALSO string16-wrapped. So a record
 *     this parser doesn't fully understand can still be skipped cleanly
 *     by length -- see nodedef_parse()'s outer loop.
 *  2. Past a certain point, ContentFeatures' own fields are read behind
 *     canRead()-style "is there more data" checks, not a version number
 *     -- see the tail fields at the bottom of ContentFeatures and
 *     nodedef_parse_content_features()'s comment on them. A short
 *     record there is a real, older-server shape, not corruption.
 *
 * Everything else -- the fixed-size prefix of a record, TileDef's own
 * `version` byte, NodeBox's `version` byte -- IS a hard invariant: if
 * those don't match what's expected, something is actually wrong (wrong
 * protocol assumed, or a genuinely different format), and this parser
 * hard-fails via net_read_require()'s exit(1), same as the rest of this
 * project.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "net_serialize.h" /* NetReader, for tileanimation_parse() below */

/* --- enums, values fixed by the wire format -- do not renumber --- */

typedef enum {
    CPT_NONE  = 0,
    CPT_LIGHT = 1,
} ContentParamType;

typedef enum {
    CPT2_NONE                    = 0,
    CPT2_FULL                    = 1,
    CPT2_FLOWINGLIQUID           = 2,
    CPT2_FACEDIR                 = 3,
    CPT2_WALLMOUNTED             = 4,
    CPT2_LEVELED                 = 5,
    CPT2_DEGROTATE               = 6,
    CPT2_MESHOPTIONS             = 7,
    CPT2_COLOR                   = 8,
    CPT2_COLORED_FACEDIR         = 9,
    CPT2_COLORED_WALLMOUNTED     = 10,
    CPT2_GLASSLIKE_LIQUID_LEVEL  = 11,
    CPT2_COLORED_DEGROTATE       = 12,
    CPT2_4DIR                    = 13,
    CPT2_COLORED_4DIR            = 14,
    CONTENT_PARAM_TYPE_2_END, /* first invalid value -- for clamping, see parse notes */
} ContentParamType2;

typedef enum {
    NDT_NORMAL = 0,
    NDT_AIRLIKE,
    NDT_LIQUID,
    NDT_FLOWINGLIQUID,
    NDT_GLASSLIKE,
    NDT_ALLFACES,
    NDT_ALLFACES_OPTIONAL,
    NDT_TORCHLIKE,
    NDT_SIGNLIKE,
    NDT_PLANTLIKE,
    NDT_FENCELIKE,
    NDT_RAILLIKE,
    NDT_NODEBOX,
    NDT_GLASSLIKE_FRAMED,
    NDT_FIRELIKE,
    NDT_GLASSLIKE_FRAMED_OPTIONAL,
    NDT_MESH,
    NDT_PLANTLIKE_ROOTED,
    NODE_DRAW_TYPE_END, /* first invalid value */
} NodeDrawType;

typedef enum {
    LIQUID_NONE = 0,
    LIQUID_FLOWING,
    LIQUID_SOURCE,
    LIQUID_TYPE_END,
} LiquidType;

typedef enum {
    ALPHAMODE_BLEND = 0,
    ALPHAMODE_CLIP,
    ALPHAMODE_OPAQUE,
    ALPHAMODE_LEGACY_COMPAT, /* only ever sent by old servers; treat as OPAQUE */
    ALPHA_MODE_END,
} AlphaMode;

typedef enum {
    NODEBOX_REGULAR = 0,
    NODEBOX_FIXED,
    NODEBOX_WALLMOUNTED,
    NODEBOX_LEVELED,
    NODEBOX_CONNECTED,
} NodeBoxType;

typedef enum {
    TAT_NONE = 0,
    TAT_VERTICAL_FRAMES = 1,
    TAT_SHEET_2D = 2,
} TileAnimationType;

typedef enum {
    ALIGN_STYLE_NODE = 0,
    ALIGN_STYLE_WORLD,
    ALIGN_STYLE_USER_DEFINED,
    ALIGN_STYLE_END,
} AlignStyle;

/* Not an enum class upstream, but the three values are wire-format-fixed
 * (see util/pointabilities.h's comments on why NOT=0/POINTABLE=1 can
 * never change -- old clients read this byte as a bool). */
typedef enum {
    POINTABLE_NOT      = 0,
    POINTABLE          = 1,
    POINTABLE_BLOCKING = 2,
} PointabilityType;

/* --- sub-structures --- */

typedef struct {
    float min[3];
    float max[3];
} NodeAabb;

/* u16-counted array of NodeAabb -- the recurring "list of boxes" shape
 * used all over NodeBox. boxes is malloc'd (NULL/count==0 for an empty
 * list), owned by whichever NodeBox holds it. */
typedef struct {
    NodeAabb *boxes;
    uint16_t  count;
} NodeBoxList;

/* Mirrors upstream's WRITEBOX/READBOXES order exactly in
 * NODEBOX_CONNECTED -- see nodedef.c's box_list_parse() call sites if
 * you need to double check against nodedef.cpp. */
typedef enum {
    NODEBOX_CONNECT_TOP = 0,
    NODEBOX_CONNECT_BOTTOM,
    NODEBOX_CONNECT_FRONT,
    NODEBOX_CONNECT_LEFT,
    NODEBOX_CONNECT_BACK,
    NODEBOX_CONNECT_RIGHT,
    NODEBOX_DISCONNECTED_TOP,
    NODEBOX_DISCONNECTED_BOTTOM,
    NODEBOX_DISCONNECTED_FRONT,
    NODEBOX_DISCONNECTED_LEFT,
    NODEBOX_DISCONNECTED_BACK,
    NODEBOX_DISCONNECTED_RIGHT,
    NODEBOX_DISCONNECTED,
    NODEBOX_DISCONNECTED_SIDES,
    NODEBOX_CONNECTED_LIST_COUNT,
} NodeBoxConnectedList;

typedef struct {
    NodeBoxType type;

    /* FIXED, LEVELED: the node's box(es). CONNECTED: the always-drawn
     * base box(es) (upstream calls this the same field, `fixed`, in
     * both cases). Unused for REGULAR/WALLMOUNTED. */
    NodeBoxList fixed;

    /* WALLMOUNTED only. */
    NodeAabb wall_top, wall_bottom, wall_side;

    /* CONNECTED only, indexed by NodeBoxConnectedList. */
    NodeBoxList connected[NODEBOX_CONNECTED_LIST_COUNT];
} NodeBox;

typedef struct {
    TileAnimationType type;
    /* Only one of these is meaningful, per `type` -- mirrors upstream's
     * union inside TileAnimationParams. */
    struct { uint16_t aspect_w, aspect_h; float length; } vertical_frames;
    struct { uint8_t  frames_w, frames_h; float frame_length; } sheet_2d;
} TileAnimation;

typedef struct {
    /* Texture string, e.g. "default_stone.png" or
     * "default_stone.png^default_mineral_coal.png". malloc'd,
     * NUL-terminated. texture_name.c (not yet written) is what actually
     * parses the ^/[ modifier grammar embedded in this string -- this
     * layer just extracts it verbatim off the wire. */
    char *name;

    TileAnimation animation;

    bool backface_culling;
    bool tileable_horizontal;
    bool tileable_vertical;

    bool    has_color;
    uint8_t color_r, color_g, color_b;

    uint8_t scale; /* 0 means "not set" (has_scale was false) */

    AlignStyle align_style;
} TileDef;

typedef struct {
    char *name; /* malloc'd, NUL-terminated; empty string means "no sound" */
    float gain;
    float pitch;
    float fade;
} SoundSpec;

typedef struct {
    char   *name;  /* group name, e.g. "cracky" -- malloc'd, NUL-terminated */
    int16_t value;
} NodeGroup;

#define CF_SPECIAL_COUNT 6 /* wire invariant -- ContentFeatures::serialize asserts this */

typedef struct {
    /* -- general -- */
    char       *name; /* e.g. "default:stone"; empty means "id not in use" */
    NodeGroup  *groups;
    uint16_t    group_count;
    ContentParamType  param_type;
    ContentParamType2 param_type_2;

    /* -- visual -- */
    NodeDrawType drawtype;
    char        *mesh; /* string16, often empty */
    float        visual_scale;
    TileDef      tiles[6];
    TileDef      tiles_overlay[6];
    TileDef      tiles_special[CF_SPECIAL_COUNT];
    AlphaMode    alpha;
    uint8_t      color_r, color_g, color_b;
    char        *palette_name;
    uint8_t      waving;
    uint8_t      connect_sides; /* bitmask */
    uint16_t    *connects_to_ids;
    uint16_t     connects_to_count;
    uint32_t     post_effect_color; /* packed ARGB8, A in bits 24-31 */
    uint8_t      leveled;

    /* -- lighting -- */
    bool    light_propagates;
    bool    sunlight_propagates;
    uint8_t light_source;

    /* -- map generation -- */
    bool is_ground_content;

    /* -- interaction -- */
    bool             walkable;
    PointabilityType pointable;
    bool             diggable;
    bool             climbable;
    bool             buildable_to;
    bool             rightclickable;
    uint32_t         damage_per_second;

    /* -- liquid -- */
    LiquidType liquid_type;
    char      *liquid_alternative_flowing;
    char      *liquid_alternative_source;
    uint8_t    liquid_viscosity;
    bool       liquid_renewable;
    uint8_t    liquid_range;
    bool       drowning;
    bool       floodable;
    /* Defaulted from liquid_type (liquid_type != LIQUID_NONE) when the
     * server doesn't send the newer explicit tail field for it -- see
     * nodedef_parse_content_features()'s comment near the tail. */
    bool       liquid_move_physics;

    /* -- node boxes -- */
    NodeBox node_box;
    NodeBox selection_box;
    NodeBox collision_box;

    /* -- sound -- */
    SoundSpec sound_footstep;
    SoundSpec sound_dig;
    SoundSpec sound_dug;

    /* -- legacy -- */
    uint8_t legacy_facedir_simple;
    uint8_t legacy_wallmounted;

    /* -- tail fields, each gated on "is there more data" rather than a
     * version number (see the doc comment at the top of this file).
     * Defaults below are what an old server that omits them implies. --
     */
    char   *node_dig_prediction;   /* default: "" (upstream's own default) */
    uint8_t leveled_max;           /* default: 0 */
    /* alpha above is ALSO set from a legacy 0-255 byte earlier in the
     * record if this tail field is absent -- see the parse function. */
    uint8_t move_resistance;       /* default: liquid_viscosity (upstream's fallback) */
    uint8_t post_effect_color_shaded; /* default: 0 */
} ContentFeatures;

/* Growable table indexed directly by content id (same approach as
 * upstream's std::vector<ContentFeatures>, sized to fit the highest id
 * seen rather than allocated at the full 65536-id range up front --
 * see nodedef.c for why). A slot with name == NULL is "not defined". */
typedef struct {
    ContentFeatures *defs;
    size_t           capacity; /* defs[] has this many valid slots */
} NodeDefTable;

void nodedef_table_init(NodeDefTable *t);
void nodedef_table_free(NodeDefTable *t);

/* Shared with itemdef.c: ItemImageDef's animation field is the exact
 * same TileAnimationParams wire format TileDef uses. Reads just the
 * animation sub-blob (type byte + its type-specific fields), nothing
 * else -- caller has already read whatever comes before it. */
void tileanimation_parse(NetReader *r, TileAnimation *anim);

/* NULL if id is out of range or was never defined. Pointer is only valid
 * until the next nodedef_parse() call (a later NODEDEF packet replaces
 * the whole table, matching upstream's NodeDefManager::deSerialize,
 * which clear()s before repopulating). */
const ContentFeatures *nodedef_table_get(const NodeDefTable *t, uint16_t id);

/* Parses one full NodeDefManager blob (the bytes content.c already
 * decompressed from TOCLIENT_NODEDEF) into t, replacing whatever was in
 * it before. Hard-fails on any structural inconsistency (bad version
 * bytes, a record's declared length not matching what's actually
 * there); tolerates a short tail on individual records, per this file's
 * doc comment. */
void nodedef_parse(NodeDefTable *t, const uint8_t *data, size_t len);
