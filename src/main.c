#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdio.h>

typedef struct App {
  GLFWwindow *window;
  VkInstance instance;
} App;

const int WIDTH = 800;
const int HEIGHT = 600;

bool verifyExtensionSupport(uint32_t extensionCount, VkExtensionProperties *extensions, uint32_t glfwExtensionCount, const char **glfwExtensions) {
  uint32_t found_extensions = 0;
  for(int i = 0; i < glfwExtensionCount; i++) {
    for(int j = 0; j < extensionCount; j++) {
      if(strcmp(extensions[j].extensionName, glfwExtensions[i]) == 0) {
        printf("found support for:%s = %s\n", extensions[j].extensionName, glfwExtensions[i]);
        found_extensions++;
      } else {
        printf("no support for:%s = %s\n", extensions[j].extensionName, glfwExtensions[i]);
      }
    }
  }
  return found_extensions == glfwExtensionCount;
}

void createInstance(App *pApp) {
  VkApplicationInfo appInfo = {0};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Peranti";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;
  appInfo.pNext = NULL;

  uint32_t glfwExtensionCount = 0;
  const char** glfwExtensions;

  glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  VkInstanceCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = glfwExtensionCount;
  createInfo.ppEnabledExtensionNames = glfwExtensions;
  createInfo.enabledLayerCount = 0;
  if (vkCreateInstance(&createInfo, NULL, &pApp->instance) != VK_SUCCESS) {
    printf("createInstance failure!\n");
    exit(1);
  }
  uint32_t extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
  VkExtensionProperties *extensions = (VkExtensionProperties*)malloc(extensionCount * sizeof(VkExtensionProperties));
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensions);
  for(uint32_t i = 0; i < extensionCount; i++) {
    printf("\tExtension: %s\n", extensions[i].extensionName);
  }
  bool extensionsSupported = verifyExtensionSupport(extensionCount, extensions, glfwExtensionCount, glfwExtensions);
  if(extensionsSupported) {
    printf("all required extensions supported! yippee!\n");
  } else {
    printf("some required extensions not supported\n");
    exit(1);
  }
}


void initWindow(App *pApp) {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  pApp->window = glfwCreateWindow(WIDTH, HEIGHT, "Peranti 0", NULL, NULL);
}
void initVulkan(App *pApp) {
  createInstance(pApp);
}
void mainLoop(App *pApp) {
    while (!glfwWindowShouldClose(pApp->window)) {
        glfwPollEvents();
    }
}
void cleanup(App *pApp) {
  vkDestroyInstance(pApp->instance, NULL);
  glfwDestroyWindow(pApp->window);
  glfwTerminate();
}

int main(void) {
  printf("the slopfest begins here babyyyy\n");
  App app = {0};
  initWindow(&app);
  initVulkan(&app);
  mainLoop(&app);
  cleanup(&app);

  return 0;
}
