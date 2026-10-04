#include "nodedef.h"
#include "net_serialize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CONTENT_AIR/CONTENT_IGNORE already live in mesh.h (the mesher needs
 * them too); CONTENT_UNKNOWN only matters here, at parse time, so it's
 * defined locally rather than dragging mesh.h into a file that otherwise
 * has nothing to do with meshing. If mesh.h's pair ever move, bring this
 * one with them so all three stay next to each other. */
#define CONTENT_UNKNOWN 125u
#define CONTENT_AIR     126u
#define CONTENT_IGNORE  127u

#define LIGHT_MAX 14u

/* ---------- small helpers ---------- */

static char *dup_str16(NetReader *r) {
    uint16_t len;
    const uint8_t *bytes = net_get_str16(r, &len);
    char *s = malloc((size_t)len + 1);
    if (!s) {
        fprintf(stderr, "nodedef: out of memory duplicating a %u-byte string\n", (unsigned)len);
        exit(1);
    }
    memcpy(s, bytes, len);
    s[len] = '\0';
    return s;
}

static NodeAabb read_aabb(NetReader *r) {
    NodeAabb box;
    box.min[0] = net_get_f32(r);
    box.min[1] = net_get_f32(r);
    box.min[2] = net_get_f32(r);
    box.max[0] = net_get_f32(r);
    box.max[1] = net_get_f32(r);
    box.max[2] = net_get_f32(r);
    return box;
}

/* ---------- NodeBox (node_box.cpp's NodeBox::deSerialize) ---------- */

static NodeBoxList box_list_parse(NetReader *r) {
    NodeBoxList list;
    list.count = net_get_u16(r);
    if (list.count == 0) {
        list.boxes = NULL;
        return list;
    }
    list.boxes = malloc(sizeof(NodeAabb) * (size_t)list.count);
    if (!list.boxes) {
        fprintf(stderr, "nodedef: out of memory allocating %u boxes\n", (unsigned)list.count);
        exit(1);
    }
    for (uint16_t i = 0; i < list.count; ++i)
        list.boxes[i] = read_aabb(r);
    return list;
}

static void box_list_free(NodeBoxList *list) {
    free(list->boxes);
    list->boxes = NULL;
    list->count = 0;
}

static void nodebox_parse(NetReader *r, NodeBox *box) {
    memset(box, 0, sizeof(*box));

    uint8_t version = net_get_u8(r);
    if (version < 6) {
        fprintf(stderr, "nodedef: unsupported NodeBox version %u (need >=6)\n", (unsigned)version);
        exit(1);
    }

    box->type = (NodeBoxType)net_get_u8(r);
    switch (box->type) {
    case NODEBOX_REGULAR:
        break;

    case NODEBOX_FIXED:
    case NODEBOX_LEVELED:
        box->fixed = box_list_parse(r);
        break;

    case NODEBOX_WALLMOUNTED:
        box->wall_top    = read_aabb(r);
        box->wall_bottom = read_aabb(r);
        box->wall_side   = read_aabb(r);
        break;

    case NODEBOX_CONNECTED:
        /* Order matches upstream's WRITEBOX/READBOXES macro invocations
         * exactly: fixed, then the 14 lists in NodeBoxConnectedList's
         * declared order. */
        box->fixed = box_list_parse(r);
        for (int i = 0; i < NODEBOX_CONNECTED_LIST_COUNT; ++i)
            box->connected[i] = box_list_parse(r);
        break;

    default:
        /* Not a canRead()-style forward-compat case -- NodeBoxType is a
         * small, old, stable enum. A value outside it means something is
         * actually wrong (protocol desync from a preceding field), not
         * a newer server using a feature we don't know about yet. */
        fprintf(stderr, "nodedef: unknown NodeBoxType %u\n", (unsigned)box->type);
        exit(1);
    }
}

static void nodebox_free(NodeBox *box) {
    box_list_free(&box->fixed);
    for (int i = 0; i < NODEBOX_CONNECTED_LIST_COUNT; ++i)
        box_list_free(&box->connected[i]);
}

/* ---------- TileAnimation (nodedef.cpp/tileanimation.cpp's
 * TileAnimationParams::deSerialize -- shared with itemdef.c's
 * ItemImageDef, hence non-static and declared in nodedef.h) ---------- */

void tileanimation_parse(NetReader *r, TileAnimation *anim) {
    anim->type = (TileAnimationType)net_get_u8(r);
    switch (anim->type) {
    case TAT_NONE:
        break;
    case TAT_VERTICAL_FRAMES:
        anim->vertical_frames.aspect_w = net_get_u16(r);
        anim->vertical_frames.aspect_h = net_get_u16(r);
        anim->vertical_frames.length   = net_get_f32(r);
        break;
    case TAT_SHEET_2D:
        anim->sheet_2d.frames_w     = net_get_u8(r);
        anim->sheet_2d.frames_h     = net_get_u8(r);
        anim->sheet_2d.frame_length = net_get_f32(r);
        break;
    default:
        /* Upstream tolerates this (falls back to TAT_NONE) rather than
         * hard-failing -- unlike NodeBoxType, TileAnimationType really
         * has grown before and might again. */
        fprintf(stderr, "nodedef: NOTE: unknown TileAnimationType %u, treating as none\n",
                (unsigned)anim->type);
        anim->type = TAT_NONE;
        break;
    }
}

/* ---------- TileDef (nodedef.cpp's TileDef::deSerialize) ---------- */

static void tiledef_parse(NetReader *r, TileDef *td) {
    memset(td, 0, sizeof(*td));

    uint8_t version = net_get_u8(r);
    if (version < 6) {
        fprintf(stderr, "nodedef: unsupported TileDef version %u (need >=6)\n", (unsigned)version);
        exit(1);
    }

    td->name = dup_str16(r);

    tileanimation_parse(r, &td->animation);

    uint16_t flags = net_get_u16(r);
    td->backface_culling     = (flags & (1u << 0)) != 0;
    td->tileable_horizontal  = (flags & (1u << 1)) != 0;
    td->tileable_vertical    = (flags & (1u << 2)) != 0;
    td->has_color            = (flags & (1u << 3)) != 0;
    bool has_scale           = (flags & (1u << 4)) != 0;
    bool has_align_style     = (flags & (1u << 5)) != 0;

    if (td->has_color) {
        td->color_r = net_get_u8(r);
        td->color_g = net_get_u8(r);
        td->color_b = net_get_u8(r);
    }

    td->scale = has_scale ? net_get_u8(r) : 0;

    if (has_align_style) {
        td->align_style = (AlignStyle)net_get_u8(r);
        if (td->align_style >= ALIGN_STYLE_END)
            td->align_style = ALIGN_STYLE_NODE;
    } else {
        td->align_style = ALIGN_STYLE_NODE;
    }
}

static void tiledef_free(TileDef *td) {
    free(td->name);
    td->name = NULL;
}

/* ---------- SoundSpec (sound_spec.cpp's SoundSpec::deSerializeSimple) ---------- */

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

/* ---------- ContentFeatures (nodedef.cpp's ContentFeatures::deSerialize) ---------- */

#define CONTENTFEATURES_VERSION 13u

static void content_features_free(ContentFeatures *f) {
    free(f->name);
    for (uint16_t i = 0; i < f->group_count; ++i)
        free(f->groups[i].name);
    free(f->groups);
    free(f->mesh);
    for (int i = 0; i < 6; ++i) tiledef_free(&f->tiles[i]);
    for (int i = 0; i < 6; ++i) tiledef_free(&f->tiles_overlay[i]);
    for (int i = 0; i < CF_SPECIAL_COUNT; ++i) tiledef_free(&f->tiles_special[i]);
    free(f->palette_name);
    free(f->connects_to_ids);
    free(f->liquid_alternative_flowing);
    free(f->liquid_alternative_source);
    nodebox_free(&f->node_box);
    nodebox_free(&f->selection_box);
    nodebox_free(&f->collision_box);
    soundspec_free(&f->sound_footstep);
    soundspec_free(&f->sound_dig);
    soundspec_free(&f->sound_dug);
    free(f->node_dig_prediction);
    memset(f, 0, sizeof(*f));
}

/* Parses one ContentFeatures record. `f` is assumed zeroed on entry
 * (nodedef_parse() reuses one scratch record per iteration -- matches
 * upstream's NodeDefManager::deSerialize, which reuses one `new_f` too --
 * so callers must content_features_free() it between records, not just
 * between calls). */
static void content_features_parse(NetReader *r, ContentFeatures *f) {
    uint8_t version = net_get_u8(r);
    if (version < CONTENTFEATURES_VERSION) {
        fprintf(stderr, "nodedef: unsupported ContentFeatures version %u (need >=%u)\n",
                (unsigned)version, CONTENTFEATURES_VERSION);
        exit(1);
    }

    /* -- general -- */
    f->name = dup_str16(r);

    f->group_count = net_get_u16(r);
    if (f->group_count > 0) {
        f->groups = malloc(sizeof(NodeGroup) * f->group_count);
        if (!f->groups) {
            fprintf(stderr, "nodedef: out of memory allocating %u groups\n", (unsigned)f->group_count);
            exit(1);
        }
        for (uint16_t i = 0; i < f->group_count; ++i) {
            f->groups[i].name  = dup_str16(r);
            f->groups[i].value = net_get_s16(r);
        }
    }

    f->param_type = (ContentParamType)net_get_u8(r);
    if (f->param_type >= CPT_LIGHT + 1) /* only CPT_NONE/CPT_LIGHT are valid */
        f->param_type = CPT_NONE;

    f->param_type_2 = (ContentParamType2)net_get_u8(r);
    if (f->param_type_2 >= CONTENT_PARAM_TYPE_2_END)
        f->param_type_2 = CPT2_NONE;

    /* -- visual -- */
    f->drawtype = (NodeDrawType)net_get_u8(r);
    if (f->drawtype >= NODE_DRAW_TYPE_END)
        f->drawtype = NDT_NORMAL;

    f->mesh = dup_str16(r);
    f->visual_scale = net_get_f32(r);

    uint8_t tile_count = net_get_u8(r);
    if (tile_count != 6) {
        fprintf(stderr, "nodedef: unsupported tile count %u (need 6)\n", (unsigned)tile_count);
        exit(1);
    }
    for (int i = 0; i < 6; ++i) tiledef_parse(r, &f->tiles[i]);
    for (int i = 0; i < 6; ++i) tiledef_parse(r, &f->tiles_overlay[i]);

    uint8_t special_count = net_get_u8(r);
    if (special_count != CF_SPECIAL_COUNT) {
        fprintf(stderr, "nodedef: unsupported special tile count %u (need %u)\n",
                (unsigned)special_count, (unsigned)CF_SPECIAL_COUNT);
        exit(1);
    }
    for (int i = 0; i < CF_SPECIAL_COUNT; ++i) tiledef_parse(r, &f->tiles_special[i]);

    /* Legacy 0-255 alpha byte -> initial AlphaMode guess (setAlphaFromLegacy).
     * Every real server we'll talk to also sends the explicit `alpha` tail
     * field below, which overwrites this -- but the tail is genuinely
     * optional, so this has to be right on its own too. */
    {
        uint8_t legacy_alpha = net_get_u8(r);
        switch (f->drawtype) {
        case NDT_NORMAL:
            f->alpha = (legacy_alpha == 255) ? ALPHAMODE_OPAQUE : ALPHAMODE_CLIP;
            break;
        case NDT_LIQUID:
        case NDT_FLOWINGLIQUID:
            f->alpha = (legacy_alpha == 255) ? ALPHAMODE_OPAQUE : ALPHAMODE_BLEND;
            break;
        default:
            f->alpha = (legacy_alpha == 255) ? ALPHAMODE_CLIP : ALPHAMODE_BLEND;
            break;
        }
    }

    f->color_r = net_get_u8(r);
    f->color_g = net_get_u8(r);
    f->color_b = net_get_u8(r);
    f->palette_name = dup_str16(r);
    f->waving = net_get_u8(r);
    f->connect_sides = net_get_u8(r);

    f->connects_to_count = net_get_u16(r);
    if (f->connects_to_count > 0) {
        f->connects_to_ids = malloc(sizeof(uint16_t) * f->connects_to_count);
        if (!f->connects_to_ids) {
            fprintf(stderr, "nodedef: out of memory allocating %u connects_to ids\n",
                    (unsigned)f->connects_to_count);
            exit(1);
        }
        for (uint16_t i = 0; i < f->connects_to_count; ++i)
            f->connects_to_ids[i] = net_get_u16(r);
    }

    f->post_effect_color = net_get_u32(r); /* packed ARGB8, read like any other u32 */
    f->leveled = net_get_u8(r);

    /* -- lighting -- */
    f->light_propagates = net_get_u8(r) != 0;
    f->sunlight_propagates = net_get_u8(r) != 0;
    f->light_source = net_get_u8(r);
    if (f->light_source > LIGHT_MAX)
        f->light_source = LIGHT_MAX;

    /* -- map generation -- */
    f->is_ground_content = net_get_u8(r) != 0;

    /* -- interaction -- */
    f->walkable = net_get_u8(r) != 0;
    {
        uint8_t p = net_get_u8(r);
        if (p != POINTABLE_NOT && p != POINTABLE && p != POINTABLE_BLOCKING)
            p = POINTABLE; /* upstream's fallback for an unrecognized value */
        f->pointable = (PointabilityType)p;
    }
    f->diggable = net_get_u8(r) != 0;
    f->climbable = net_get_u8(r) != 0;
    f->buildable_to = net_get_u8(r) != 0;
    f->rightclickable = net_get_u8(r) != 0;
    f->damage_per_second = net_get_u32(r);

    /* -- liquid -- */
    f->liquid_type = (LiquidType)net_get_u8(r);
    if (f->liquid_type >= LIQUID_TYPE_END)
        f->liquid_type = LIQUID_NONE;
    f->liquid_move_physics = (f->liquid_type != LIQUID_NONE); /* default; tail may override */
    f->liquid_alternative_flowing = dup_str16(r);
    f->liquid_alternative_source = dup_str16(r);
    f->liquid_viscosity = net_get_u8(r);
    f->move_resistance = f->liquid_viscosity; /* default; tail may override */
    f->liquid_renewable = net_get_u8(r) != 0;
    f->liquid_range = net_get_u8(r);
    f->drowning = net_get_u8(r) != 0;
    f->floodable = net_get_u8(r) != 0;

    /* -- node boxes -- */
    nodebox_parse(r, &f->node_box);
    nodebox_parse(r, &f->selection_box);
    nodebox_parse(r, &f->collision_box);

    /* -- sound -- */
    soundspec_parse(r, &f->sound_footstep);
    soundspec_parse(r, &f->sound_dig);
    soundspec_parse(r, &f->sound_dug);

    /* -- legacy -- */
    f->legacy_facedir_simple = net_get_u8(r);
    f->legacy_wallmounted = net_get_u8(r);

    /* -- tail: node_dig_prediction is unconditional (present since long
     * before CONTENTFEATURES_VERSION 13 existed); everything after it is
     * gated on "is there more data", not a version number. A server that
     * stops partway through this tail is an OLDER one, not a corrupt
     * one -- see this file's top-of-file doc comment. */
    f->node_dig_prediction = dup_str16(r);

    if (net_reader_remaining(r) == 0) return; /* pre-5.3.0-dev */
    f->leveled_max = net_get_u8(r);

    if (net_reader_remaining(r) == 0) return; /* pre-5.4.0-dev */
    {
        uint8_t a = net_get_u8(r);
        if (a >= ALPHA_MODE_END || a == ALPHAMODE_LEGACY_COMPAT)
            a = ALPHAMODE_OPAQUE;
        f->alpha = (AlphaMode)a; /* overwrites the legacy-derived guess above */
    }

    if (net_reader_remaining(r) == 0) return; /* pre-5.5.0-dev */
    f->move_resistance = net_get_u8(r);
    f->liquid_move_physics = net_get_u8(r) != 0;

    if (net_reader_remaining(r) == 0) return; /* pre-5.8.0-dev */
    f->post_effect_color_shaded = net_get_u8(r);

    /* Any bytes past here belong to a field newer than what this parser
     * knows about. That's fine and expected -- NOT an error -- per the
     * canRead() convention above; just don't go looking for more fields
     * we don't have names for yet. */
}

/* ---------- table management ---------- */

void nodedef_table_init(NodeDefTable *t) {
    t->defs = NULL;
    t->capacity = 0;
}

void nodedef_table_free(NodeDefTable *t) {
    for (size_t i = 0; i < t->capacity; ++i) {
        if (t->defs[i].name)
            content_features_free(&t->defs[i]);
    }
    free(t->defs);
    t->defs = NULL;
    t->capacity = 0;
}

const ContentFeatures *nodedef_table_get(const NodeDefTable *t, uint16_t id) {
    if (id >= t->capacity)
        return NULL;
    if (!t->defs[id].name)
        return NULL;
    return &t->defs[id];
}

/* Grows t->defs (if needed) so that index `id` is valid, zero-filling the
 * newly added slots (name == NULL there, i.e. "not defined" -- matches
 * nodedef_table_get()'s contract). Doubles from a small initial size
 * rather than jumping straight to 65536 slots: real nodedefs are
 * typically a few hundred to a few thousand entries, and a
 * ContentFeatures is not small (a handful of TileDefs and NodeBoxes
 * each), so eagerly allocating the full id space would waste a lot of
 * memory for the common case. Same doubling approach as
 * zstd_stream_decompress's output buffer, for the same reason. */
static void table_ensure_capacity(NodeDefTable *t, size_t min_capacity) {
    if (min_capacity <= t->capacity)
        return;

    size_t new_capacity = t->capacity == 0 ? 256 : t->capacity;
    while (new_capacity < min_capacity)
        new_capacity *= 2;

    ContentFeatures *new_defs = realloc(t->defs, sizeof(ContentFeatures) * new_capacity);
    if (!new_defs) {
        fprintf(stderr, "nodedef: out of memory growing table to %zu entries\n", new_capacity);
        exit(1);
    }
    memset(new_defs + t->capacity, 0, sizeof(ContentFeatures) * (new_capacity - t->capacity));
    t->defs = new_defs;
    t->capacity = new_capacity;
}

void nodedef_parse(NodeDefTable *t, const uint8_t *data, size_t len) {
    /* Matches upstream's NodeDefManager::deSerialize: replace the whole
     * table on every NODEDEF packet, don't try to merge with whatever
     * was there before. */
    nodedef_table_free(t);

    NetReader r;
    net_reader_init(&r, data, len);

    uint8_t version = net_get_u8(&r);
    if (version < 1) {
        fprintf(stderr, "nodedef: unsupported NodeDefManager version %u (need >=1)\n",
                (unsigned)version);
        exit(1);
    }

    uint16_t count = net_get_u16(&r);

    uint32_t table_len;
    const uint8_t *table_bytes = net_get_str32(&r, &table_len);
    NetReader table_r;
    net_reader_init(&table_r, table_bytes, table_len);

    for (uint16_t n = 0; n < count; ++n) {
        uint16_t id = net_get_u16(&table_r);

        /* Each record is ALSO string16-wrapped -- see this file's doc
         * comment. We don't currently use that to skip unparseable
         * records (an actual parse failure below hard-fails the whole
         * table, matching upstream's SerializationError propagating up
         * and disconnecting), but it does mean a corrupt/short record
         * is caught immediately by net_get_str16's own bounds check
         * rather than by us reading garbage past it. */
        uint16_t record_len;
        const uint8_t *record_bytes = net_get_str16(&table_r, &record_len);
        NetReader record_r;
        net_reader_init(&record_r, record_bytes, record_len);

        ContentFeatures f;
        memset(&f, 0, sizeof(f));
        content_features_parse(&record_r, &f);

        if (id == CONTENT_IGNORE || id == CONTENT_AIR || id == CONTENT_UNKNOWN) {
            fprintf(stderr, "nodedef: NOTE: server tried to redefine builtin id %u, ignoring\n",
                    (unsigned)id);
            content_features_free(&f);
            continue;
        }
        if (f.name[0] == '\0') {
            fprintf(stderr, "nodedef: NOTE: received a record with an empty name, ignoring\n");
            content_features_free(&f);
            continue;
        }

        table_ensure_capacity(t, (size_t)id + 1);
        if (t->defs[id].name)
            content_features_free(&t->defs[id]); /* shouldn't happen, but don't leak if it does */
        t->defs[id] = f;
    }
}
