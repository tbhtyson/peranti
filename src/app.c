#include "app.h"
#include "content.h"

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
// net_destroy(), world_destroy(), and content_reset() are exceptions:
// closing the UDP socket cleanly (vs. letting the OS reclaim the fd)
// matters for anything the server-side is tracking as a live peer,
// world_destroy() frees a real heap allocation (the slot array) that the
// OS would reclaim anyway on exit but that's still worth freeing
// explicitly now that this isn't just throwaway test-scene state anymore,
// and content_reset() frees the NodeDefTable/ItemDefTable/MediaSync that
// content.c has been accumulating into since the first NODEDEF/ITEMDEF/
// ANNOUNCE_MEDIA packet -- same "real heap allocations worth freeing
// explicitly" reasoning as world_destroy(). There's no reconnect-to-a-
// different-server path yet (this is a connect-once, run-until-exit
// architecture today), so process shutdown is content_reset()'s only
// call site for now; if a reconnect feature ever gets added, that path
// will need to call it too, before the new connection's first NODEDEF
// arrives.
int peranti_shutdown(void) {
  net_destroy(net);
  world_destroy(&world);
  content_reset();
  return 0;
}
