#ifndef P_SOKOL_STANDALONE_H
#define P_SOKOL_STANDALONE_H
// sokol_impl_standalone.c
//
// Dev-harness variant. This is the ONLY file in the project allowed to
// include sokol_app.h/sokol_glue.h (CONVENTIONS.md rule 5) -- it owns the
// window/GL-context/event-loop for standalone testing via src/standalone/
// main.c. Nothing in src/core/ or src/render/ may depend on anything this
// file provides; the graft target (src/render/sokol_impl_luanti.c) is a
// separate, sokol_app-free file used instead when embedded in a host
// application that owns its own window (e.g. Luanti/Irrlicht).
#define SOKOL_IMPL
#define SOKOL_TIME_IMPL

#if defined(__APPLE__)
#define SOKOL_METAL
#elif defined(_WIN32)
#define SOKOL_D3D11
#elif defined(SOKOL_USE_VULKAN)
#define SOKOL_VULKAN
#else
#define SOKOL_GLCORE
#endif

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"

// See CONVENTIONS.md rule 6 -- undef immediately so nothing included later
// in the same translation unit can re-arm the implementation macros.
#undef SOKOL_IMPL
#undef SOKOL_GFX_IMPL
#undef SOKOL_TIME_IMPL

#endif
