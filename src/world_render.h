#ifndef PERANTI_WORLD_RENDER_H
#define PERANTI_WORLD_RENDER_H

/* Destroys the previous combined vertex/index buffer (if any -- the very
 * first call replaces the init-time test-cube buffer) and rebuilds it from
 * every chunk currently loaded in the global `world`, updating
 * app.vBuffer/vBufferAllocation/vBufSize/indexCount to match what loop.c's
 * draw call reads every frame.
 *
 * This is the mesh_legacy.h bridge's real target: one combined buffer, one
 * draw call, matching the existing pipeline exactly. It deliberately does
 * NOT introduce per-chunk buffers or multiple draw calls -- mesh_legacy.h's
 * own header comment already flags this whole bridge as temporary pending
 * Phase 3's vertex-pulling SSBO redesign, so a bigger multi-buffer
 * investment here would be throwaway work on top of throwaway work.
 *
 * Indices are 32-bit (VK_INDEX_TYPE_UINT32) -- this was originally 16-bit
 * and genuinely hit that wall in practice: 88 loaded chunks alone produced
 * 65300 combined vertices, right at UINT16_MAX. 32-bit indices push the
 * ceiling to ~4 billion, which comfortably outlives this bridge's expected
 * lifetime -- see mesh_legacy.h for the same change on the per-chunk side.
 *
 * NOT verified against a real GPU/Vulkan validation layer -- this project
 * has no Vulkan/SDL/VMA headers available in the sandbox this was written
 * in, unlike the pure-C net/mapblock work earlier. The buffer-lifetime
 * reasoning (waiting for the GPU to finish with the old buffer before
 * destroying it -- see the vkDeviceWaitIdle call in the .c file) is sound
 * by Vulkan's own synchronization rules, but hasn't been exercised on
 * real hardware. Run this under validation layers before trusting it.
 *
 * Caller decides *when* to call this (see loop.c) -- this function only
 * implements *how*.
 */
void world_render_rebuild(void);

#endif
