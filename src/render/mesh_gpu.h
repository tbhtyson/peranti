#ifndef PERANTI_MESH_GPU_H
#define PERANTI_MESH_GPU_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
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

uint32_t pack_quad_word0(uint32_t x, uint32_t y, uint32_t z, 
                         uint32_t width, uint32_t height, 
                         uint32_t axis, uint32_t direction);

// Called once, right after sg_setup() (and its GL-state save/restore
// bracket) in renderingengine.cpp. Builds the vertex-pulling
// shader/pipeline -- a fixed GPU object, not rebuilt per frame.
void peranti_render_init(void);

// Called every frame from game.cpp, between beginScene()'s clear and
// Irrlicht's own draw_scene() call -- see the graft-site discussion for
// why that ordering matters (shared depth buffer, single clear per frame).
void peranti_draw_frame(void);

#ifdef __cplusplus
}
#endif

#endif
