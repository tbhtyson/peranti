#ifndef PERANTI_MESH_LEGACY_H
#define PERANTI_MESH_LEGACY_H
#include "mesh.h"
#include "types.h"

// Converts a Mesh's Quads into a conventional (Vertex[], uint16_t[]) pair
// for the existing fixed-format vertex/index buffer pipeline. This is a
// bridge, not the permanent path -- Phase 3's vertex-pulling design
// replaces the fixed Vertex format (pos+normal+uv per vertex, uint16
// indices) with a packed SSBO the shader pulls from directly, at which
// point this file goes away rather than growing. It exists now purely to
// get real mesher output on screen before that design is built.
//
// *out_vertices and *out_indices are malloc'd; caller frees both.
// Hard-fails if mesh->count * 4 would exceed UINT16_MAX vertices, since
// the existing pipeline is wired for 16-bit indices -- fine for a small
// synthetic test scene, not fine at real chunk-streaming scale (another
// reason this file is temporary).
void mesh_to_legacy_buffers(const Mesh *mesh, Vertex **out_vertices,
                             uint32_t *out_vertex_count,
                             uint16_t **out_indices,
                             uint32_t *out_index_count);

#endif
