#ifndef PERANTI_WEBGPU_CONTEXT_H
#define PERANTI_WEBGPU_CONTEXT_H

#include <GLFW/glfw3.h>
#include "webgpu-headers/webgpu.h"

typedef struct {
    GLFWwindow      *window;
    WGPUInstance     instance;
    WGPUSurface      surface;
    WGPUAdapter      adapter;
    WGPUDevice       device;
    WGPUQueue        queue;
    WGPUTextureFormat surface_format;
    uint32_t         width;
    uint32_t         height;
} WebGpuContext;

// Creates the window and brings up instance -> surface -> adapter -> device
// -> queue -> surface configuration, in that order. Hard-fails (exit(1)) on
// any step that fails, per project convention.
WebGpuContext webgpu_context_create(uint32_t width, uint32_t height, const char *window_title);

// Releases every handle in ctx, in reverse creation order, then terminates GLFW.
void webgpu_context_destroy(WebGpuContext *ctx);

#endif // PERANTI_WEBGPU_CONTEXT_H
