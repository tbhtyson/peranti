#ifndef PERANTI_MESH_H
#define PERANTI_MESH_H
#include "world.h"

// Placeholder solidity model until Phase 6's real content-definition
// lookup exists: everything except air/ignore is treated as opaque and
// belongs in the greedy-mesh occupancy mask. Real drawtypes other than
// "normal" (nodebox, plantlike, liquid, glasslike, etc.) still need
// excluding once content defs exist -- see the Luanti edge-cases list.
// These two ids are Luanti's own reserved constants (mapnode.h), not
// something Peranti gets to choose.
#define CONTENT_AIR 126
#define CONTENT_IGNORE 127

// Per-axis occupancy, padded by one node in the axis's own direction only
// (bits 0 and 17 are the neighbor block's border; bits 1..16 are this
// block's own 16 nodes along that axis). The other two axes are never
// padded -- a per-axis mask only ever gets shifted along its own axis, so
// only the 6 face-neighbors are needed, never edge/corner diagonals.
//
//   cols[0][y][z] -- X-axis columns, indexed by this block's own (y, z)
//   cols[1][x][z] -- Y-axis columns, indexed by this block's own (x, z)
//   cols[2][x][y] -- Z-axis columns, indexed by this block's own (x, y)
typedef struct {
  uint32_t cols[3][16][16];
} OccupancyMasks;

// Pulls `coord`'s own Mapblock plus its 6 face-neighbors from `w` and
// fills `out`. A neighbor (or `coord` itself) not present in `w` is
// treated as entirely non-solid -- see the note on holes vs. stale
// boundary faces above. Does not modify `w`.
void occupancy_build(World *w, ChunkCoord coord, OccupancyMasks *out);

// A single merged quad. (x, y, z) is the local coordinate (0..15) of the
// solid node the quad is attached to -- not a separate "face plane
// position"; `direction` says which side of that node the face sits on.
// `axis` says which axis the quad is perpendicular to (0=X, 1=Y, 2=Z);
// `direction` is 0 for the face pointing in the -axis direction, 1 for
// +axis. `height` is the quad's extent along the plane's first coordinate
// (y for axis 0, x for axis 1, x for axis 2 -- i.e. `a` in occupancy_build's
// cols[axis][a][b] convention) and `width` is the extent along the second
// (`b`) -- both starting at (x,y,z) and growing in the positive direction
// only. All nodes covered by the quad share `content_id`.
typedef struct {
  uint8_t x, y, z;
  uint8_t width, height;
  uint8_t axis;
  uint8_t direction;
  uint16_t content_id;
} Quad;

// Every node exposed on every one of its 6 faces is the theoretical worst
// case (only reachable with an adversarial checkerboard of distinct
// content ids that never merges) -- the fixed capacity below has to cover
// it since mesh_build hard-fails rather than silently truncating output.
#define MESH_MAX_QUADS (16 * 16 * 16 * 6)

typedef struct {
  Quad *quads; // heap-allocated, fixed capacity (MESH_MAX_QUADS)
  uint32_t capacity;
  uint32_t count;
} Mesh;

void mesh_init(Mesh *m);
void mesh_destroy(Mesh *m);

// Builds a greedy-merged quad list for `coord` into `out` (out->count is
// reset to 0 first). If `coord` isn't loaded in `w`, out->count is left at
// 0 -- this function doesn't decide whether meshing an unloaded block was
// a caller mistake, it just has nothing to produce.
void mesh_build(World *w, ChunkCoord coord, Mesh *out);

#endif
