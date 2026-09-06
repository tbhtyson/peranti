#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "volk/volk.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "vk_mem_alloc.h"
#include "types.h"
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
  .lightPos = { 0.0f, -10.0f, 10.0f, 0.0f },
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

bool updateSwapchain = { false };

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

int main(int argc, char *argv[]) {
  /*
   *  INIT INIT
   *
   *
   *  INIT INIT
   */

  chkvk(volkInitialize()); // see bug #2 below re: chk()

  // instance
  
  VkApplicationInfo appInfo = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = "How to Vulkan", // useful for identifying app
    .apiVersion = VK_API_VERSION_1_3     // useful to show what version your app needs of vulkan, here, 1.3
  };

  uint32_t instanceExtensionsCount = { 0 };
  char const* const* instanceExtensions = { SDL_Vulkan_GetInstanceExtensions(&instanceExtensionsCount) }; // query SDL for platform-specific features
  
  VkInstanceCreateInfo instanceCI = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &appInfo,
    .enabledExtensionCount = instanceExtensionsCount,
    .ppEnabledExtensionNames = instanceExtensions,
  };
  VkInstance instance = { 0 };
  chkvk(vkCreateInstance(&instanceCI, NULL, &instance));
  volkLoadInstance(instance);
  
  // device
  
  uint32_t deviceCount = { 0 };
  chkvk(vkEnumeratePhysicalDevices(instance, &deviceCount, NULL));
  VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(deviceCount * sizeof(VkPhysicalDevice)); // free(devices) when done!!!
  chkvk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices));

  uint32_t deviceIndex = { 0 };
  if (argc > 1) {
    deviceIndex = (uint32_t)atoi(argv[1]);
    assert(deviceIndex < deviceCount);
  }

  VkPhysicalDeviceProperties2 deviceProperties = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
  vkGetPhysicalDeviceProperties2(devices[deviceIndex], &deviceProperties);
  printf("Selected device: %s\n", deviceProperties.properties.deviceName);

  // queues

  uint32_t queueFamilyCount = { 0 };
  vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, NULL);
  VkQueueFamilyProperties* queueFamilies = (VkQueueFamilyProperties*)malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties)); // free(queueFamilies) when done!
  vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, queueFamilies);
  uint32_t queueFamily = { 0 };
  for (uint32_t i = 0; i < queueFamilyCount; i++) {
    if (queueFamilies[i].queueFlags& VK_QUEUE_GRAPHICS_BIT) {
      queueFamily = i;
      break;
    }
  }

  chk(SDL_Vulkan_GetPresentationSupport(instance, devices[deviceIndex], queueFamily));
  
  // logical device

  const float qfpriorities = { 1.0f };
  VkDeviceQueueCreateInfo queueCI = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = queueFamily,
    .queueCount = 1,
    .pQueuePriorities = &qfpriorities
  };

  const char* const deviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
  };
  uint32_t deviceExtensionCount = 1;

  VkPhysicalDeviceVulkan12Features enabledVk12Features = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    .descriptorIndexing = true,
    .shaderSampledImageArrayNonUniformIndexing = true,
    .descriptorBindingVariableDescriptorCount = true,
    .runtimeDescriptorArray = true,
    .bufferDeviceAddress = true
  };
  VkPhysicalDeviceVulkan13Features enabledVk13Features = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    .pNext = &enabledVk12Features,
    .synchronization2 = true,
    .dynamicRendering = true,
  };
  VkPhysicalDeviceFeatures enabledVk10Features = {
    .samplerAnisotropy = VK_TRUE
  };

  VkDeviceCreateInfo deviceCI = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = &enabledVk13Features,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &queueCI,
    .enabledExtensionCount = deviceExtensionCount,
    .ppEnabledExtensionNames = deviceExtensions,
    .pEnabledFeatures = &enabledVk10Features
  };
  VkDevice device = { VK_NULL_HANDLE };
  chkvk(vkCreateDevice(devices[deviceIndex], &deviceCI, NULL, &device));
 
  VkQueue queue = { VK_NULL_HANDLE };
  vkGetDeviceQueue(device, queueFamily, 0, &queue);

  // VMA setup

  VmaVulkanFunctions vkFunctions = {
    .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
    .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    .vkCreateImage = vkCreateImage
  };
  VmaAllocatorCreateInfo allocatorCI = {
    .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, 
    .physicalDevice = devices[deviceIndex],
    .device = device,
    .pVulkanFunctions = &vkFunctions,
    .instance = instance
  };
  VmaAllocator allocator = { VK_NULL_HANDLE };
  chkvk(vmaCreateAllocator(&allocatorCI, &allocator));

  // window and surface (finally)
  
  SDL_Window* window = SDL_CreateWindow("How to Vulkan", 1280u, 720u, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  
  VkSurfaceKHR surface = { VK_NULL_HANDLE };
  chk(SDL_Vulkan_CreateSurface(window, instance, NULL, &surface));

  VkSurfaceCapabilitiesKHR surfaceCaps = { 0 };
  chkvk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCaps)); // surprise tool that will help us later.

  // swapchain, so close to over with this tringle tutorial
  
  ivec2 windowSize; // rember, just array of 2 ints
  SDL_GetWindowSizeInPixels(window, &windowSize[0], &windowSize[1]);
  VkExtent2D swapchainExtent = surfaceCaps.currentExtent; 
  if (surfaceCaps.currentExtent.width == 0xFFFFFFFF) {     
    swapchainExtent.width = (uint32_t)windowSize[0];
    swapchainExtent.height = (uint32_t)windowSize[1];
  }

  const VkFormat imageFormat = { VK_FORMAT_B8G8R8A8_SRGB };
  VkSwapchainCreateInfoKHR swapchainCI = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface,
    .minImageCount = surfaceCaps.minImageCount,
    .imageFormat = imageFormat,
    .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
    .imageExtent = {.width = swapchainExtent.width, .height = swapchainExtent.height },
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = VK_PRESENT_MODE_FIFO_KHR
  };
  VkSwapchainKHR swapchain = { VK_NULL_HANDLE };
  chkvk(vkCreateSwapchainKHR(device, &swapchainCI, NULL, &swapchain));

  uint32_t imageCount = 0;
  chkvk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, NULL));
  VkImage* swapchainImages = (VkImage*)malloc(imageCount * sizeof(VkImage)); // free(swapchainImages) when done!
  if (swapchainImages == NULL) {
    fprintf(stderr, "memory allocation for swapchain images failed :(\n");
  }
  chkvk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages));
  VkImageView* swapchainImageViews = (VkImageView*)malloc(imageCount * sizeof(VkImageView)); // free(swapchainImageViews) when done!
  if (swapchainImageViews == NULL) {
    fprintf(stderr, "memory allocation for swapchain image views failed :(\n");
  }

  //depth

  VkFormat depthFormatList[] = { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
  uint32_t depthFormatCount = sizeof(depthFormatList) / sizeof(depthFormatList[0]);
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;

  for (uint32_t i = 0; i < depthFormatCount; i++) {
    VkFormat format = depthFormatList[i];
    
    VkFormatProperties2 formatProperties = {
        .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        .pNext = NULL
    };
    
    vkGetPhysicalDeviceFormatProperties2(devices[deviceIndex], format, &formatProperties);
    
    if (formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      depthFormat = format;
      break;
    }
  }

  VkImageCreateInfo depthImageCI = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = NULL, // Explicitly setting optional pointers is good practice in C
    .flags = 0,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = depthFormat,
    .extent = {
        .width = (uint32_t)windowSize[0], 
        .height = (uint32_t)windowSize[1], 
        .depth = 1
    },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = NULL,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  
  VmaAllocationCreateInfo allocCI = {
    .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    .usage = VMA_MEMORY_USAGE_AUTO
  };
  VkImage depthImage;
  VmaAllocation depthImageAllocation;
  chkvk(vmaCreateImage(allocator, &depthImageCI, &allocCI, &depthImage, &depthImageAllocation, NULL));
  
  VkImageViewCreateInfo depthViewCI = { 
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = depthImage,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = depthFormat,
    .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
  };
  VkImageView depthImageView;
  chkvk(vkCreateImageView(device, &depthViewCI, NULL, &depthImageView));

  // loading meshes
  static const Vertex vertices[] = {
    { {-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f} }, // bottom-left
    { { 0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f} }, // bottom-right
    { { 0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f} }, // top-right
    { {-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} }, // top-left
  };
  static const uint16_t indices[] = {
    0, 1, 2,
    2, 3, 0,
  };
 
  const VkDeviceSize indexCount = { sizeof(indices) / sizeof(uint16_t) };

  static const VkVertexInputBindingDescription bindingDesc = {
    .binding = 0,
    .stride = sizeof(Vertex),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
  };

  static const VkVertexInputAttributeDescription attributeDescs[] = {
    { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
    { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
    { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex, uv) },
  };

  VkDeviceSize vBufSize = { sizeof(vertices) };
  VkDeviceSize iBufSize = { sizeof(indices) };
  VkBufferCreateInfo bufferCI = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = vBufSize + iBufSize,
    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
  };

  VmaAllocationCreateInfo vBufferAllocCI = {
    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .usage = VMA_MEMORY_USAGE_AUTO
  };
  VmaAllocation vBufferAllocation = { VK_NULL_HANDLE };
  VkBuffer vBuffer = { VK_NULL_HANDLE };
  VmaAllocationInfo vBufferAllocInfo = { 0 };
  chkvk(vmaCreateBuffer(allocator, &bufferCI, &vBufferAllocCI, &vBuffer, &vBufferAllocation, &vBufferAllocInfo));

  memcpy(vBufferAllocInfo.pMappedData, vertices, vBufSize);
  memcpy(((char*)vBufferAllocInfo.pMappedData) + vBufSize, indices, iBufSize);

  ShaderDataBuffer shaderDataBuffers[MAX_FRAMES_IN_FLIGHT] = { 0 };
  VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT] = { 0 };

  // shader data buffers
   
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    VkBufferCreateInfo uBufferCI = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sizeof(ShaderData),
      .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    };
    VmaAllocationCreateInfo uBufferAllocCI = {
      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO
    };
    chkvk(vmaCreateBuffer(allocator, &uBufferCI, &uBufferAllocCI, &shaderDataBuffers[i].buffer, &shaderDataBuffers[i].allocation, &shaderDataBuffers[i].allocationInfo));
    VkBufferDeviceAddressInfo uBufferBdaInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = shaderDataBuffers[i].buffer
    };
    shaderDataBuffers[i].deviceAddress = vkGetBufferDeviceAddress(device, &uBufferBdaInfo);
  }

  // synchronization objects: fences, semaphores, pipeline barriers
  
  VkSemaphoreCreateInfo semaphoreCI = {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
  };
  VkFenceCreateInfo fenceCI = {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT
  };

  VkFence fences[MAX_FRAMES_IN_FLIGHT] = { VK_NULL_HANDLE };
  VkSemaphore imageAcquiredSemaphores[MAX_FRAMES_IN_FLIGHT] = { VK_NULL_HANDLE };
  
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    chkvk(vkCreateFence(device, &fenceCI, NULL, &fences[i]));
    chkvk(vkCreateSemaphore(device, &semaphoreCI, NULL, &imageAcquiredSemaphores[i]));
  }

  VkSemaphore* renderCompleteSemaphores = (VkSemaphore*)malloc(imageCount * sizeof(VkSemaphore)); // free(renderCompleteSemaphores) when done!
  if (renderCompleteSemaphores == NULL) {
    fprintf(stderr, "failed to allocate render complete semaphores\n");
    exit(1);
  }
  for (uint32_t i = 0; i < imageCount; i++) {
    chkvk(vkCreateSemaphore(device, &semaphoreCI, NULL, &renderCompleteSemaphores[i]));
  }

  // command buffers

  VkCommandPoolCreateInfo commandPoolCI = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = queueFamily
  };
  VkCommandPool commandPool = { VK_NULL_HANDLE };
  chkvk(vkCreateCommandPool(device, &commandPoolCI, NULL, &commandPool));

  VkCommandBufferAllocateInfo cbAllocCI = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = commandPool,
    .commandBufferCount = MAX_FRAMES_IN_FLIGHT
  };
  chkvk(vkAllocateCommandBuffers(device, &cbAllocCI, commandBuffers));

  // loading textures
  
  // TODO: KTX rendering
  VkDescriptorSetLayout descriptorSetLayoutTex = { VK_NULL_HANDLE };
  VkDescriptorSet descriptorSetTex = { VK_NULL_HANDLE };

  // shaders

  static const uint32_t vertShaderSpv[] = {
  #include "shaders/shader.vert.spv.inc"
  };

  static const uint32_t fragShaderSpv[] = {
  #include "shaders/shader.frag.spv.inc"
  };

  VkShaderModuleCreateInfo vertModuleCI = { // no use in vulkan 1.4, so in a few years, remove this
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = sizeof(vertShaderSpv),
    .pCode = vertShaderSpv,
  };
  VkShaderModule vertModule = VK_NULL_HANDLE;

  VkShaderModuleCreateInfo fragModuleCI = { // same deal, gone in 1.4-only code
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = sizeof(fragShaderSpv),
    .pCode = fragShaderSpv,
  };
  VkShaderModule fragModule = VK_NULL_HANDLE;
  chkvk(vkCreateShaderModule(device, &fragModuleCI, NULL, &fragModule));

  chkvk(vkCreateShaderModule(device, &vertModuleCI, NULL, &vertModule));
  
  // yay yippee yippee yay yay pipeline

  VkPushConstantRange pushConstantRange = {
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    .size = sizeof(VkDeviceAddress)
  };
  VkPipelineLayoutCreateInfo pipelineLayoutCI = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &descriptorSetLayoutTex,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges = &pushConstantRange
  };
  VkPipelineLayout pipelineLayout = { VK_NULL_HANDLE };
  chkvk(vkCreatePipelineLayout(device, &pipelineLayoutCI, NULL, &pipelineLayout));

  VkVertexInputBindingDescription vertexBinding = {
     .binding = 0,
     .stride = sizeof(Vertex),
     .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
  };

  VkVertexInputAttributeDescription vertexAttributes[] = {
    { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
    { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
    { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex, uv) }
  };

  VkPipelineVertexInputStateCreateInfo vertexInputState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &vertexBinding,
    .vertexAttributeDescriptionCount = (uint32_t)sizeof(vertexAttributes),
    .pVertexAttributeDescriptions = vertexAttributes,
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
  };

  VkPipelineShaderStageCreateInfo shaderStages[2] = {
    {
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext  = NULL, /* Good practice in C to explicitly clear or rely on zero-init */
      .flags  = 0,
      .stage  = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vertModule,
      .pName  = "main",
      .pSpecializationInfo = NULL
    },
    {
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext  = NULL,
      .flags  = 0,
      .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = fragModule,
      .pName  = "main",
      .pSpecializationInfo = NULL
    }
  };
  
  VkPipelineViewportStateCreateInfo viewportState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .scissorCount = 1
  };
  VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamicState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = sizeof(dynamicStates) / sizeof(VkPipelineDynamicStateCreateInfo),
    .pDynamicStates = dynamicStates
  };

  VkPipelineDepthStencilStateCreateInfo depthStencilState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .depthTestEnable = VK_TRUE,
    .depthWriteEnable = VK_TRUE,
    .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
  };

  VkPipelineRenderingCreateInfo renderingCI = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &imageFormat,
    .depthAttachmentFormat = depthFormat
  };

  VkPipelineColorBlendAttachmentState blendAttachment = {
    .colorWriteMask = 0xF
  };
  VkPipelineColorBlendStateCreateInfo colorBlendState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &blendAttachment
  };
  VkPipelineRasterizationStateCreateInfo rasterizationState = {
     .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
     .lineWidth = 1.0f
  };
  VkPipelineMultisampleStateCreateInfo multisampleState = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
  };

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
    .layout = pipelineLayout
  };
  VkPipeline pipeline = { VK_NULL_HANDLE };
  chkvk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, NULL, &pipeline));

  // finally, a loop, the main loop at least

  uint64_t lastTime = { SDL_GetTicks() };
  bool quit = { false };
  uint32_t frameIndex = { 0 };
  uint32_t imageIndex = { 0 };
  ShaderData shaderData = { 0 };
  vec3 camPos = { 0.0f, 0.0f, -6.0f };
  vec3 objectRotations[3] = { 0 };
  while (!quit) {
    // Wait on fence
    chk(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));
    chk(vkResetFences(device, 1, &fences[frameIndex]));
    // Acquire next image
    chkSwapchain(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAcquiredSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex));
    // Update shader data
    glm_perspective(glm_rad(45.0f), (float)windowSize[0] / (float)windowSize[1], 0.1f, 32.0f, shaderData.projection);
    glm_translate_make(shaderData.view, camPos);
    for (int i = 0; i < 3; i++) {
      vec3 instancePos = { (float)(i - 1) * 3.0f, 0.0f, 0.0f };
      mat4 translationMat, rotationMat;
      glm_translate_make(translationMat, instancePos);
      glm_euler_xyz(objectRotations[i], rotationMat);
      glm_mat4_mul(translationMat, rotationMat, shaderData.model[i]);
    }
    memcpy(shaderDataBuffers[frameIndex].allocationInfo.pMappedData, &shaderData, sizeof(ShaderData));
    // Record command buffer
    VkCommandBuffer cb = commandBuffers[frameIndex];
    chk(vkResetCommandBuffer(cb, 0));

    VkCommandBufferBeginInfo cbBI = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    chk(vkBeginCommandBuffer(cb, &cbBI));
    
    VkImageMemoryBarrier2 outputBarriers[2] = {
      {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image = swapchainImages[imageIndex],
        .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
      },
      {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image = depthImage,
        .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
      }
    };
    VkDependencyInfo barrierDependencyInfo = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 2,
      .pImageMemoryBarriers = outputBarriers
    };
    vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);


    VkRenderingAttachmentInfo colorAttachmentInfo = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = swapchainImageViews[imageIndex],
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.color = { 0.0f, 0.0f, 0.2f, 1.0f }}
    };
    VkRenderingAttachmentInfo depthAttachmentInfo = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = depthImageView,
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .clearValue = {.depthStencil = {1.0f,  0}}
    };

    VkRenderingInfo renderingInfo = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.extent = {.width = (uint32_t)windowSize[0], .height = (uint32_t)windowSize[1] }},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachmentInfo,
      .pDepthAttachment = &depthAttachmentInfo
    };
    vkCmdBeginRendering(cb, &renderingInfo);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize vOffset = { 0 };
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSetTex, 0, NULL);
    vkCmdBindVertexBuffers(cb, 0, 1, &vBuffer, &vOffset);
    vkCmdBindIndexBuffer(cb, vBuffer, vBufSize, VK_INDEX_TYPE_UINT16);
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkDeviceAddress), &shaderDataBuffers[frameIndex].deviceAddress);
    
    vkCmdDrawIndexed(cb, indexCount, 3, 0, 0, 0); // drawing, finally
    vkCmdEndRendering(cb);

    VkImageMemoryBarrier2 barrierPresent = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = 0,
      .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .image = swapchainImages[imageIndex],
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo barrierPresentDependencyInfo = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrierPresent
    };
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
      .commandBuffer = cb
    };
    VkSemaphoreSubmitInfo signalSemaphoreInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = renderCompleteSemaphores[imageIndex],
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
    VkPresentInfoKHR presentInfo = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &renderCompleteSemaphores[imageIndex],
      .swapchainCount = 1,
      .pSwapchains = &swapchain,
      .pImageIndices = &imageIndex
    };
    chkSwapchain(vkQueuePresentKHR(queue, &presentInfo));

    // Poll events

    float elapsedTime = { (SDL_GetTicks() - lastTime) / 1000.0f };
    lastTime = SDL_GetTicks();
    for (SDL_Event event; SDL_PollEvent(&event);) {

        // Exit loop if the application is about to close
        if (event.type == SDL_EVENT_QUIT) {
            quit = true;
            break;
        }

        // Rotate the selected object with mouse drag
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                objectRotations[shaderData.selected][0] -= (float)event.motion.yrel * elapsedTime;
                objectRotations[shaderData.selected][1] += (float)event.motion.xrel * elapsedTime;
            }
        }

        // Zooming with the mouse wheel
        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            camPos[2] += (float)event.wheel.y * elapsedTime * 10.0f;
        }

        // Select active model instance
        if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS) {
                shaderData.selected = (shaderData.selected < 2) ? shaderData.selected + 1 : 0;
            }
            if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
                shaderData.selected = (shaderData.selected > 0) ? shaderData.selected - 1 : 2;
            }
        }

        // Window resize
        if (event.type == SDL_EVENT_WINDOW_RESIZED) {
            updateSwapchain = true;
        }
    }

    // swapchain update 
    
    if (updateSwapchain) {
        updateSwapchain = false;
        chkvk(vkDeviceWaitIdle(device));
        chkvk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCaps));

        uint32_t oldImageCount = imageCount; // must save this before imageCount gets overwritten below

        swapchainCI.oldSwapchain = swapchain;
        swapchainCI.imageExtent = (VkExtent2D){ .width = (uint32_t)windowSize[0], .height = (uint32_t)windowSize[1] };
        chkvk(vkCreateSwapchainKHR(device, &swapchainCI, NULL, &swapchain));

        for (uint32_t i = 0; i < oldImageCount; i++) {
            vkDestroyImageView(device, swapchainImageViews[i], NULL);
        }

        chkvk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, NULL));

        free(swapchainImages);
        swapchainImages = (VkImage*)malloc(imageCount * sizeof(VkImage));
        chkvk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages));

        free(swapchainImageViews);
        swapchainImageViews = (VkImageView*)malloc(imageCount * sizeof(VkImageView));
        for (uint32_t i = 0; i < imageCount; i++) {
            VkImageViewCreateInfo viewCI = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapchainImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = imageFormat,
                .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
            };
            chkvk(vkCreateImageView(device, &viewCI, NULL, &swapchainImageViews[i]));
        }

        for (uint32_t i = 0; i < oldImageCount; i++) {
            vkDestroySemaphore(device, renderCompleteSemaphores[i], NULL);
        }
        free(renderCompleteSemaphores);
        renderCompleteSemaphores = (VkSemaphore*)malloc(imageCount * sizeof(VkSemaphore));
        for (uint32_t i = 0; i < imageCount; i++) {
            chkvk(vkCreateSemaphore(device, &semaphoreCI, NULL, &renderCompleteSemaphores[i]));
        }

        vkDestroySwapchainKHR(device, swapchainCI.oldSwapchain, NULL);

        vmaDestroyImage(allocator, depthImage, depthImageAllocation);
        vkDestroyImageView(device, depthImageView, NULL);

        depthImageCI.extent = (VkExtent3D){ .width = (uint32_t)windowSize[0], .height = (uint32_t)windowSize[1], .depth = 1 };
        VmaAllocationCreateInfo allocCI = {
            .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO
        };
        chkvk(vmaCreateImage(allocator, &depthImageCI, &allocCI, &depthImage, &depthImageAllocation, NULL));

        VkImageViewCreateInfo depthViewCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = depthImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = depthFormat,
            .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
        };
        chkvk(vkCreateImageView(device, &depthViewCI, NULL, &depthImageView));
    }
  }

}
