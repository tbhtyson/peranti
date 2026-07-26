#ifndef PERANTI_PIPELINE_H
#define PERANTI_PIPELINE_H

#include "webgpu-headers/webgpu.h"

typedef struct {
    WGPURenderPipeline pipeline;
    WGPUBindGroupLayout bind_group_layout;
} PipelineWithLayout;

// Loads shaders/triangle.wgsl, builds the shader module, and creates a
// render pipeline targeting the given surface format. The returned
// bind_group_layout is owned by the caller and must be released with
// wgpuBindGroupLayoutRelease once no longer needed.
PipelineWithLayout pipeline_create_triangle(WGPUDevice device, WGPUTextureFormat surface_format);

#endif // PERANTI_PIPELINE_H
