#define GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdio.h>

typedef struct App {
  GLFWwindow *window;
} App;

const int WIDTH = 800;
const int HEIGHT = 600;

void initWindow(App *pApp) {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  pApp->window = glfwCreateWindow(WIDTH, HEIGHT, "Peranti 0", NULL, NULL);
}
void initVulkan(void) {
}
void mainLoop(App *pApp) {
    while (!glfwWindowShouldClose(pApp->window)) {
        glfwPollEvents();
    }
}
void cleanup(App *pApp) {
  glfwDestroyWindow(pApp->window);
  glfwTerminate();
}

int main(void) {
  printf("the slopfest begins here babyyyy\n");
  App app = {0};
  initWindow(&app);
  initVulkan();
  mainLoop(&app);
  cleanup(&app);

  return 0;
}
