#ifndef P_SOKOL_LUANTI_H
#define P_SOKOL_LUANTI_H
// sokol_impl_luanti.c
//
// Luanti-graft variant of sokol_impl.c.
//
// Deliberately does NOT include sokol_app.h or sokol_glue.h:
//   - sokol_app.h owns window/GL-context/event-loop creation. Inside Luanti,
//     Irrlicht already owns the window and GL context, so sokol_app has
//     nothing to do and must not run.
//   - sokol_glue.h exists solely to read sokol_app's window/context state
//     (sapp_get_environment(), sapp_acquire_swapchain()) and repackage it
//     into sg_environment/sg_swapchain. With no sokol_app instance, those
//     calls are undefined -- sokol_glue is structurally inseparable from
//     sokol_app, not an independent module, so it drops out too.
//
// Confirmed via grep: no file outside main.c references sapp_/SAPP_, so
// nothing else in this codebase (gfx, world, mesh, camera) depends on
// sokol_app being present.
//
// Backend is hardcoded to SOKOL_GLCORE (not conditional on platform, unlike
// the standalone sokol_impl.c): Irrlicht's context on desktop Linux/macOS/
// Windows is GL, and matching that backend means zero new link dependencies
// (GL libs are already linked into the Luanti binary via Irrlicht's own
// CMake config). Vulkan/Metal/D3D11 would require new link lines Irrlicht
// doesn't already provide.
#define SOKOL_IMPL
#define SOKOL_TIME_IMPL
#define SOKOL_GLCORE

#include "third_party/sokol/sokol_gfx.h"
#include "third_party/sokol/sokol_log.h"
#include "third_party/sokol/sokol_time.h"

// sokol_gfx.h's implementation block is guarded only by `#ifdef SOKOL_GFX_IMPL`
// (unlike its declaration section, which has a proper SOKOL_GFX_INCLUDED
// "already seen" latch). SOKOL_IMPL/SOKOL_GFX_IMPL stay defined for the rest
// of this translation unit otherwise, so any later `#include "sokol_gfx.h"`
// elsewhere in the graft (e.g. chunk_render.h, for declarations only) would
// silently re-emit the entire implementation a second time -> redefinition
// errors. Undefine immediately after use so nothing downstream can re-arm it.
#undef SOKOL_IMPL
#undef SOKOL_GFX_IMPL
#undef SOKOL_TIME_IMPL

// sg_environment/sg_swapchain must be built by hand from Irrlicht's own GL
// context info at the call site (wherever sg_setup()/sg_begin_pass() end up
// living in the graft) -- there is no sglue_environment()/sglue_swapchain()
// helper available here.

#endif
