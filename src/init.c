#include "app.h"
#include "mesh.h"
#include "mesh_legacy.h"
#include "world.h"
#include <SDL3/SDL_scancode.h>
#include <assert.h>
#include <math.h>
#include <string.h>

static VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *userData) {
  fprintf(stderr, "[validation] %s\n", data->pMessage);
  return VK_FALSE; // don't abort the call that triggered this
}

// (Re)builds everything in `sc` sized to `sc.windowSize`. Safe to call for
// the very first creation too: every "old" handle in `sc` is zero on the
// first call (global, zero-initialized), and destroying a VK_NULL_HANDLE (or
// free()-ing NULL) is a defined no-op -- so the "destroy old, then create
// new" sequence below just skips the "destroy old" half the first time.
void recreateSwapchain(void) {
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

int peranti_init(int argc, char *argv[]) {
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
  //
  // --- past base cube: mesh from Phase 1/2's World + binary greedy mesher
  // --- past base cube: two adjacent solid mapblocks instead of one, to
  // actually exercise (visually, not just via mesh.c's unit tests) the
  // mesher's own neighbor-pulling face culling: the shared face between
  // {0,0,0} and {1,0,0} should be invisible, and the two blocks should
  // read as one seamless 32x16x16 box with no crack at the boundary. If
  // there's a visible seam or a doubled-up face at the join, that's
  // occupancy_build's neighbor logic failing in a way the unit tests
  // (which only ever checked bitmask values, never actual rendered
  // geometry) didn't catch.
  //
  // IMPORTANT ordering: both mapblocks are inserted into testWorld before
  // either one is meshed. Meshing block A before block B exists would
  // incorrectly leave block A's +X face exposed (its neighbor wasn't
  // there yet when occupancy_build looked for it), even though block B's
  // own mesh (built afterward) would correctly cull its -X face -- an
  // asymmetric bug that's easy to introduce by interleaving insert/build
  // per block instead of inserting everything up front.
  World testWorld;
  world_init(&testWorld, 8);
  Node testStone = {.content_id = 3, .param1 = 0, .param2 = 0};
  ChunkCoord coordA = {0, 0, 0};
  ChunkCoord coordB = {1, 0, 0};
  world_insert(&testWorld, coordA, mapblock_make_uniform(testStone));
  world_insert(&testWorld, coordB, mapblock_make_uniform(testStone));

  Mesh meshA, meshB;
  mesh_init(&meshA);
  mesh_init(&meshB);
  mesh_build(&testWorld, coordA, &meshA);
  mesh_build(&testWorld, coordB, &meshB);
  // Expected: 5 quads each (20 vertices, 30 indices each), not 6/24/36 --
  // each block's face toward the other got culled. 6/24/36 each here
  // would mean the shared-face culling isn't actually happening.

  Vertex *vertsA, *vertsB;
  uint32_t vertexCountA, vertexCountB;
  uint16_t *idxA, *idxB;
  uint32_t indexCountA, indexCountB;
  mesh_to_legacy_buffers(&meshA, &vertsA, &vertexCountA, &idxA, &indexCountA);
  mesh_to_legacy_buffers(&meshB, &vertsB, &vertexCountB, &idxB, &indexCountB);

  mesh_destroy(&meshA);
  mesh_destroy(&meshB);
  world_destroy(&testWorld);

  // mesh_to_legacy_buffers has no idea what ChunkCoord it built for -- it
  // only ever sees local 0..15 node coordinates (see mesh_legacy.h).
  // Placing block B next to block A in world space, rather than directly
  // on top of it, is this test scene's job: offset every one of block B's
  // vertices by +16 along X, matching coordB being {1,0,0}.
  for (uint32_t i = 0; i < vertexCountB; i++) {
    vertsB[i].pos[0] += 16.0f;
  }

  uint32_t vertexCount = vertexCountA + vertexCountB;
  uint32_t indexCountU32 = indexCountA + indexCountB;
  Vertex *vertices = malloc(sizeof(Vertex) * vertexCount);
  uint16_t *indices = malloc(sizeof(uint16_t) * indexCountU32);
  if (!vertices || !indices) {
    fprintf(stderr, "peranti_init: out of memory combining test mapblocks\n");
    exit(1);
  }
  memcpy(vertices, vertsA, sizeof(Vertex) * vertexCountA);
  memcpy(vertices + vertexCountA, vertsB, sizeof(Vertex) * vertexCountB);
  memcpy(indices, idxA, sizeof(uint16_t) * indexCountA);
  // Block B's indices point into vertsB, which is now appended after
  // vertsA in the combined array -- every one needs to shift by
  // vertexCountA to still point at the right (now-relocated) vertex.
  for (uint32_t i = 0; i < indexCountB; i++) {
    indices[indexCountA + i] = (uint16_t)(idxB[i] + vertexCountA);
  }

  free(vertsA);
  free(vertsB);
  free(idxA);
  free(idxB);

  const VkDeviceSize indexCount = {indexCountU32};

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

  VkDeviceSize vBufSize = {sizeof(Vertex) * vertexCount};
  VkDeviceSize iBufSize = {sizeof(uint16_t) * indexCountU32};
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

  // Already copied into the GPU-visible mapped buffer above; the CPU-side
  // copies are done being useful.
  free(vertices);
  free(indices);

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

  // --- past base cube: promote everything loop.c needs into the shared
  // AppState. Anything not copied here stays a local of peranti_init() and
  // is only ever touched again inside this function (or never again, for
  // one-shot setup temporaries like the staging buffer already freed above).
  app.device = device;
  app.queue = queue;
  app.window = window;
  memcpy(app.commandBuffers, commandBuffers, sizeof(commandBuffers));
  memcpy(app.shaderDataBuffers, shaderDataBuffers, sizeof(shaderDataBuffers));
  memcpy(app.fences, fences, sizeof(fences));
  memcpy(app.imageAcquiredSemaphores, imageAcquiredSemaphores,
         sizeof(imageAcquiredSemaphores));
  app.vBuffer = vBuffer;
  app.vBufSize = vBufSize;
  app.indexCount = indexCount;
  app.descriptorSetTex = descriptorSetTex;
  app.pipelineLayout = pipelineLayout;
  app.pipeline = pipeline;

  return 0;
}
