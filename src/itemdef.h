/* --- itemdef.h ---
 * Parses TOCLIENT_ITEMDEF's payload (once content.c has decompressed it)
 * into a table of ItemDefinitions looked up by name, matching
 * itemdef.cpp's CItemDefManager/ItemDefinition wire format (ItemDefinition
 * version 6). Same naming-matches-upstream and canRead()-tail conventions
 * as nodedef.h -- see that file's doc comment, it all applies here too.
 *
 * Two format differences from NODEDEF worth knowing before reading
 * itemdef.c:
 *  - Items are keyed by NAME (e.g. "default:stone"), not by a small dense
 *    numeric id -- item stacks reference items by string, unlike map
 *    nodes. There's no outer string32 wrapper around the whole table
 *    either, just a flat count -- see itemdef_parse()'s comment.
 *  - Unlike ContentFeatures, ItemDefinition's tail ALSO has one field
 *    gated on protocol_version rather than canRead() (the pre-5.8.0-dev
 *    single-byte place_param2, protocol_version <= 43) -- but since
 *    content.c already refuses to talk to servers below protocol 48
 *    (see content.c's CONTENT_MIN_PROTO_VER), that branch can never
 *    actually fire for us and isn't implemented. ItemImageDef's
 *    animation field (protocol_version >= 51) DOES matter for us, since
 *    48-50 is a real range we might talk to -- that one's implemented.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "nodedef.h" /* TileAnimation, SoundSpec, NodeGroup, PointabilityType */

typedef enum {
    ITEM_NONE = 0,
    ITEM_NODE,
    ITEM_CRAFT,
    ITEM_TOOL,
    ITEM_TYPE_END,
} ItemType;

typedef enum {
    TOUCH_LONG_DIG_SHORT_PLACE = 0,
    TOUCH_SHORT_DIG_LONG_PLACE = 1,
    TOUCH_USER                 = 2, /* meaning depends on client-side settings */
    TOUCH_INTERACTION_MODE_END,
} TouchInteractionMode;

/* Default-constructs to all-USER upstream (TouchInteraction's ctor) --
 * matched by touch_interaction_default() in itemdef.c, since an absent
 * tail means "never parsed", not "zeroed", and 0 is a real mode
 * (TOUCH_LONG_DIG_SHORT_PLACE), not a safe default here. */
typedef struct {
    TouchInteractionMode pointed_nothing;
    TouchInteractionMode pointed_node;
    TouchInteractionMode pointed_object;
} TouchInteraction;

typedef struct {
    char *name; /* texture string, malloc'd */
    /* Only meaningful if this connection's protocol_version >= 51 --
     * itemdef_parse() is given the protocol version for exactly this
     * reason. Zeroed (TAT_NONE) below that. */
    TileAnimation animation;
} ItemImageDef;

typedef struct {
    int16_t level;
    float   time;
} ToolGroupCapTime;

typedef struct {
    char             *group_name; /* malloc'd */
    int16_t           uses;
    int16_t           maxlevel;
    ToolGroupCapTime *times;
    uint32_t          time_count;
} ToolGroupCap;

typedef struct {
    char   *group_name; /* malloc'd */
    int16_t rating;
} ToolDamageGroup;

typedef struct {
    /* false if this item had no tool_capabilities at all (upstream:
     * tool_capabilities is a null pointer) -- every other field is
     * meaningless when this is false. */
    bool present;

    float             full_punch_interval;
    int16_t           max_drop_level;
    ToolGroupCap     *groupcaps;
    uint32_t          groupcap_count;
    ToolDamageGroup  *damage_groups;
    uint32_t          damage_group_count;
    uint16_t          punch_attack_uses; /* 0 if the sub-blob's own version < 5 */
} ToolCapabilities;

typedef struct {
    char             *name; /* malloc'd */
    PointabilityType  type;
} PointabilityEntry;

typedef struct {
    /* false if this item sent no Pointabilities override at all
     * (upstream: pointabilities is std::nullopt). */
    bool present;

    PointabilityEntry *nodes;         uint32_t node_count;
    PointabilityEntry *node_groups;   uint32_t node_group_count;
    PointabilityEntry *objects;       uint32_t object_count;
    PointabilityEntry *object_groups; uint32_t object_group_count;
} Pointabilities;

typedef enum {
    WEAR_BAR_BLEND_CONSTANT = 0,
    WEAR_BAR_BLEND_LINEAR   = 1,
    WEAR_BAR_BLEND_END,
} WearBarBlendMode;

typedef struct {
    float    key;   /* 0..1, remaining wear fraction this stop applies at */
    uint32_t color; /* packed ARGB8 */
} WearBarColorStop;

typedef struct {
    bool present; /* false if this item sent no wear bar override */
    WearBarBlendMode  blend;
    WearBarColorStop *stops;
    uint16_t          stop_count; /* upstream requires >=1 when present is true */
} WearBarParams;

typedef struct {
    /* -- fixed prefix -- */
    ItemType type;
    char    *name;        /* e.g. "default:stone"; empty means "unknown" item */
    char    *description;
    ItemImageDef inventory_image;
    ItemImageDef wield_image;
    float    wield_scale[3];
    int16_t  stack_max;
    bool     usable;
    bool     liquids_pointable;
    ToolCapabilities tool_capabilities; /* .present flag -- see above */
    NodeGroup *groups; /* reused from nodedef.h -- identical {name,value} shape */
    uint16_t   group_count;
    char      *node_placement_prediction;
    SoundSpec  sound_place;
    SoundSpec  sound_place_failed;
    float      range;
    char      *palette_image;
    uint32_t   color; /* packed ARGB8 */
    ItemImageDef inventory_overlay;
    ItemImageDef wield_overlay;

    /* -- tail, each gated on "is there more data" (see this file's doc
     * comment on the one exception). Defaults below are what an absent
     * tail implies, matching upstream's pre-deSerialize construction. --
     */
    char            *short_description; /* default "" */
    SoundSpec        sound_use;         /* default: empty name, gain/pitch 1, fade 0 */
    SoundSpec        sound_use_air;     /* same default */
    bool             has_place_param2;  /* default false */
    uint8_t          place_param2;      /* meaningless if has_place_param2 is false */
    uint8_t          wallmounted_rotate_vertical; /* default 0 */
    TouchInteraction touch_interaction; /* default: all TOUCH_USER */
    Pointabilities   pointabilities;    /* default: .present = false */
    WearBarParams    wear_bar_params;   /* default: .present = false */
} ItemDefinition;

typedef struct {
    char *name;    /* malloc'd */
    char *target;  /* malloc'd -- the real item name this alias resolves to */
} ItemAlias;

/* Growable-by-append tables (items are keyed by name, not a dense
 * numeric id the way nodes are, so there's no direct-index shortcut like
 * NodeDefTable's -- lookup is a linear scan, same as it would be for any
 * small-to-medium item list; a hash map would be the next step if this
 * ever shows up in a profile). */
typedef struct {
    ItemDefinition *items;
    size_t          item_count;
    size_t          item_capacity;

    ItemAlias *aliases;
    size_t     alias_count;
    size_t     alias_capacity;
} ItemDefTable;

void itemdef_table_init(ItemDefTable *t);
void itemdef_table_free(ItemDefTable *t);

/* Direct lookup by exact name -- does NOT resolve aliases. NULL if not
 * found. Pointer valid only until the next itemdef_parse() call (a new
 * ITEMDEF packet replaces the whole table). */
const ItemDefinition *itemdef_table_find(const ItemDefTable *t, const char *name);

/* Matches upstream's CItemDefManager::getAlias: ONE hop only (an alias
 * pointing at another alias is not followed further -- that's upstream's
 * actual behavior, not a shortcut taken here). Returns `name` itself if
 * it isn't an alias. The returned pointer is either `name` or a string
 * owned by the table (valid until the next itemdef_parse()) -- never
 * free it yourself. */
const char *itemdef_resolve_alias(const ItemDefTable *t, const char *name);

/* Convenience: itemdef_resolve_alias() then itemdef_table_find(). This is
 * the equivalent of upstream's CItemDefManager::get(), MINUS its
 * fallback to a registered "unknown" item on a miss -- that fallback
 * item is a gameplay/rendering policy decision, not a parsing one, so
 * it's left to the caller (return NULL here; caller decides what an
 * unknown item looks like). */
const ItemDefinition *itemdef_table_get(const ItemDefTable *t, const char *name);

/* Parses one full ItemDefManager blob (the bytes content.c already
 * decompressed from TOCLIENT_ITEMDEF) into t, replacing whatever was in
 * it before. protocol_version is needed for ItemImageDef's animation
 * field (only present for protocol_version >= 51) -- pass
 * net_get_proto_ver(net). Hard-fails on structural inconsistency,
 * tolerates a short tail on individual records -- see this file's doc
 * comment. */
void itemdef_parse(ItemDefTable *t, const uint8_t *data, size_t len, uint16_t protocol_version);
