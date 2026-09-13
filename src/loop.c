#include "app.h"
#include <math.h>

int peranti_mainloop(void) {
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

// Flat (pitch-independent) forward vector for W/S. camFront[0]/camFront[2]
// are each scaled by cos(pitch) (see the spherical-coordinate camFront
// above), so their combined horizontal length is cos(pitch) -- using them
// directly here would make forward/back speed shrink to zero the more you
// look up or down. Derived from yaw alone, this is already unit length
// (sin^2 + cos^2 = 1), so walking speed stays constant regardless of pitch.
vec3 camFrontFlat = {sinf(yaw), 0.0f, cosf(yaw)};

if (state[SDL_SCANCODE_W]) {
    camPos[0] += camFrontFlat[0] * cameraSpeed;
    camPos[2] += camFrontFlat[2] * cameraSpeed;
}
if (state[SDL_SCANCODE_S]) {
    camPos[0] -= camFrontFlat[0] * cameraSpeed;
    camPos[2] -= camFrontFlat[2] * cameraSpeed;
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
  SDL_GetWindowSizeInPixels(app.window, &sc.windowSize[0], &sc.windowSize[1]);

  if (sc.windowSize[0] == 0 || sc.windowSize[1] == 0) {
    SDL_PollEvent(&(SDL_Event){0}); // Simple event poll
    SDL_Delay(10);
    continue;
  }

    // Wait on fence
    chkvk(vkWaitForFences(app.device, 1, &app.fences[frameIndex], true, UINT64_MAX));

    // Acquire next image
    VkResult acquireResult = vkAcquireNextImageKHR(
    app.device, sc.swapchain, UINT64_MAX,
    app.imageAcquiredSemaphores[frameIndex],
    VK_NULL_HANDLE, &imageIndex);

if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
    updateSwapchain = false; // recreateSwapchain() below already handles it
    recreateSwapchain(); // do it right here -- `continue` skips the block further down otherwise
    continue;
} else if (acquireResult != VK_SUCCESS) {
    chkvk(acquireResult);
}
    // update fence AFTER!
    chkvk(vkResetFences(app.device, 1, &app.fences[frameIndex]));
    // Update shader data
    //
    // --- past base cube: far plane bumped from 32 to 512. The old value
    // was tuned for a 1-unit hardcoded cube near the origin; the mesher's
    // output is node-scale (16 units/mapblock at NODE_SIZE=1 in
    // mesh_legacy.c), so 32 was clipping the cube itself at a normal
    // walking distance. 512 is arbitrary headroom for the current
    // single-mapblock test scene, not a real answer -- once Phase 5
    // streaming exists, far should be driven by actual view distance, not
    // a constant.
    glm_perspective(glm_rad(45.0f), (float)sc.windowSize[0] / (float)sc.windowSize[1],
                    0.1f, 512.0f, shaderData.projection);
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
    memcpy(app.shaderDataBuffers[frameIndex].allocationInfo.pMappedData,
           &shaderData, sizeof(ShaderData));
    // Record command buffer
    VkCommandBuffer cb = app.commandBuffers[frameIndex];
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

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);

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
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelineLayout,
                            0, 1, &app.descriptorSetTex, 0, NULL);
    vkCmdBindVertexBuffers(cb, 0, 1, &app.vBuffer, &vOffset);
    vkCmdBindIndexBuffer(cb, app.vBuffer, app.vBufSize, VK_INDEX_TYPE_UINT16);
    vkCmdPushConstants(cb, app.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(VkDeviceAddress),
                       &app.shaderDataBuffers[frameIndex].deviceAddress);

    vkCmdDrawIndexed(cb, app.indexCount, 1, 0, 0, 0); // drawing, finally
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
        .semaphore = app.imageAcquiredSemaphores[frameIndex],
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
    chkvk(vkQueueSubmit2(app.queue, 1, &submitInfo, app.fences[frameIndex]));
    frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    // Present image
    VkPresentInfoKHR presentInfo = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                    .waitSemaphoreCount = 1,
                                    .pWaitSemaphores =
                                        &sc.renderCompleteSemaphores[imageIndex],
                                    .swapchainCount = 1,
                                    .pSwapchains = &sc.swapchain,
                                    .pImageIndices = &imageIndex};
    VkResult presentResult = vkQueuePresentKHR(app.queue, &presentInfo);

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
        camFront[0] = cosf(pitch) * sinf(yaw);
        camFront[1] = sinf(pitch);
        camFront[2] = cosf(pitch) * cosf(yaw);
        glm_normalize(camFront); // already unit length by construction; kept as cheap fp-drift insurance
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
          chk(SDL_SetWindowRelativeMouseMode(app.window, false));
          mouseCaptured = false;
        }
      }

      // Click back inside the window to recapture the cursor
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !mouseCaptured) {
        chk(SDL_SetWindowRelativeMouseMode(app.window, true));
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

  return 0;
}
