#ifndef PERANTI_APP
#define PERANTI_APP

// Shared plumbing for the init/loop split. Anything declared here is needed
// by both init.c and loop.c (or by app.c, which owns the single instance of
// each extern global below). Nothing in this header is Peranti-feature
// code -- it's the base-cube prototype's own infrastructure, relocated
// verbatim out of the old main.c monolith.

#define VK_NO_PROTOTYPES
#include "types.h"
#include "vk_mem_alloc.h"
#include "volk/volk.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#define MAX_FRAMES_IN_FLIGHT 2
#define CGLM_ALL_UNALIGNED
#include <cglm/cglm.h>

typedef struct {
  VmaAllocation allocation;
  VmaAllocationInfo allocationInfo;
  VkBuffer buffer;
  VkDeviceAddress deviceAddress;
} ShaderDataBuffer;

typedef struct {
  mat4 projection;
  mat4 view;
  mat4 model[3];
  vec4 lightPos;
  uint32_t selected;
} ShaderData;

extern const ShaderData ShaderDataDefault;

static inline void chk(bool result) {
  if (!result) {
    fprintf(stderr, "Call returned an error\n");
    exit(result);
  }
}

static inline void chkvk(VkResult result) {
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Vulkan call returned %d\n", result);
    exit(1);
  }
}

extern bool updateSwapchain;

static inline void chkSwapchain(VkResult result) {
  if (result < VK_SUCCESS) {
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      updateSwapchain = true;
      return;
    }
    fprintf(stderr, "Vulkan call returned %d\n", result);
    exit(result);
  }
}

// Everything that has to be rebuilt when the window resizes (or the surface
// otherwise goes out of date): the swapchain itself, its image views, the
// per-image present semaphores, and the depth image/view sized to match.
typedef struct {
  VkDevice device;
  VmaAllocator allocator;
  VkPhysicalDevice physicalDevice;
  VkSurfaceKHR surface;
  ivec2 windowSize;

  // constant fields set once before the first recreateSwapchain() call;
  // .oldSwapchain and .imageExtent are overwritten by recreateSwapchain()
  // itself on every call, including the first.
  VkSwapchainCreateInfoKHR swapchainCI;
  VkSwapchainKHR swapchain;
  uint32_t imageCount;
  VkImage *swapchainImages;
  VkImageView *swapchainImageViews;

  VkSemaphoreCreateInfo semaphoreCI;
  VkSemaphore *renderCompleteSemaphores;

  // constant fields set once; .extent is overwritten by recreateSwapchain()
  // on every call, including the first.
  VkImageCreateInfo depthImageCI;
  VkImage depthImage;
  VmaAllocation depthImageAllocation;
  VkImageView depthImageView;
} SwapchainState;

extern SwapchainState sc;

// (Re)builds everything in `sc` above sized to `sc.windowSize`. Safe to call
// for the very first creation too -- see the definition in init.c. Called
// from init.c once up front, and from loop.c on every resize / out-of-date
// swapchain, so it can't be static anymore now that it crosses a TU boundary.
void recreateSwapchain(void);

// Everything created once during init.c that loop.c needs on every frame.
// This is the base-cube prototype's cross-phase state, promoted out of
// main()'s locals so init.c and loop.c can each see it. Future additions
// (chunk streaming, mesh buffers, etc.) get their own state living in their
// own files -- this struct is not the place to keep growing.
typedef struct {
  VkDevice device;
  VkQueue queue;
  SDL_Window *window;
  VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];
  ShaderDataBuffer shaderDataBuffers[MAX_FRAMES_IN_FLIGHT];
  VkFence fences[MAX_FRAMES_IN_FLIGHT];
  VkSemaphore imageAcquiredSemaphores[MAX_FRAMES_IN_FLIGHT];
  VkBuffer vBuffer;
  VkDeviceSize vBufSize;
  VkDeviceSize indexCount;
  VkDescriptorSet descriptorSetTex;
  VkPipelineLayout pipelineLayout;
  VkPipeline pipeline;
} AppState;

extern AppState app;

// --- past base cube: add new cross-phase state above this line, in its own
// struct, not by growing AppState indefinitely.

int peranti_init(int argc, char *argv[]);
int peranti_mainloop(void);
int peranti_shutdown(void);

#endif
