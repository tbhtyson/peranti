@vs vs
// Packed quad descriptor -- one struct per quad, matching core/greedy_mesh.c's
// QuadList output. Two uint32s: geometry packed into the first, material/tile
// index given the full second word since bit-budget tuning is premature
// before profiling (see CONVENTIONS.md's "profile before optimizing").
//
// word0 bit layout:
//   bits  0- 4: x (0-31, covers CHUNK_SIZE=16 with room to spare)
//   bits  5- 9: y
//   bits 10-14: z
//   bits 15-19: width  - 1  (stored as width-1 so 1-32 fits in 5 bits)
//   bits 20-24: height - 1
//   bits 25-26: axis (0=X, 1=Y, 2=Z -- which axis this quad's plane is
//               perpendicular to)
//   bit  27:    direction (0=negative facing, 1=positive facing)
//   bits 28-31: reserved
struct sb_quad {
    uint word0;
    uint material_index;
};

layout(binding=0) readonly buffer quads {
    sb_quad quad[];
};

layout(binding=0) uniform vs_params {
    mat4 view_proj;
    // Per-chunk world offset, matching the exact formula confirmed against
    // clientmap.cpp: (block_pos * MAP_BLOCKSIZE - camera_offset) * BS.
    // Computed once per chunk on the CPU side (core/projection.c's job),
    // not recomputed per-vertex here.
    vec3 chunk_world_offset;
};

out vec2 uv;
flat out uint material_index;

void main() {
    sb_quad q = quad[gl_InstanceIndex];

    uint x      = (q.word0 >>  0) & 0x1Fu;
    uint y      = (q.word0 >>  5) & 0x1Fu;
    uint z      = (q.word0 >> 10) & 0x1Fu;
    uint width  = ((q.word0 >> 15) & 0x1Fu) + 1u;
    uint height = ((q.word0 >> 20) & 0x1Fu) + 1u;
    uint axis   = (q.word0 >> 25) & 0x3u;
    uint dir    = (q.word0 >> 27) & 0x1u;

    // Same {0,1,2, 2,3,0} triangulation confirmed from Luanti's own
    // content_mapblock.cpp quad_indices -- two triangles, fan around
    // corner 0, sharing winding convention with the rest of the graft.
    // gl_VertexIndex resets to 0..5 every instance (non-indexed
    // instanced draw), never a running total across instances.
    const int corner_lut[6] = int[6](0, 1, 2, 2, 3, 0);
    int corner = corner_lut[gl_VertexIndex];

    // Local-space corner offsets within the quad, before axis mapping --
    // (0,0), (w,0), (w,h), (0,h). Same shape for every axis; only which
    // world components they map to differs.
    vec2 local_offsets[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(float(width), 0.0),
        vec2(float(width), float(height)),
        vec2(0.0, float(height))
    );
    vec2 local = local_offsets[corner];

    // --- Y-axis case (up/down facing quads) shown explicitly. ---
    // X and Z axis cases follow the identical pattern: swap which two
    // world components `local` maps onto, and which component `dir`
    // offsets along -- e.g. for axis==0 (X), local.x maps to world Z,
    // local.y maps to world Y, and dir offsets world X by `dir` instead
    // of by `height`/`width` here.
    vec3 pos;
    if (axis == 1u) {
        pos = vec3(float(x) + local.x, float(y) + float(dir), float(z) + local.y);
    } else if (axis == 2u) {
        // axis == 0 (X-facing) and axis == 2 (Z-facing) cases,
        // same structure as above with components swapped per axis.
        pos = vec3(float(x) + local.x, float(y) + local.y, float(z) + float(dir));
    } else {
        pos = vec3(float(x) + float(dir), float(y) + local.y, float(z) + local.x);
    }

    vec3 world_pos = pos + chunk_world_offset;
    gl_Position = view_proj * vec4(world_pos, 1.0);

    uv = local;
    material_index = q.material_index;
}
@end

@fs fs
in vec2 uv;
flat in uint material_index;
out vec4 frag_color;

void main() {
    // TODO: sample the terrain texture atlas using material_index to
    // compute the atlas region, then uv within that region.
    frag_color = vec4(uv, float(material_index) / 255.0, 1.0);
}
@end

@program terrain vs fs
