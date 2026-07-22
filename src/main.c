#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <stdio.h>

int main(void) {
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *window = glfwCreateWindow(1600, 900, "Peranti 0", NULL, NULL);
  if(window == NULL) {
    printf("how the hell do you mess up making a window???");
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  gladLoadGL((GLADloadfunc)glfwGetProcAddress);
  int fb_width, fb_height;
  glfwGetFramebufferSize(window, &fb_width, &fb_height);
  glViewport(0, 0, fb_width, fb_height);
  

  glClearColor(1.0f, 0.0f, 1.0f, 0.5f);
  glClear(GL_COLOR_BUFFER_BIT);
  glfwSwapBuffers(window);
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    // main loooop
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
