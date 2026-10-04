#include "itemdef.h"
#include "net_serialize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- small helpers (same shape as nodedef.c's; see that file's
 * doc comment on why these aren't shared across files -- they're a few
 * lines each, not worth a shared-utility header for) ---------- */

static char *dup_str16(NetReader *r) {
    uint16_t len;
    const uint8_t *bytes = net_get_str16(r, &len);
    char *s = malloc((size_t)len + 1);
    if (!s) {
        fprintf(stderr, "itemdef: out of memory duplicating a %u-byte string\n", (unsigned)len);
        exit(1);
    }
    memcpy(s, bytes, len);
    s[len] = '\0';
    return s;
}

static void soundspec_parse(NetReader *r, SoundSpec *s) {
    s->name  = dup_str16(r);
    s->gain  = net_get_f32(r);
    s->pitch = net_get_f32(r);
    s->fade  = net_get_f32(r);
}

static void soundspec_free(SoundSpec *s) {
    free(s->name);
    s->name = NULL;
}

static SoundSpec soundspec_default(void) {
    /* Matches an absent tail's implied value -- same as a freshly
     * constructed upstream SimpleSoundSpec (empty name, unity gain/pitch,
     * no fade). */
    SoundSpec s;
    s.name = malloc(1);
    if (!s.name) { fprintf(stderr, "itemdef: out of memory\n"); exit(1); }
    s.name[0] = '\0';
    s.gain = 1.0f;
    s.pitch = 1.0f;
    s.fade = 0.0f;
    return s;
}

static TouchInteraction touch_interaction_default(void) {
    /* Matches TouchInteraction's default constructor (itemdef.cpp) --
     * all three default to USER, NOT to 0/LONG_DIG_SHORT_PLACE, since 0
     * is a real, different mode. */
    TouchInteraction t;
    t.pointed_nothing = TOUCH_USER;
    t.pointed_node = TOUCH_USER;
    t.pointed_object = TOUCH_USER;
    return t;
}

/* ---------- ItemImageDef (itemdef.cpp's ItemImageDef::deSerialize) ---------- */

static void itemimage_parse(NetReader *r, ItemImageDef *img, uint16_t protocol_version) {
    img->name = dup_str16(r);
    if (protocol_version < 51) {
        /* Animation was added in protocol 51 -- older servers never send
         * it at all (not even a "none" marker), so there's nothing more
         * to read here, not an optional field to default. */
        img->animation.type = TAT_NONE;
        memset(&img->animation.vertical_frames, 0, sizeof(img->animation.vertical_frames));
        memset(&img->animation.sheet_2d, 0, sizeof(img->animation.sheet_2d));
        return;
    }
    tileanimation_parse(r, &img->animation);
}

static void itemimage_free(ItemImageDef *img) {
    free(img->name);
    img->name = NULL;
}

/* ---------- ToolCapabilities (tool.cpp's ToolCapabilities::deSerialize)
 * -- wrapped in its own string16 inside ItemDefinition; an empty string
 * means "this item has no tool_capabilities at all" (.present = false),
 * NOT "a zero-valued ToolCapabilities" -- matching upstream's null
 * pointer vs. a real (possibly all-zero) object. ---------- */

static void toolcapabilities_free(ToolCapabilities *tc) {
    for (uint32_t i = 0; i < tc->groupcap_count; ++i) {
        free(tc->groupcaps[i].group_name);
        free(tc->groupcaps[i].times);
    }
    free(tc->groupcaps);
    for (uint32_t i = 0; i < tc->damage_group_count; ++i)
        free(tc->damage_groups[i].group_name);
    free(tc->damage_groups);
    memset(tc, 0, sizeof(*tc));
}

static void toolcapabilities_parse(NetReader *r, ToolCapabilities *tc) {
    memset(tc, 0, sizeof(*tc));
    tc->present = true;

    uint8_t version = net_get_u8(r);
    if (version < 4) {
        fprintf(stderr, "itemdef: unsupported ToolCapabilities version %u (need >=4)\n",
                (unsigned)version);
        exit(1);
    }

    tc->full_punch_interval = net_get_f32(r);
    tc->max_drop_level = net_get_s16(r);

    tc->groupcap_count = net_get_u32(r);
    if (tc->groupcap_count > 0) {
        tc->groupcaps = malloc(sizeof(ToolGroupCap) * tc->groupcap_count);
        if (!tc->groupcaps) {
            fprintf(stderr, "itemdef: out of memory allocating %u groupcaps\n",
                    (unsigned)tc->groupcap_count);
            exit(1);
        }
        for (uint32_t i = 0; i < tc->groupcap_count; ++i) {
            ToolGroupCap *gc = &tc->groupcaps[i];
            gc->group_name = dup_str16(r);
            gc->uses = net_get_s16(r);
            gc->maxlevel = net_get_s16(r);
            gc->time_count = net_get_u32(r);
            gc->times = NULL;
            if (gc->time_count > 0) {
                gc->times = malloc(sizeof(ToolGroupCapTime) * gc->time_count);
                if (!gc->times) {
                    fprintf(stderr, "itemdef: out of memory allocating %u group times\n",
                            (unsigned)gc->time_count);
                    exit(1);
                }
                for (uint32_t j = 0; j < gc->time_count; ++j) {
                    gc->times[j].level = net_get_s16(r);
                    gc->times[j].time = net_get_f32(r);
                }
            }
        }
    }

    tc->damage_group_count = net_get_u32(r);
    if (tc->damage_group_count > 0) {
        tc->damage_groups = malloc(sizeof(ToolDamageGroup) * tc->damage_group_count);
        if (!tc->damage_groups) {
            fprintf(stderr, "itemdef: out of memory allocating %u damage groups\n",
                    (unsigned)tc->damage_group_count);
            exit(1);
        }
        for (uint32_t i = 0; i < tc->damage_group_count; ++i) {
            tc->damage_groups[i].group_name = dup_str16(r);
            tc->damage_groups[i].rating = net_get_s16(r);
        }
    }

    tc->punch_attack_uses = (version >= 5) ? net_get_u16(r) : 0;
}

/* ---------- Pointabilities (util/pointabilities.cpp's
 * Pointabilities::deSerialize) -- same present/absent-via-empty-string16
 * convention as ToolCapabilities above. ---------- */

static void pointability_type_map_parse(NetReader *r, PointabilityEntry **out_entries,
                                         uint32_t *out_count) {
    uint32_t count = net_get_u32(r);
    *out_count = count;
    if (count == 0) {
        *out_entries = NULL;
        return;
    }
    PointabilityEntry *entries = malloc(sizeof(PointabilityEntry) * count);
    if (!entries) {
        fprintf(stderr, "itemdef: out of memory allocating %u pointability entries\n",
                (unsigned)count);
        exit(1);
    }
    for (uint32_t i = 0; i < count; ++i) {
        entries[i].name = dup_str16(r);
        uint8_t v = net_get_u8(r);
        if (v != POINTABLE_NOT && v != POINTABLE && v != POINTABLE_BLOCKING)
            v = POINTABLE; /* same fallback as nodedef.c's pointable field */
        entries[i].type = (PointabilityType)v;
    }
    *out_entries = entries;
}

static void pointability_type_map_free(PointabilityEntry *entries, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i)
        free(entries[i].name);
    free(entries);
}

static void pointabilities_free(Pointabilities *p) {
    pointability_type_map_free(p->nodes, p->node_count);
    pointability_type_map_free(p->node_groups, p->node_group_count);
    pointability_type_map_free(p->objects, p->object_count);
    pointability_type_map_free(p->object_groups, p->object_group_count);
    memset(p, 0, sizeof(*p));
}

static void pointabilities_parse(NetReader *r, Pointabilities *p) {
    memset(p, 0, sizeof(*p));
    p->present = true;

    uint8_t version = net_get_u8(r);
    if (version != 0) {
        fprintf(stderr, "itemdef: unsupported Pointabilities version %u (need 0)\n",
                (unsigned)version);
        exit(1);
    }

    pointability_type_map_parse(r, &p->nodes, &p->node_count);
    pointability_type_map_parse(r, &p->node_groups, &p->node_group_count);
    pointability_type_map_parse(r, &p->objects, &p->object_count);
    pointability_type_map_parse(r, &p->object_groups, &p->object_group_count);
}

/* ---------- WearBarParams (tool.cpp's WearBarParams::deserialize) --
 * NOT string16-wrapped like the two above -- it's read inline, gated by
 * its own "have wear bar params" flag byte (see content_features_parse's
 * caller, itemdef_parse_record() below). ---------- */

static void wearbarparams_free(WearBarParams *w) {
    free(w->stops);
    memset(w, 0, sizeof(*w));
}

static void wearbarparams_parse(NetReader *r, WearBarParams *w) {
    memset(w, 0, sizeof(*w));
    w->present = true;

    uint8_t version = net_get_u8(r);
    if (version > 1) {
        fprintf(stderr, "itemdef: unsupported WearBarParams version %u (need <=1)\n",
                (unsigned)version);
        exit(1);
    }

    uint8_t blend = net_get_u8(r);
    if (blend >= WEAR_BAR_BLEND_END) {
        /* Upstream hard-fails here too (throw SerializationError) --
         * unlike most other out-of-range enum bytes in this format,
         * there's no sensible "fall back to a default" for a blend mode
         * a renderer doesn't recognize. */
        fprintf(stderr, "itemdef: invalid WearBarParams blend mode %u\n", (unsigned)blend);
        exit(1);
    }
    w->blend = (WearBarBlendMode)blend;

    w->stop_count = net_get_u16(r);
    if (w->stop_count == 0) {
        fprintf(stderr, "itemdef: WearBarParams with zero color stops\n");
        exit(1);
    }
    w->stops = malloc(sizeof(WearBarColorStop) * w->stop_count);
    if (!w->stops) {
        fprintf(stderr, "itemdef: out of memory allocating %u wear bar stops\n",
                (unsigned)w->stop_count);
        exit(1);
    }
    for (uint16_t i = 0; i < w->stop_count; ++i) {
        float key = net_get_f32(r);
        if (key < 0.0f || key > 1.0f) {
            fprintf(stderr, "itemdef: WearBarParams stop key %f out of [0,1]\n", (double)key);
            exit(1);
        }
        w->stops[i].key = key;
        w->stops[i].color = net_get_u32(r);
    }
}

/* ---------- ItemDefinition (itemdef.cpp's ItemDefinition::deSerialize) ---------- */

#define ITEMDEFINITION_VERSION 6u

static void item_definition_free(ItemDefinition *f) {
    free(f->name);
    free(f->description);
    itemimage_free(&f->inventory_image);
    itemimage_free(&f->wield_image);
    if (f->tool_capabilities.present)
        toolcapabilities_free(&f->tool_capabilities);
    for (uint16_t i = 0; i < f->group_count; ++i)
        free(f->groups[i].name);
    free(f->groups);
    free(f->node_placement_prediction);
    soundspec_free(&f->sound_place);
    soundspec_free(&f->sound_place_failed);
    free(f->palette_image);
    itemimage_free(&f->inventory_overlay);
    itemimage_free(&f->wield_overlay);
    free(f->short_description);
    soundspec_free(&f->sound_use);
    soundspec_free(&f->sound_use_air);
    if (f->pointabilities.present)
        pointabilities_free(&f->pointabilities);
    if (f->wear_bar_params.present)
        wearbarparams_free(&f->wear_bar_params);
    memset(f, 0, sizeof(*f));
}

/* `f` must be zeroed on entry. */
static void item_definition_parse(NetReader *r, ItemDefinition *f, uint16_t protocol_version) {
    uint8_t version = net_get_u8(r);
    if (version < ITEMDEFINITION_VERSION) {
        fprintf(stderr, "itemdef: unsupported ItemDefinition version %u (need >=%u)\n",
                (unsigned)version, ITEMDEFINITION_VERSION);
        exit(1);
    }

    f->type = (ItemType)net_get_u8(r);
    if (f->type >= ITEM_TYPE_END)
        f->type = ITEM_NONE;

    f->name = dup_str16(r);
    f->description = dup_str16(r);
    itemimage_parse(r, &f->inventory_image, protocol_version);
    itemimage_parse(r, &f->wield_image, protocol_version);
    f->wield_scale[0] = net_get_f32(r);
    f->wield_scale[1] = net_get_f32(r);
    f->wield_scale[2] = net_get_f32(r);
    f->stack_max = net_get_s16(r);
    f->usable = net_get_u8(r) != 0;
    f->liquids_pointable = net_get_u8(r) != 0;

    {
        uint16_t tc_len;
        const uint8_t *tc_bytes = net_get_str16(r, &tc_len);
        if (tc_len > 0) {
            NetReader tc_r;
            net_reader_init(&tc_r, tc_bytes, tc_len);
            toolcapabilities_parse(&tc_r, &f->tool_capabilities);
        } else {
            memset(&f->tool_capabilities, 0, sizeof(f->tool_capabilities)); /* .present = false */
        }
    }

    f->group_count = net_get_u16(r);
    if (f->group_count > 0) {
        f->groups = malloc(sizeof(NodeGroup) * f->group_count);
        if (!f->groups) {
            fprintf(stderr, "itemdef: out of memory allocating %u groups\n", (unsigned)f->group_count);
            exit(1);
        }
        for (uint16_t i = 0; i < f->group_count; ++i) {
            f->groups[i].name = dup_str16(r);
            f->groups[i].value = net_get_s16(r);
        }
    }

    f->node_placement_prediction = dup_str16(r);

    soundspec_parse(r, &f->sound_place);
    soundspec_parse(r, &f->sound_place_failed);

    f->range = net_get_f32(r);
    f->palette_image = dup_str16(r);
    f->color = net_get_u32(r); /* ARGB8, read like any other u32 */
    itemimage_parse(r, &f->inventory_overlay, protocol_version);
    itemimage_parse(r, &f->wield_overlay, protocol_version);

    /* -- tail: every field below is gated on "is there more data", not a
     * version number (see this file's doc comment on the one exception,
     * the pre-protocol-44 place_param2 byte, which is skipped entirely
     * since content.c refuses servers below protocol 48). A server that
     * stops partway through here is older, not corrupt. -- */
    f->short_description = malloc(1);
    if (f->short_description) f->short_description[0] = '\0';
    f->sound_use = soundspec_default();
    f->sound_use_air = soundspec_default();
    f->touch_interaction = touch_interaction_default();

    if (net_reader_remaining(r) == 0) return; /* pre-5.4.0-dev */
    free(f->short_description);
    f->short_description = dup_str16(r);

    if (net_reader_remaining(r) == 0) return; /* pre-5.5.0-dev */
    /* protocol_version <= 43 legacy place_param2 byte would be read here
     * -- intentionally not implemented, see top-of-file doc comment. */

    if (net_reader_remaining(r) == 0) return; /* pre-5.7.0-dev */
    soundspec_free(&f->sound_use);
    soundspec_parse(r, &f->sound_use);
    soundspec_free(&f->sound_use_air);
    soundspec_parse(r, &f->sound_use_air);

    if (net_reader_remaining(r) == 0) return; /* pre-5.8.0-dev */
    f->has_place_param2 = net_get_u8(r) != 0;
    if (f->has_place_param2)
        f->place_param2 = net_get_u8(r);

    if (net_reader_remaining(r) == 0) return; /* pre-5.9.0-dev */
    f->wallmounted_rotate_vertical = net_get_u8(r);
    {
        /* TouchInteraction::deSerialize: each byte independently either
         * sets that field or, if out of range, leaves it at whatever it
         * already was (the USER default set above) -- not a hard fail,
         * matching upstream exactly (see nodedef.h-style enums above). */
        uint8_t v;
        v = net_get_u8(r);
        if (v < TOUCH_INTERACTION_MODE_END) f->touch_interaction.pointed_nothing = (TouchInteractionMode)v;
        v = net_get_u8(r);
        if (v < TOUCH_INTERACTION_MODE_END) f->touch_interaction.pointed_node = (TouchInteractionMode)v;
        v = net_get_u8(r);
        if (v < TOUCH_INTERACTION_MODE_END) f->touch_interaction.pointed_object = (TouchInteractionMode)v;
    }

    {
        uint16_t pa_len;
        const uint8_t *pa_bytes = net_get_str16(r, &pa_len);
        if (pa_len > 0) {
            NetReader pa_r;
            net_reader_init(&pa_r, pa_bytes, pa_len);
            pointabilities_parse(&pa_r, &f->pointabilities);
        }
        /* else: leave as zeroed-at-entry, .present stays false */
    }

    if (net_get_u8(r)) /* "have wear bar params" */
        wearbarparams_parse(r, &f->wear_bar_params);

    /* Any bytes past here belong to a field newer than what this parser
     * knows about -- expected, not an error, per the canRead() convention
     * documented at the top of nodedef.h. */
}

/* ---------- table management ---------- */

void itemdef_table_init(ItemDefTable *t) {
    memset(t, 0, sizeof(*t));
}

void itemdef_table_free(ItemDefTable *t) {
    for (size_t i = 0; i < t->item_count; ++i)
        item_definition_free(&t->items[i]);
    free(t->items);
    for (size_t i = 0; i < t->alias_count; ++i) {
        free(t->aliases[i].name);
        free(t->aliases[i].target);
    }
    free(t->aliases);
    memset(t, 0, sizeof(*t));
}

const ItemDefinition *itemdef_table_find(const ItemDefTable *t, const char *name) {
    for (size_t i = 0; i < t->item_count; ++i) {
        if (strcmp(t->items[i].name, name) == 0)
            return &t->items[i];
    }
    return NULL;
}

const char *itemdef_resolve_alias(const ItemDefTable *t, const char *name) {
    for (size_t i = 0; i < t->alias_count; ++i) {
        if (strcmp(t->aliases[i].name, name) == 0)
            return t->aliases[i].target; /* single hop only -- matches upstream */
    }
    return name;
}

const ItemDefinition *itemdef_table_get(const ItemDefTable *t, const char *name) {
    return itemdef_table_find(t, itemdef_resolve_alias(t, name));
}

static void table_append_item(ItemDefTable *t, ItemDefinition item) {
    if (t->item_count == t->item_capacity) {
        size_t new_cap = t->item_capacity == 0 ? 64 : t->item_capacity * 2;
        ItemDefinition *new_items = realloc(t->items, sizeof(ItemDefinition) * new_cap);
        if (!new_items) {
            fprintf(stderr, "itemdef: out of memory growing item table to %zu entries\n", new_cap);
            exit(1);
        }
        t->items = new_items;
        t->item_capacity = new_cap;
    }
    t->items[t->item_count++] = item;
}

static void table_append_alias(ItemDefTable *t, char *name, char *target) {
    if (t->alias_count == t->alias_capacity) {
        size_t new_cap = t->alias_capacity == 0 ? 64 : t->alias_capacity * 2;
        ItemAlias *new_aliases = realloc(t->aliases, sizeof(ItemAlias) * new_cap);
        if (!new_aliases) {
            fprintf(stderr, "itemdef: out of memory growing alias table to %zu entries\n", new_cap);
            exit(1);
        }
        t->aliases = new_aliases;
        t->alias_capacity = new_cap;
    }
    t->aliases[t->alias_count].name = name;
    t->aliases[t->alias_count].target = target;
    t->alias_count++;
}

void itemdef_parse(ItemDefTable *t, const uint8_t *data, size_t len, uint16_t protocol_version) {
    /* Matches upstream's CItemDefManager::deSerialize: replace the whole
     * table on every ITEMDEF packet. */
    itemdef_table_free(t);
    itemdef_table_init(t);

    NetReader r;
    net_reader_init(&r, data, len);

    uint8_t version = net_get_u8(&r);
    if (version != 0) {
        fprintf(stderr, "itemdef: unsupported ItemDefManager version %u (need 0)\n",
                (unsigned)version);
        exit(1);
    }

    /* Unlike NodeDefManager, there's no outer string32 wrapper around
     * the whole table and no per-record numeric id -- just a flat count,
     * each record individually string16-wrapped, each keyed by its own
     * `name` field once parsed. See itemdef.h's doc comment. */
    uint16_t count = net_get_u16(&r);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t record_len;
        const uint8_t *record_bytes = net_get_str16(&r, &record_len);
        NetReader record_r;
        net_reader_init(&record_r, record_bytes, record_len);

        ItemDefinition item;
        memset(&item, 0, sizeof(item));
        item_definition_parse(&record_r, &item, protocol_version);

        if (item.name[0] == '\0') {
            fprintf(stderr, "itemdef: NOTE: received a record with an empty name, ignoring\n");
            item_definition_free(&item);
            continue;
        }
        table_append_item(t, item);
    }

    uint16_t alias_count = net_get_u16(&r);
    for (uint16_t i = 0; i < alias_count; ++i) {
        char *name = dup_str16(&r);
        char *target = dup_str16(&r);
        table_append_alias(t, name, target);
    }
}
