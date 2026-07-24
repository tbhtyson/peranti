#ifndef PERANTI_SURFACE_H
#define PERANTI_SURFACE_H

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "webgpu-headers/webgpu.h"

// Dispatches to the correct platform-specific surface creation function
// (X11 for now; Wayland/Win32/Cocoa added as needed).
WGPUSurface create_surface(WGPUInstance instance, GLFWwindow *window);

#endif // PERANTI_SURFACE_H
