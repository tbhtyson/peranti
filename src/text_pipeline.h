#ifndef PERANTI_TEXT_PIPELINE_H
#define PERANTI_TEXT_PIPELINE_H

#include "webgpu-headers/webgpu.h"

// Loads shaders/text.wgsl and builds a render pipeline for screen-space
// 2D text quads. No bind group layout -- text vertices already carry
// NDC-space positions baked in at mesh-build time, so there's nothing
// to bind. See text_mesh_build.
WGPURenderPipeline text_pipeline_create(WGPUDevice device, WGPUTextureFormat surface_format);

#endif // PERANTI_TEXT_PIPELINE_H
