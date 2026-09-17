#include "app.h"

// Single definition point for every extern declared in app.h. init.c writes
// these during peranti_init(); loop.c reads (and sometimes writes, e.g.
// updateSwapchain) them every frame.

const ShaderData ShaderDataDefault = {
    .lightPos = {0.0f, -10.0f, 10.0f, 0.0f},
    .selected = 1,
};

bool updateSwapchain = {false};

SwapchainState sc = {0};

AppState app = {0};

Net *net = NULL;

// Zero-initialized until init.c's world_init() call gives it a real slot
// array -- see the comment on `extern World world` in app.h.
World world = {0};

PlayerNetState playerNetState = {0};

// The original monolith never freed anything -- main() just fell off the
// end of the while loop and let the OS reclaim everything on process exit.
// This stub preserves that (lack of) behavior exactly rather than inventing
// new cleanup logic as part of a supposedly mechanical split. Real
// vkDestroy*/vmaDestroy*/SDL_Destroy* calls belong here whenever that gets
// written, not before.
//
// net_destroy() and world_destroy() are exceptions: closing the UDP socket
// cleanly (vs. letting the OS reclaim the fd) matters for anything the
// server-side is tracking as a live peer, and world_destroy() frees a real
// heap allocation (the slot array) that the OS would reclaim anyway on
// exit but that's still worth freeing explicitly now that this isn't just
// throwaway test-scene state anymore.
int peranti_shutdown(void) {
  net_destroy(net);
  world_destroy(&world);
  return 0;
}
