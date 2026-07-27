#ifndef P_SOKOL_H
#define P_SOKOL_H
// sokol_impl.c
#define SOKOL_IMPL
#define SOKOL_TIME_IMPL
#if defined(__APPLE__)
  #define SOKOL_METAL
#else
  #define SOKOL_VULKAN
#endif

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#include "sokol_time.h"

#endif
