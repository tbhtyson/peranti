#include "surface.h"
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
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

WGPUSurface create_surface(WGPUInstance instance, GLFWwindow *window) {
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
