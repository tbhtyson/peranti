#ifndef P_SOKOL_H
#define P_SOKOL_H
// sokol_impl.c
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

#endif
