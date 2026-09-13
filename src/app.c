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

// The original monolith never freed anything -- main() just fell off the
// end of the while loop and let the OS reclaim everything on process exit.
// This stub preserves that (lack of) behavior exactly rather than inventing
// new cleanup logic as part of a supposedly mechanical split. Real
// vkDestroy*/vmaDestroy*/SDL_Destroy* calls belong here whenever that gets
// written, not before.
int peranti_shutdown(void) {
  return 0;
}
