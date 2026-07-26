#include "text_pipeline.h"
#include "text_mesh.h"
#include "text_wgsl.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WGPUShaderModule create_shader_module(WGPUDevice device,
                                             const char *wgsl_source) {
  WGPUShaderSourceWGSL wgsl_desc = {0};
  wgsl_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
  wgsl_desc.code =
      (WGPUStringView){.data = wgsl_source, .length = strlen(wgsl_source)};

  WGPUShaderModuleDescriptor shader_desc = {0};
  shader_desc.nextInChain = (WGPUChainedStruct *)&wgsl_desc;

  WGPUShaderModule shader_module =
      wgpuDeviceCreateShaderModule(device, &shader_desc);
  if (!shader_module) {
    fprintf(stderr, "Failed to create WGPUShaderModule (text)\n");
    exit(1);
  }
  return shader_module;
}

WGPURenderPipeline text_pipeline_create(WGPUDevice device,
                                        WGPUTextureFormat surface_format) {
  WGPUShaderModule shader_module = create_shader_module(device, text_wgsl_source);

  // --- Vertex attribute + buffer layout ---
  WGPUVertexAttribute vertex_attrs[2] = {0};

  vertex_attrs[0].format = WGPUVertexFormat_Float32x2; // NDC position, no z
  vertex_attrs[0].offset = offsetof(Vertex2D, x);
  vertex_attrs[0].shaderLocation = 0;

  vertex_attrs[1].format = WGPUVertexFormat_Float32x3;
  vertex_attrs[1].offset = offsetof(Vertex2D, r);
  vertex_attrs[1].shaderLocation = 1;

  WGPUVertexBufferLayout vertex_layout = {0};
  vertex_layout.arrayStride = sizeof(Vertex2D);
  vertex_layout.stepMode = WGPUVertexStepMode_Vertex;
  vertex_layout.attributeCount = 2;
  vertex_layout.attributes = vertex_attrs;

  // --- Vertex state ---
  WGPUVertexState vertex_state = {0};
  vertex_state.module = shader_module;
  vertex_state.entryPoint =
      (WGPUStringView){.data = "vs_main", .length = strlen("vs_main")};
  vertex_state.constantCount = 0;
  vertex_state.constants = NULL;
  vertex_state.bufferCount = 1;
  vertex_state.buffers = &vertex_layout;

  // --- no bind group layout, no pipeline layout -- text needs neither ---

  WGPUPrimitiveState primitive_state = {0};
  primitive_state.topology = WGPUPrimitiveTopology_TriangleList;
  primitive_state.stripIndexFormat = WGPUIndexFormat_Undefined;
  primitive_state.frontFace = WGPUFrontFace_CCW;
  primitive_state.cullMode = WGPUCullMode_None; // 2D quads -- winding doesn't matter here

  // --- Color target + fragment state ---
  WGPUColorTargetState color_target = {0};
  color_target.format = surface_format;
  color_target.blend = NULL;
  color_target.writeMask = WGPUColorWriteMask_All;

  WGPUFragmentState fragment_state = {0};
  fragment_state.module = shader_module;
  fragment_state.entryPoint =
      (WGPUStringView){.data = "fs_main", .length = strlen("fs_main")};
  fragment_state.constantCount = 0;
  fragment_state.constants = NULL;
  fragment_state.targetCount = 1;
  fragment_state.targets = &color_target;

  // --- Pipeline ---
  WGPURenderPipelineDescriptor pipeline_desc = {0};
  pipeline_desc.layout = NULL; // no bindings -- let the implementation infer
  pipeline_desc.vertex = vertex_state;
  pipeline_desc.primitive = primitive_state;
  pipeline_desc.depthStencil = NULL;
  pipeline_desc.multisample =
      (WGPUMultisampleState){.nextInChain = NULL,
                             .count = 1,
                             .mask = 0xFFFFFFFF,
                             .alphaToCoverageEnabled = WGPU_FALSE};
  pipeline_desc.fragment = &fragment_state;

  WGPURenderPipeline pipeline =
      wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);
  if (!pipeline) {
    fprintf(stderr, "Failed to create WGPURenderPipeline (text)\n");
    exit(1);
  }

  wgpuShaderModuleRelease(shader_module);
  return pipeline;
}
