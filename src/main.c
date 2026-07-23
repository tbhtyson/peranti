#include <string.h>
#include <wgpu.h>
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "webgpu-headers/webgpu.h"

#include <stdio.h>
#include <stdlib.h>

static WGPUSurface create_surface_x11(WGPUInstance instance,
                                      GLFWwindow *window) {
  WGPUSurfaceSourceXlibWindow x11_desc = {0};
  x11_desc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
  x11_desc.display = glfwGetX11Display();
  x11_desc.window = (uint64_t)glfwGetX11Window(window);

  WGPUSurfaceDescriptor surface_desc = {0};
  surface_desc.nextInChain = (const WGPUChainedStruct *)&x11_desc;

  WGPUSurface surface = wgpuInstanceCreateSurface(instance, &surface_desc);
  if (!surface) {
    fprintf(stderr, "Failed to create WGPUSurface (X11)\n");
    exit(1);
  }
  return surface;
}

static WGPUSurface create_surface(WGPUInstance instance, GLFWwindow *window) {
#if defined(_WIN32)
  return create_surface_win32(instance, window);
#elif defined(__APPLE__)
  return create_surface_cocoa(instance, window);
#else
  switch (glfwGetPlatform()) {
  case GLFW_PLATFORM_X11:
    return create_surface_x11(instance, window);
  // case GLFW_PLATFORM_WAYLAND: return create_surface_wayland(instance,
  // window);
  default:
    fprintf(stderr, "Unsupported GLFW platform for surface creation\n");
    exit(1);
  }
#endif
}

/* --- Callbacks for synchronous-in-practice adapter/device requests --- */

static void handle_adapter_request(WGPURequestAdapterStatus status,
                                   WGPUAdapter adapter, WGPUStringView message,
                                   void *userdata1, void *userdata2) {
  (void)userdata2;
  if (status != WGPURequestAdapterStatus_Success) {
    fprintf(stderr, "Could not request WebGPU adapter: %.*s\n",
            (int)message.length, message.data);
    exit(1);
  }
  *(WGPUAdapter *)userdata1 = adapter;
}

static void handle_device_request(WGPURequestDeviceStatus status,
                                  WGPUDevice device, WGPUStringView message,
                                  void *userdata1, void *userdata2) {
  (void)userdata2;
  if (status != WGPURequestDeviceStatus_Success) {
    fprintf(stderr, "Could not request WebGPU device: %.*s\n",
            (int)message.length, message.data);
    exit(1);
  }
  *(WGPUDevice *)userdata1 = device;
}

int main(void) {
  /* --- 1. Instance --- */
  WGPUInstanceDescriptor instance_desc = {0};
  WGPUInstance instance = wgpuCreateInstance(&instance_desc);
  if (!instance) {
    fprintf(stderr, "Failed to create WGPUInstance\n");
    return 1;
  }

  /* --- 2. Window --- */
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return 1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window = glfwCreateWindow(1600, 900, "Peranti", NULL, NULL);
  if (!window) {
    fprintf(stderr, "Failed to create GLFW window\n");
    return 1;
  }

  /* --- 3. Surface --- */
  WGPUSurface surface = create_surface(instance, window);

  /* --- 4. Adapter --- */
  WGPUAdapter adapter = NULL;
  WGPURequestAdapterOptions adapter_opts = {0};
  adapter_opts.compatibleSurface = surface;

  WGPURequestAdapterCallbackInfo adapter_callback_info = {0};
  adapter_callback_info.nextInChain = NULL;
  adapter_callback_info.mode = WGPUCallbackMode_AllowProcessEvents;
  adapter_callback_info.callback = handle_adapter_request;
  adapter_callback_info.userdata1 = &adapter;
  adapter_callback_info.userdata2 = NULL;

  wgpuInstanceRequestAdapter(instance, &adapter_opts, adapter_callback_info);

  WGPUDevice device = NULL;
  WGPUDeviceDescriptor device_desc = {0};

  WGPURequestDeviceCallbackInfo device_callback_info = {0};
  device_callback_info.nextInChain = NULL;
  device_callback_info.mode = WGPUCallbackMode_AllowProcessEvents;
  device_callback_info.callback = handle_device_request;
  device_callback_info.userdata1 = &device;
  device_callback_info.userdata2 = NULL;

  wgpuAdapterRequestDevice(adapter, &device_desc, device_callback_info);
  printf("Instance: %p\n", (void *)instance);
  printf("Surface:  %p\n", (void *)surface);
  printf("Adapter:  %p\n", (void *)adapter);
  printf("Device:   %p\n", (void *)device);

  /*useful for the rest, seperate from adapter*/
  WGPUSurfaceCapabilities caps = {0};
  WGPUStatus caps_status = wgpuSurfaceGetCapabilities(surface, adapter, &caps);
  if (caps_status != WGPUStatus_Success) {
    fprintf(stderr, "Failed to get surface capabilities\n");
    exit(1);
  }

  WGPUTextureFormat surface_format = caps.formats[0];

  WGPUSurfaceConfiguration config = {0};
  config.device = device;
  config.format = surface_format;
  config.usage = WGPUTextureUsage_RenderAttachment;
  config.width = 1600;
  config.height = 900;
  config.viewFormatCount = 0;
  config.viewFormats = NULL;
  config.alphaMode = WGPUCompositeAlphaMode_Auto;
  config.presentMode = WGPUPresentMode_Fifo;

  wgpuSurfaceConfigure(surface, &config);

  WGPUQueue queue = wgpuDeviceGetQueue(device);


  // --- Shader module ---
static const char *shader_source =
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {\n"
    "    var pos = array<vec2f, 3>(\n"
    "        vec2f( 0.0,  0.5),\n"
    "        vec2f(-0.5, -0.5),\n"
    "        vec2f( 0.5, -0.5)\n"
    "    );\n"
    "    return vec4f(pos[in_vertex_index], 0.0, 1.0);\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs_main() -> @location(0) vec4f {\n"
    "    return vec4f(1.0, 0.5, 0.2, 1.0);\n"
    "}\n";

WGPUShaderSourceWGSL wgsl_desc = {0};
wgsl_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
wgsl_desc.code = (WGPUStringView){ .data = shader_source, .length = strlen(shader_source) };

WGPUShaderModuleDescriptor shader_desc = {0};
shader_desc.nextInChain = (const WGPUChainedStruct *)&wgsl_desc;

WGPUShaderModule shader_module = wgpuDeviceCreateShaderModule(device, &shader_desc);

// --- Vertex state (no vertex buffers — positions are hardcoded in the shader) ---
WGPUVertexState vertex_state = {0};
vertex_state.module = shader_module;
vertex_state.entryPoint = (WGPUStringView){ .data = "vs_main", .length = strlen("vs_main") };
vertex_state.constantCount = 0;
vertex_state.constants = NULL;
vertex_state.bufferCount = 0;
vertex_state.buffers = NULL;

// --- Primitive state ---
WGPUPrimitiveState primitive_state = {0};
primitive_state.topology = WGPUPrimitiveTopology_TriangleList;
primitive_state.stripIndexFormat = WGPUIndexFormat_Undefined;
primitive_state.frontFace = WGPUFrontFace_CCW;
primitive_state.cullMode = WGPUCullMode_None;

// --- Color target + fragment state ---
WGPUColorTargetState color_target = {0};
color_target.format = surface_format;   // from your surface capabilities query
color_target.blend = NULL;
color_target.writeMask = WGPUColorWriteMask_All;

WGPUFragmentState fragment_state = {0};
fragment_state.module = shader_module;
fragment_state.entryPoint = (WGPUStringView){ .data = "fs_main", .length = strlen("fs_main") };
fragment_state.constantCount = 0;
fragment_state.constants = NULL;
fragment_state.targetCount = 1;
fragment_state.targets = &color_target;

// --- Pipeline ---
WGPURenderPipelineDescriptor pipeline_desc = {0};
pipeline_desc.layout = NULL;   // NULL = let the implementation infer layout from the shader
pipeline_desc.vertex = vertex_state;
pipeline_desc.primitive = primitive_state;
pipeline_desc.depthStencil = NULL;
pipeline_desc.multisample = (WGPUMultisampleState){
    .nextInChain = NULL,
    .count = 1,
    .mask = 0xFFFFFFFF,
    .alphaToCoverageEnabled = WGPU_FALSE
};
pipeline_desc.fragment = &fragment_state;

WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);


  /* --- Main loop (no rendering yet — just confirms the window stays open) ---
   */
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    WGPUSurfaceTexture surface_texture = {0};
    wgpuSurfaceGetCurrentTexture(surface, &surface_texture);

    if (surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
      fprintf(stderr, "Failed to acquire surface texture (status %d)\n",
              surface_texture.status);
      // for now: bail. Later: handle Timeout/Outdated by reconfiguring the
      // surface and skipping this frame.
      exit(1);
    }

    WGPUTextureView view = wgpuTextureCreateView(surface_texture.texture, NULL);
    // ... render pass targets `view` here ...
    // 2. record commands
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);

    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.nextInChain = NULL;
    color_attachment.view = view;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_attachment.resolveTarget = NULL;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = (WGPUColor){0.5, 0.8, 0.9, 1.0};

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    // (once you have a pipeline: wgpuRenderPassEncoderSetPipeline,
    // wgpuRenderPassEncoderDraw go here)

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // 3. submit
    WGPUCommandBufferDescriptor cmd_buffer_desc = {0};
    WGPUCommandBuffer cmd_buffer =
        wgpuCommandEncoderFinish(encoder, &cmd_buffer_desc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(
        queue, 1, &cmd_buffer); // queue: grab once via
                                // wgpuDeviceGetQueue(device), near device setup
    wgpuCommandBufferRelease(cmd_buffer);
    wgpuSurfacePresent(surface);
    wgpuTextureViewRelease(view);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
