#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef struct {
  const float *positions;   // 3 floats per vertex
  const float *normals;     // 3 floats per vertex
  const float *uvs;         // 2 floats per vertex
  const uint32_t *indices;
  size_t vertex_count;
  size_t index_count;
} PerantiRawMesh;

int peranti_extract_mapblock_mesh(int16_t mapblockX, int16_t mapblockY, int16_t mapblockZ, const PerantiRawMesh *mesh_data);

#ifdef __cplusplus
}
#endif
