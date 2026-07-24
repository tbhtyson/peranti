#ifndef PERANTI_PIPELINE_H
#define PERANTI_PIPELINE_H

#include "webgpu-headers/webgpu.h"

// Loads shaders/triangle.wgsl, builds the shader module, and creates a
// render pipeline targeting the given surface format.
WGPURenderPipeline pipeline_create_triangle(WGPUDevice device, WGPUTextureFormat surface_format);

#endif // PERANTI_PIPELINE_H
