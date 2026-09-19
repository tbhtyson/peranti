#ifndef PERANTI_MESH_LEGACY_H
#define PERANTI_MESH_LEGACY_H
#include "mesh.h"
#include "types.h"

// Converts a Mesh's Quads into a conventional (Vertex[], uint32_t[]) pair
// for the existing fixed-format vertex/index buffer pipeline. This is a
// bridge, not the permanent path -- Phase 3's vertex-pulling design
// replaces the fixed Vertex format (pos+normal+uv per vertex) with a
// packed SSBO the shader pulls from directly, at which point this file
// goes away rather than growing. It exists now purely to get real mesher
// output on screen before that design is built.
//
// *out_vertices and *out_indices are malloc'd; caller frees both.
// Indices are 32-bit (VK_INDEX_TYPE_UINT32) -- this was originally 16-bit
// and hard-failed past UINT16_MAX vertices, which real chunk-streaming
// hit almost immediately (88 loaded chunks alone produced 65300 combined
// vertices, right at the wall). 32-bit indices push that ceiling to
// ~4 billion, comfortably outliving this bridge's expected lifetime.
void mesh_to_legacy_buffers(const Mesh *mesh, Vertex **out_vertices,
                             uint32_t *out_vertex_count,
                             uint32_t **out_indices,
                             uint32_t *out_index_count);

#endif
