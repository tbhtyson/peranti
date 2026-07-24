#include "webgpu_context.h"
#include "surface.h"
#include <stdio.h>
#include <stdlib.h>

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

WebGpuContext webgpu_context_create(uint32_t width, uint32_t height,
                                    const char *window_title) {
  /* --- Instance (entry point for interacting with hardware) --- */
  WGPUInstanceDescriptor instance_desc = {0};
  WGPUInstance instance = wgpuCreateInstance(&instance_desc);
  if (!instance) {
    fprintf(stderr, "Failed to create WGPUInstance\n");
    exit(1);
  }

  /* --- Window (window) --- */
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    exit(1);
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window =
      glfwCreateWindow(width, height, window_title, NULL, NULL);
  if (!window) {
    fprintf(stderr, "Failed to create GLFW window\n");
    exit(1);
  }

  /* --- Surface (bridges the window and the gpu) --- */
  WGPUSurface surface = create_surface(instance, window);

  /* --- Adapter (bridges code and hardware) --- */
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
  config.width = width;
  config.height = height;
  config.viewFormatCount = 0;
  config.viewFormats = NULL;
  config.alphaMode = WGPUCompositeAlphaMode_Auto;
  config.presentMode = WGPUPresentMode_Fifo;

  wgpuSurfaceConfigure(surface, &config);

  WGPUQueue queue = wgpuDeviceGetQueue(device);

  WebGpuContext ctx = {0};
  ctx.window = window;
  ctx.instance = instance;
  ctx.surface = surface;
  ctx.adapter = adapter;
  ctx.device = device;
  ctx.queue = queue;
  ctx.surface_format = surface_format;
  ctx.width = width;
  ctx.height = height;
  return ctx;
}
void webgpu_context_destroy(WebGpuContext *ctx) {
  wgpuQueueRelease(ctx->queue);
  wgpuDeviceRelease(ctx->device);
  wgpuAdapterRelease(ctx->adapter);
  wgpuSurfaceRelease(ctx->surface);
  wgpuInstanceRelease(ctx->instance);

  glfwDestroyWindow(ctx->window);
  glfwTerminate();
}
