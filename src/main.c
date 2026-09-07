#include <SDL3/SDL_scancode.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define VK_NO_PROTOTYPES
#include "types.h"
#include "vk_mem_alloc.h"
#include "volk/volk.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
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

const ShaderData ShaderDataDefault = {
    .lightPos = {0.0f, -10.0f, 10.0f, 0.0f},
    .selected = 1,
};

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

bool updateSwapchain = {false};

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

static VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *userData) {
  fprintf(stderr, "[validation] %s\n", data->pMessage);
  return VK_FALSE; // don't abort the call that triggered this
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

static SwapchainState sc = {0};

// (Re)builds everything in `sc` above sized to `sc.windowSize`. Safe to call
// for the very first creation too: every "old" handle in `sc` is zero on the
// first call (global, zero-initialized), and destroying a VK_NULL_HANDLE (or
// free()-ing NULL) is a defined no-op -- so the "destroy old, then create
// new" sequence below just skips the "destroy old" half the first time.
static void recreateSwapchain(void) {
  chkvk(vkDeviceWaitIdle(sc.device));

  VkSurfaceCapabilitiesKHR surfaceCaps = {0};
  chkvk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(sc.physicalDevice,
                                                  sc.surface, &surfaceCaps));

  uint32_t oldImageCount =
      sc.imageCount; // must save this before sc.imageCount gets overwritten

  // currentExtent is authoritative when it's a real value (e.g. X11); the
  // 0xFFFFFFFF sentinel means "you choose" (e.g. Wayland), so fall back to
  // the window size only in that case rather than always overriding it.
  VkExtent2D extent = surfaceCaps.currentExtent;
  if (extent.width == 0xFFFFFFFF) {
    extent.width = (uint32_t)sc.windowSize[0];
    extent.height = (uint32_t)sc.windowSize[1];
  }

  sc.swapchainCI.oldSwapchain = sc.swapchain;
  sc.swapchainCI.imageExtent = extent;
  chkvk(vkCreateSwapchainKHR(sc.device, &sc.swapchainCI, NULL, &sc.swapchain));

  for (uint32_t i = 0; i < oldImageCount; i++) {
    vkDestroyImageView(sc.device, sc.swapchainImageViews[i], NULL);
  }

  chkvk(vkGetSwapchainImagesKHR(sc.device, sc.swapchain, &sc.imageCount, NULL));

  free(sc.swapchainImages);
  sc.swapchainImages = (VkImage *)malloc(sc.imageCount * sizeof(VkImage));
  chkvk(vkGetSwapchainImagesKHR(sc.device, sc.swapchain, &sc.imageCount,
                                sc.swapchainImages));

  free(sc.swapchainImageViews);
  sc.swapchainImageViews =
      (VkImageView *)malloc(sc.imageCount * sizeof(VkImageView));
  for (uint32_t i = 0; i < sc.imageCount; i++) {
    VkImageViewCreateInfo viewCI = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = sc.swapchainImages[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = sc.swapchainCI.imageFormat,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1}};
    chkvk(vkCreateImageView(sc.device, &viewCI, NULL, &sc.swapchainImageViews[i]));
  }

  for (uint32_t i = 0; i < oldImageCount; i++) {
    vkDestroySemaphore(sc.device, sc.renderCompleteSemaphores[i], NULL);
  }
  free(sc.renderCompleteSemaphores);
  sc.renderCompleteSemaphores =
      (VkSemaphore *)malloc(sc.imageCount * sizeof(VkSemaphore));
  for (uint32_t i = 0; i < sc.imageCount; i++) {
    chkvk(vkCreateSemaphore(sc.device, &sc.semaphoreCI, NULL,
                            &sc.renderCompleteSemaphores[i]));
  }

  vkDestroySwapchainKHR(sc.device, sc.swapchainCI.oldSwapchain, NULL);

  vmaDestroyImage(sc.allocator, sc.depthImage, sc.depthImageAllocation);
  vkDestroyImageView(sc.device, sc.depthImageView, NULL);

  sc.depthImageCI.extent = (VkExtent3D){
      .width = (uint32_t)sc.windowSize[0],
      .height = (uint32_t)sc.windowSize[1],
      .depth = 1,
  };
  VmaAllocationCreateInfo depthAllocCI = {
      .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO};
  chkvk(vmaCreateImage(sc.allocator, &sc.depthImageCI, &depthAllocCI,
                       &sc.depthImage, &sc.depthImageAllocation, NULL));

  VkImageViewCreateInfo depthViewCI = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = sc.depthImage,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = sc.depthImageCI.format,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                                         VK_IMAGE_ASPECT_STENCIL_BIT,
                           .levelCount = 1,
                           .layerCount = 1}};
  chkvk(vkCreateImageView(sc.device, &depthViewCI, NULL, &sc.depthImageView));
}

int main(int argc, char *argv[]) {
  /*
   *  INIT INIT
   *
   *
   *  INIT INIT
   */
  chk(SDL_Init(SDL_INIT_VIDEO));
  chk(SDL_Vulkan_LoadLibrary(NULL));
  chkvk(volkInitialize()); // see bug #2 below re: chk()

  // instance

  VkApplicationInfo appInfo = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "How to Vulkan", // useful for identifying app
      .apiVersion = VK_API_VERSION_1_3 // useful to show what version your app
                                       // needs of vulkan, here, 1.3
  };

  uint32_t instanceExtensionsCount = 0;
  char const *const *sdlExtensions =
      SDL_Vulkan_GetInstanceExtensions(&instanceExtensionsCount);

  const char *instanceExtensions[16]; // plenty of headroom
  for (uint32_t i = 0; i < instanceExtensionsCount; i++) {
    instanceExtensions[i] = sdlExtensions[i];
  }
  instanceExtensions[instanceExtensionsCount++] =
      VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

  const char *const instanceLayers[] = {"VK_LAYER_KHRONOS_validation"};

  VkInstanceCreateInfo instanceCI = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &appInfo,
      .enabledLayerCount = 1,
      .ppEnabledLayerNames = instanceLayers,
      .enabledExtensionCount = instanceExtensionsCount,
      .ppEnabledExtensionNames = instanceExtensions,
  };

  VkInstance instance = {0};
  chkvk(vkCreateInstance(&instanceCI, NULL, &instance));
  volkLoadInstance(instance);

  VkDebugUtilsMessengerCreateInfoEXT debugMessengerCI = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugCallback,
  };
  VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
  chkvk(vkCreateDebugUtilsMessengerEXT(instance, &debugMessengerCI, NULL,
                                       &debugMessenger));

  // device

  uint32_t deviceCount = {0};
  chkvk(vkEnumeratePhysicalDevices(instance, &deviceCount, NULL));
  VkPhysicalDevice *devices = (VkPhysicalDevice *)malloc(
      deviceCount * sizeof(VkPhysicalDevice)); // free(devices) when done!!!
  chkvk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices));

  uint32_t deviceIndex = {0};
  if (argc > 1) {
    deviceIndex = (uint32_t)atoi(argv[1]);
    assert(deviceIndex < deviceCount);
  }

  VkPhysicalDeviceProperties2 deviceProperties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  vkGetPhysicalDeviceProperties2(devices[deviceIndex], &deviceProperties);
  printf("Selected device: %s\n", deviceProperties.properties.deviceName);

  // queues

  uint32_t queueFamilyCount = {0};
  vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex],
                                           &queueFamilyCount, NULL);
  VkQueueFamilyProperties *queueFamilies = (VkQueueFamilyProperties *)malloc(
      queueFamilyCount *
      sizeof(VkQueueFamilyProperties)); // free(queueFamilies) when done!
  vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex],
                                           &queueFamilyCount, queueFamilies);
  uint32_t queueFamily = {0};
  for (uint32_t i = 0; i < queueFamilyCount; i++) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queueFamily = i;
      break;
    }
  }

  chk(SDL_Vulkan_GetPresentationSupport(instance, devices[deviceIndex],
                                        queueFamily));

  // logical device

  const float qfpriorities = {1.0f};
  VkDeviceQueueCreateInfo queueCI = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = queueFamily,
      .queueCount = 1,
      .pQueuePriorities = &qfpriorities};

  const char *const deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  uint32_t deviceExtensionCount = 1;

  VkPhysicalDeviceVulkan12Features enabledVk12Features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .descriptorIndexing = true,
      .shaderSampledImageArrayNonUniformIndexing = true,
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true,
      .bufferDeviceAddress = true};
  VkPhysicalDeviceVulkan13Features enabledVk13Features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &enabledVk12Features,
      .synchronization2 = true,
      .dynamicRendering = true,
  };
  VkPhysicalDeviceFeatures enabledVk10Features = {.samplerAnisotropy = VK_TRUE};

  VkDeviceCreateInfo deviceCI = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                 .pNext = &enabledVk13Features,
                                 .queueCreateInfoCount = 1,
                                 .pQueueCreateInfos = &queueCI,
                                 .enabledExtensionCount = deviceExtensionCount,
                                 .ppEnabledExtensionNames = deviceExtensions,
                                 .pEnabledFeatures = &enabledVk10Features};
  VkDevice device = {VK_NULL_HANDLE};
  chkvk(vkCreateDevice(devices[deviceIndex], &deviceCI, NULL, &device));
  volkLoadDevice(device);

  VkQueue queue = {VK_NULL_HANDLE};
  vkGetDeviceQueue(device, queueFamily, 0, &queue);

  // VMA setup

  VmaVulkanFunctions vkFunctions = {.vkGetInstanceProcAddr =
                                        vkGetInstanceProcAddr,
                                    .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
                                    .vkCreateImage = vkCreateImage};
  VmaAllocatorCreateInfo allocatorCI = {
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = devices[deviceIndex],
      .device = device,
      .pVulkanFunctions = &vkFunctions,
      .instance = instance};
  VmaAllocator allocator = {VK_NULL_HANDLE};
  chkvk(vmaCreateAllocator(&allocatorCI, &allocator));

  // window and surface (finally)

  SDL_Window *window = SDL_CreateWindow(
      "How to Vulkan", 1280u, 720u, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  // Lock the cursor to the window and hide it -- standard for a first-person
  // game. SDL keeps reporting relative motion (event.motion.xrel/yrel) even
  // when the (now invisible) cursor would otherwise hit a screen edge.
  chk(SDL_SetWindowRelativeMouseMode(window, true));

  sc.device = device;
  sc.allocator = allocator;
  sc.physicalDevice = devices[deviceIndex];
  chk(SDL_Vulkan_CreateSurface(window, instance, NULL, &sc.surface));

  // swapchain, so close to over with this tringle tutorial
  // (only the constant fields go here -- .imageExtent and .oldSwapchain are
  // set by recreateSwapchain() itself, on this first call and every one after)

  sc.swapchainCI = (VkSwapchainCreateInfoKHR){
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = sc.surface,
      .imageFormat = VK_FORMAT_B8G8R8A8_SRGB,
      .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
  };
  {
    VkSurfaceCapabilitiesKHR surfaceCaps = {0};
    chkvk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(sc.physicalDevice,
                                                    sc.surface, &surfaceCaps));
    sc.swapchainCI.minImageCount = surfaceCaps.minImageCount;
  }

  sc.semaphoreCI =
      (VkSemaphoreCreateInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

  // depth

  VkFormat depthFormatList[] = {VK_FORMAT_D32_SFLOAT_S8_UINT,
                                VK_FORMAT_D24_UNORM_S8_UINT};
  uint32_t depthFormatCount =
      sizeof(depthFormatList) / sizeof(depthFormatList[0]);
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;

  for (uint32_t i = 0; i < depthFormatCount; i++) {
    VkFormat format = depthFormatList[i];

    VkFormatProperties2 formatProperties = {
        .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = NULL};

    vkGetPhysicalDeviceFormatProperties2(devices[deviceIndex], format,
                                         &formatProperties);

    if (formatProperties.formatProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      depthFormat = format;
      break;
    }
  }

  sc.depthImageCI = (VkImageCreateInfo){
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = depthFormat,
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  // first-time build: every "old" handle in `sc` is still zero, so this just
  // creates the swapchain/views/semaphores/depth image fresh -- the exact
  // same call used for every later resize.
  SDL_GetWindowSizeInPixels(window, &sc.windowSize[0], &sc.windowSize[1]);
  recreateSwapchain();

  // loading meshes
  static const Vertex vertices[] = {
      // +Z (front)
      {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
      {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
      {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
      {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
      // -Z (back)
      {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
      {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
      {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
      {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
      // +X (right)
      {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
      {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
      {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
      {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
      // -X (left)
      {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
      {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
      {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
      {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
      // +Y (top)
      {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
      {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
      {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
      {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
      // -Y (bottom)
      {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
      {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
      {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
      {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
  };
  static const uint16_t indices[] = {
      0,  1,  2,  2,  3,  0,  // +Z
      4,  5,  6,  6,  7,  4,  // -Z
      8,  9,  10, 10, 11, 8,  // +X
      12, 13, 14, 14, 15, 12, // -X
      16, 17, 18, 18, 19, 16, // +Y
      20, 21, 22, 22, 23, 20, // -Y
  };

  const VkDeviceSize indexCount = {sizeof(indices) / sizeof(uint16_t)};

  static const VkVertexInputBindingDescription bindingDesc = {
      .binding = 0,
      .stride = sizeof(Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
  };

  static const VkVertexInputAttributeDescription attributeDescs[] = {
      {.location = 0,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Vertex, pos)},
      {.location = 1,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Vertex, normal)},
      {.location = 2,
       .binding = 0,
       .format = VK_FORMAT_R32G32_SFLOAT,
       .offset = offsetof(Vertex, uv)},
  };

  VkDeviceSize vBufSize = {sizeof(vertices)};
  VkDeviceSize iBufSize = {sizeof(indices)};
  VkBufferCreateInfo bufferCI = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                 .size = vBufSize + iBufSize,
                                 .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT};

  VmaAllocationCreateInfo vBufferAllocCI = {
      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO};
  VmaAllocation vBufferAllocation = {VK_NULL_HANDLE};
  VkBuffer vBuffer = {VK_NULL_HANDLE};
  VmaAllocationInfo vBufferAllocInfo = {0};
  chkvk(vmaCreateBuffer(allocator, &bufferCI, &vBufferAllocCI, &vBuffer,
                        &vBufferAllocation, &vBufferAllocInfo));

  memcpy(vBufferAllocInfo.pMappedData, vertices, vBufSize);
  memcpy(((char *)vBufferAllocInfo.pMappedData) + vBufSize, indices, iBufSize);

  ShaderDataBuffer shaderDataBuffers[MAX_FRAMES_IN_FLIGHT] = {0};
  VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT] = {0};

  // shader data buffers

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    VkBufferCreateInfo uBufferCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(ShaderData),
        .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
    VmaAllocationCreateInfo uBufferAllocCI = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO};
    chkvk(vmaCreateBuffer(allocator, &uBufferCI, &uBufferAllocCI,
                          &shaderDataBuffers[i].buffer,
                          &shaderDataBuffers[i].allocation,
                          &shaderDataBuffers[i].allocationInfo));
    VkBufferDeviceAddressInfo uBufferBdaInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = shaderDataBuffers[i].buffer};
    shaderDataBuffers[i].deviceAddress =
        vkGetBufferDeviceAddress(device, &uBufferBdaInfo);
  }

  // synchronization objects: fences + per-frame-in-flight semaphores.
  // (the per-swap-image render-complete semaphores are already built inside
  // `sc` by recreateSwapchain() above, since they're sized to imageCount)

  VkFenceCreateInfo fenceCI = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                               .flags = VK_FENCE_CREATE_SIGNALED_BIT};

  VkFence fences[MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
  VkSemaphore imageAcquiredSemaphores[MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    chkvk(vkCreateFence(device, &fenceCI, NULL, &fences[i]));
    chkvk(vkCreateSemaphore(device, &sc.semaphoreCI, NULL,
                            &imageAcquiredSemaphores[i]));
  }

  // command buffers

  VkCommandPoolCreateInfo commandPoolCI = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queueFamily};
  VkCommandPool commandPool = {VK_NULL_HANDLE};
  chkvk(vkCreateCommandPool(device, &commandPoolCI, NULL, &commandPool));

  VkCommandBufferAllocateInfo cbAllocCI = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = commandPool,
      .commandBufferCount = MAX_FRAMES_IN_FLIGHT};
  chkvk(vkAllocateCommandBuffers(device, &cbAllocCI, commandBuffers));

  // loading textures

  // TODO: KTX rendering

  VkDescriptorSetLayoutBinding textureBinding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1, // upper bound for this binding; actual live count
                            // set at allocation time
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  VkDescriptorBindingFlags textureBindingFlags =
      VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
  VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCI = {
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = 1,
      .pBindingFlags = &textureBindingFlags,
  };
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &bindingFlagsCI,
      .bindingCount = 1,
      .pBindings = &textureBinding,
  };
  VkDescriptorSetLayout descriptorSetLayoutTex = {VK_NULL_HANDLE};
  chkvk(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, NULL,
                                    &descriptorSetLayoutTex));

  VkDescriptorPoolSize texPoolSize = {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
  };
  VkDescriptorPoolCreateInfo descriptorPoolCI = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &texPoolSize,
  };
  VkDescriptorPool descriptorPool = {VK_NULL_HANDLE};
  chkvk(
      vkCreateDescriptorPool(device, &descriptorPoolCI, NULL, &descriptorPool));

  uint32_t textureCount = 1; // one live descriptor in the variable-count tail
  VkDescriptorSetVariableDescriptorCountAllocateInfo variableCountAI = {
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
      .descriptorSetCount = 1,
      .pDescriptorCounts = &textureCount,
  };
  VkDescriptorSetAllocateInfo descriptorSetAI = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = &variableCountAI,
      .descriptorPool = descriptorPool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptorSetLayoutTex,
  };
  VkDescriptorSet descriptorSetTex = {VK_NULL_HANDLE};
  chkvk(vkAllocateDescriptorSets(device, &descriptorSetAI, &descriptorSetTex));

  const uint8_t whitePixel[4] = {25, 255, 25, 255};
  VkBufferCreateInfo texStagingBufferCI = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sizeof(whitePixel),
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
  };
  VmaAllocationCreateInfo texStagingAllocCI = {
      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO,
  };
  VkBuffer texStagingBuffer;
  VmaAllocation texStagingAllocation;
  VmaAllocationInfo texStagingAllocInfo = {0};
  chkvk(vmaCreateBuffer(allocator, &texStagingBufferCI, &texStagingAllocCI,
                        &texStagingBuffer, &texStagingAllocation,
                        &texStagingAllocInfo));
  memcpy(texStagingAllocInfo.pMappedData, whitePixel, sizeof(whitePixel));

  const VkFormat texFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkImageCreateInfo texImageCI = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = texFormat,
      .extent = {1, 1, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VkImage texImage;
  VmaAllocation texImageAllocation;
  chkvk(
      vmaCreateImage(allocator, &texImageCI,
                     &(VmaAllocationCreateInfo){.usage = VMA_MEMORY_USAGE_AUTO},
                     &texImage, &texImageAllocation, NULL));

  VkCommandBuffer texCb;
  chkvk(vkAllocateCommandBuffers(
      device,
      &(VkCommandBufferAllocateInfo){
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .commandPool = commandPool,
          .commandBufferCount = 1,
      },
      &texCb));

  chkvk(vkBeginCommandBuffer(
      texCb, &(VkCommandBufferBeginInfo){
                 .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
             }));

  // undefined -> transfer dst
  VkImageMemoryBarrier2 texToTransferDst = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .image = texImage,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
  };
  vkCmdPipelineBarrier2(texCb, &(VkDependencyInfo){
                                   .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                   .imageMemoryBarrierCount = 1,
                                   .pImageMemoryBarriers = &texToTransferDst,
                               });

  // copy staging -> image
  vkCmdCopyBufferToImage(
      texCb, texStagingBuffer, texImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &(VkBufferImageCopy){
          .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
          .imageExtent = {1, 1, 1}});

  // transfer dst -> shader read-only
  VkImageMemoryBarrier2 texToShaderRead = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .image = texImage,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
  };
  vkCmdPipelineBarrier2(texCb, &(VkDependencyInfo){
                                   .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                   .imageMemoryBarrierCount = 1,
                                   .pImageMemoryBarriers = &texToShaderRead,
                               });

  chkvk(vkEndCommandBuffer(texCb));
  chkvk(vkQueueSubmit2(
      queue, 1,
      &(VkSubmitInfo2){
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
          .commandBufferInfoCount = 1,
          .pCommandBufferInfos =
              &(VkCommandBufferSubmitInfo){
                  .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                  .commandBuffer = texCb},
      },
      VK_NULL_HANDLE));
  chkvk(vkQueueWaitIdle(queue));

  vkFreeCommandBuffers(device, commandPool, 1, &texCb);
  vmaDestroyBuffer(allocator, texStagingBuffer, texStagingAllocation);

  VkImageView texImageView;
  chkvk(vkCreateImageView(
      device,
      &(VkImageViewCreateInfo){
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = texImage,
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = texFormat,
          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      },
      NULL, &texImageView));

  VkSampler texSampler;
  chkvk(vkCreateSampler(device,
                        &(VkSamplerCreateInfo){
                            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                            .magFilter = VK_FILTER_NEAREST,
                            .minFilter = VK_FILTER_NEAREST,
                            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            .maxLod = VK_LOD_CLAMP_NONE,
                        },
                        NULL, &texSampler));

  VkDescriptorImageInfo texDescriptorImageInfo = {
      .sampler = texSampler,
      .imageView = texImageView,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  vkUpdateDescriptorSets(
      device, 1,
      &(VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = descriptorSetTex,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .pImageInfo = &texDescriptorImageInfo,
      },
      0, NULL);

  // shaders

  static const uint32_t vertShaderSpv[] =
#include "shaders/shader.vert.spv.inc"
  ;

  static const uint32_t fragShaderSpv[] =
#include "shaders/shader.frag.spv.inc"
  ;

  VkShaderModuleCreateInfo vertModuleCI = {
      // no use in vulkan 1.4, so in a few years, remove this
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(vertShaderSpv),
      .pCode = vertShaderSpv,
  };
  VkShaderModule vertModule = VK_NULL_HANDLE;

  VkShaderModuleCreateInfo fragModuleCI = {
      // same deal, gone in 1.4-only code
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(fragShaderSpv),
      .pCode = fragShaderSpv,
  };
  VkShaderModule fragModule = VK_NULL_HANDLE;
  chkvk(vkCreateShaderModule(device, &fragModuleCI, NULL, &fragModule));

  chkvk(vkCreateShaderModule(device, &vertModuleCI, NULL, &vertModule));

  // yay yippee yippee yay yay pipeline

  VkPushConstantRange pushConstantRange = {.stageFlags =
                                               VK_SHADER_STAGE_VERTEX_BIT,
                                           .size = sizeof(VkDeviceAddress)};
  VkPipelineLayoutCreateInfo pipelineLayoutCI = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptorSetLayoutTex,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstantRange};
  VkPipelineLayout pipelineLayout = {VK_NULL_HANDLE};
  chkvk(
      vkCreatePipelineLayout(device, &pipelineLayoutCI, NULL, &pipelineLayout));

  VkVertexInputBindingDescription vertexBinding = {
      .binding = 0,
      .stride = sizeof(Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

  VkVertexInputAttributeDescription vertexAttributes[] = {
      {.location = 0,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = 0},
      {.location = 1,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Vertex, normal)},
      {.location = 2,
       .binding = 0,
       .format = VK_FORMAT_R32G32_SFLOAT,
       .offset = offsetof(Vertex, uv)}};

  VkPipelineVertexInputStateCreateInfo vertexInputState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = (uint32_t)sizeof(vertexAttributes) / (uint32_t)sizeof(vertexAttributes[0]),
      .pVertexAttributeDescriptions = vertexAttributes,
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

  VkPipelineShaderStageCreateInfo shaderStages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .pNext = NULL, /* Good practice in C to explicitly clear or rely on
                         zero-init */
       .flags = 0,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vertModule,
       .pName = "main",
       .pSpecializationInfo = NULL},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .pNext = NULL,
       .flags = 0,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fragModule,
       .pName = "main",
       .pSpecializationInfo = NULL}};

  VkPipelineViewportStateCreateInfo viewportState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1};
  VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                    VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount =
          sizeof(dynamicStates) / sizeof(VkDynamicState),
      .pDynamicStates = dynamicStates};

  VkPipelineDepthStencilStateCreateInfo depthStencilState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};

  VkPipelineRenderingCreateInfo renderingCI = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &sc.swapchainCI.imageFormat,
      .depthAttachmentFormat = depthFormat};

  VkPipelineColorBlendAttachmentState blendAttachment = {.colorWriteMask = 0xF};
  VkPipelineColorBlendStateCreateInfo colorBlendState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blendAttachment};
  VkPipelineRasterizationStateCreateInfo rasterizationState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .lineWidth = 1.0f};
  VkPipelineMultisampleStateCreateInfo multisampleState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

  VkGraphicsPipelineCreateInfo pipelineCI = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &renderingCI,
      .stageCount = 2,
      .pStages = shaderStages,
      .pVertexInputState = &vertexInputState,
      .pInputAssemblyState = &inputAssemblyState,
      .pViewportState = &viewportState,
      .pRasterizationState = &rasterizationState,
      .pMultisampleState = &multisampleState,
      .pDepthStencilState = &depthStencilState,
      .pColorBlendState = &colorBlendState,
      .pDynamicState = &dynamicState,
      .layout = pipelineLayout};
  VkPipeline pipeline = {VK_NULL_HANDLE};
  chkvk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, NULL,
                                  &pipeline));

  // finally, a loop, the main loop at least

  uint64_t lastTime = {SDL_GetTicks()};
  bool quit = {false};
  bool mouseCaptured = {true}; // matches the initial SDL_SetWindowRelativeMouseMode(window, true) above
  uint32_t frameIndex = {0};
  uint32_t imageIndex = {0};
  ShaderData shaderData = {0};
  vec3 camPos = {0.0f, 0.0f, -6.0f};
  vec3 camFront = {0.0f, 0.0f, 1.0f}; // Looking towards +Z
vec3 camUp = {0.0f, 1.0f, 0.0f};
float yaw = 0.0f;
float pitch = 0.0f;
  vec3 objectRotations[3] = {0};
  int i = 0;
  while (!quit) {

float elapsedTime = {(SDL_GetTicks() - lastTime) / 1000.0f};
    lastTime = SDL_GetTicks();
    float fps = 1/elapsedTime;
    i+=(int)(elapsedTime * 1000);
    if(i == 60*1000) {
      printf("fps: %f\n", fps);
      i = 0;
    }

    // WASD Camera Movement
const bool *state = SDL_GetKeyboardState(NULL);
float cameraSpeed = 5.0f * elapsedTime;

// Calculate the right vector (Up x Front)
vec3 camRight;
camRight[0] = camUp[1] * camFront[2] - camUp[2] * camFront[1];
camRight[1] = camUp[2] * camFront[0] - camUp[0] * camFront[2];
camRight[2] = camUp[0] * camFront[1] - camUp[1] * camFront[0];
glm_normalize(camRight);

if (state[SDL_SCANCODE_W]) {
    camPos[0] += camFront[0] * cameraSpeed;
    // camPos[1] += camFront[1] * cameraSpeed; // not here because space/shift
    camPos[2] += camFront[2] * cameraSpeed;
}
if (state[SDL_SCANCODE_S]) {
    camPos[0] -= camFront[0] * cameraSpeed;
    // camPos[1] -= camFront[1] * cameraSpeed;
    camPos[2] -= camFront[2] * cameraSpeed;
}
if (state[SDL_SCANCODE_A]) {
    camPos[0] += camRight[0] * cameraSpeed;
    // camPos[1] -= camRight[1] * cameraSpeed;
    camPos[2] += camRight[2] * cameraSpeed;
}
if (state[SDL_SCANCODE_D]) {
    camPos[0] -= camRight[0] * cameraSpeed;
    // camPos[1] += camRight[1] * cameraSpeed;
    camPos[2] -= camRight[2] * cameraSpeed;
}
if (state[SDL_SCANCODE_SPACE]) {
    camPos[1] -= cameraSpeed;
}
if(state[SDL_SCANCODE_RSHIFT] || state[SDL_SCANCODE_LSHIFT]) {
    camPos[1] += cameraSpeed;
}

  // redo windowsize per frame
  SDL_GetWindowSizeInPixels(window, &sc.windowSize[0], &sc.windowSize[1]);

  if (sc.windowSize[0] == 0 || sc.windowSize[1] == 0) {
    SDL_PollEvent(&(SDL_Event){0}); // Simple event poll
    SDL_Delay(10);
    continue;
  }

    // Wait on fence
    chkvk(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));

    // Acquire next image
    VkResult acquireResult = vkAcquireNextImageKHR(
    device, sc.swapchain, UINT64_MAX,
    imageAcquiredSemaphores[frameIndex],
    VK_NULL_HANDLE, &imageIndex);

if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
    updateSwapchain = false; // recreateSwapchain() below already handles it
    recreateSwapchain(); // do it right here -- `continue` skips the block further down otherwise
    continue;
} else if (acquireResult != VK_SUCCESS) {
    chkvk(acquireResult);
}
    // update fence AFTER!
    chkvk(vkResetFences(device, 1, &fences[frameIndex]));
    // Update shader data
    glm_perspective(glm_rad(45.0f), (float)sc.windowSize[0] / (float)sc.windowSize[1],
                    0.1f, 32.0f, shaderData.projection);
    // Replace: glm_translate_make(shaderData.view, camPos);
vec3 camTarget;
glm_vec3_add(camPos, camFront, camTarget);
glm_lookat(camPos, camTarget, camUp, shaderData.view);
    for (int i = 0; i < 3; i++) {
      vec3 instancePos = {(float)0.0f, 0.0f, 0.0f};
      mat4 translationMat, rotationMat;
      glm_translate_make(translationMat, instancePos);
      glm_euler_xyz(objectRotations[i], rotationMat);
      glm_mat4_mul(translationMat, rotationMat, shaderData.model[i]);
    }
    memcpy(shaderDataBuffers[frameIndex].allocationInfo.pMappedData,
           &shaderData, sizeof(ShaderData));
    // Record command buffer
    VkCommandBuffer cb = commandBuffers[frameIndex];
    chkvk(vkResetCommandBuffer(cb, 0));

    VkCommandBufferBeginInfo cbBI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    chkvk(vkBeginCommandBuffer(cb, &cbBI));

    VkImageMemoryBarrier2 outputBarriers[2] = {
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, // FIXED: Must be TOP_OF_PIPE for UNDEFINED
        .srcAccessMask = 0, // FIXED: Must be 0 for UNDEFINED
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image = sc.swapchainImages[imageIndex],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
    },
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, // FIXED: Must be TOP_OF_PIPE for UNDEFINED
        .srcAccessMask = 0, // FIXED: Must be 0 for UNDEFINED
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, // FIXED: Added READ bit
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image = sc.depthImage,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1}
    }
};
    VkDependencyInfo barrierDependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = outputBarriers};
    vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);

    VkRenderingAttachmentInfo colorAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = sc.swapchainImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {0.0f, 0.0f, 0.2f, 1.0f}}};
    VkRenderingAttachmentInfo depthAttachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = sc.depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {1.0f, 0}}};

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = {.width = (uint32_t)sc.windowSize[0],
                                  .height = (uint32_t)sc.windowSize[1]}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo};
    vkCmdBeginRendering(cb, &renderingInfo);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // viewport/scissor are dynamic state on this pipeline -- must be set every
    // time this command buffer is (re-)recorded, or the driver rasterizes
    // against an undefined/zero-sized viewport and nothing shows up on screen.
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)sc.windowSize[0],
        .height = (float)sc.windowSize[1],
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = {(uint32_t)sc.windowSize[0], (uint32_t)sc.windowSize[1]},
    };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    VkDeviceSize vOffset = {0};
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 1, &descriptorSetTex, 0, NULL);
    vkCmdBindVertexBuffers(cb, 0, 1, &vBuffer, &vOffset);
    vkCmdBindIndexBuffer(cb, vBuffer, vBufSize, VK_INDEX_TYPE_UINT16);
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(VkDeviceAddress),
                       &shaderDataBuffers[frameIndex].deviceAddress);

    vkCmdDrawIndexed(cb, indexCount, 1, 0, 0, 0); // drawing, finally
    vkCmdEndRendering(cb);

    VkImageMemoryBarrier2 barrierPresent = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_2_NONE_KHR, // FIXED: Presentation engine doesn't use color attachment stage
    .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT, // FIXED: Presentation engine MUST read the memory
    .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    .image = sc.swapchainImages[imageIndex],
    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
};
    VkDependencyInfo barrierPresentDependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrierPresent};
    vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);
    vkEndCommandBuffer(cb);
    // Submit command buffer
    VkSemaphoreSubmitInfo waitSemaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = imageAcquiredSemaphores[frameIndex],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkCommandBufferSubmitInfo commandBufferSubmitInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cb};
    VkSemaphoreSubmitInfo signalSemaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = sc.renderCompleteSemaphores[imageIndex],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSubmitInfo2 submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitSemaphoreInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferSubmitInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalSemaphoreInfo,
    };
    chkvk(vkQueueSubmit2(queue, 1, &submitInfo, fences[frameIndex]));
    frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    // Present image
    VkPresentInfoKHR presentInfo = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                    .waitSemaphoreCount = 1,
                                    .pWaitSemaphores =
                                        &sc.renderCompleteSemaphores[imageIndex],
                                    .swapchainCount = 1,
                                    .pSwapchains = &sc.swapchain,
                                    .pImageIndices = &imageIndex};
    VkResult presentResult = vkQueuePresentKHR(queue, &presentInfo);

if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
    updateSwapchain = true;
} else if (presentResult != VK_SUCCESS) {
    chkvk(presentResult);
}

    // Poll events

    
    for (SDL_Event event; SDL_PollEvent(&event);) {

      // Exit loop if the application is about to close
      if (event.type == SDL_EVENT_QUIT) {
        quit = true;
        break;
      }

      // Rotate the selected object / Look around with mouse
      // Camera look, unconditional now that the cursor is locked to the window
      if (event.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured) {
        yaw -= event.motion.xrel * 0.003f;
        pitch += event.motion.yrel * 0.003f; // Invert Y axis

        // Constrain pitch to avoid camera flipping upside down
        if (pitch > 1.55f) pitch = 1.55f;
        if (pitch < -1.55f) pitch = -1.55f;

        // Recalculate camFront based on spherical coordinates
        camFront[0] = sinf(yaw);
        camFront[1] = sinf(pitch);
        camFront[2] = cosf(yaw);
        glm_normalize(camFront);
      }

// Zooming with the mouse wheel (now moves along the camera's forward vector)
if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    float zoomSpeed = 10.0f;
    camPos[0] += camFront[0] * (float)event.wheel.y * zoomSpeed;
    camPos[1] += camFront[1] * (float)event.wheel.y * zoomSpeed;
    camPos[2] += camFront[2] * (float)event.wheel.y * zoomSpeed;
}

      // Select active model instance
      if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS) {
          shaderData.selected =
              (shaderData.selected < 2) ? shaderData.selected + 1 : 0;
        }
        if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
          shaderData.selected =
              (shaderData.selected > 0) ? shaderData.selected - 1 : 2;
        }
        // Esc frees the cursor -- lets you reach other windows/menus
        if (event.key.key == SDLK_ESCAPE && mouseCaptured) {
          chk(SDL_SetWindowRelativeMouseMode(window, false));
          mouseCaptured = false;
        }
      }

      // Click back inside the window to recapture the cursor
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !mouseCaptured) {
        chk(SDL_SetWindowRelativeMouseMode(window, true));
        mouseCaptured = true;
      }

      // Window resize
      if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        updateSwapchain = true;
      }
    }

    // swapchain update

    if (updateSwapchain) {
      updateSwapchain = false;
      recreateSwapchain();
    }
  }
}
