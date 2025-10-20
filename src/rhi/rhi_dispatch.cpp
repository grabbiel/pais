// src/rhi/rhi_dispatch.cpp
#include "pixel/rhi/rhi.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstdlib>
#include <cstring>

namespace pixel::rhi {

// Forward declarations for backend factories
Device *create_gl_device(GLFWwindow *window);

#ifdef __APPLE__
Device *create_metal_device(void *window);
#endif

Device *create_device_from_config(bool preferMetalIfAvailable) {
  // Initialize GLFW to get a window for context
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return nullptr;
  }

  GLFWwindow *window = nullptr;

#ifdef __APPLE__
  if (preferMetalIfAvailable) {
    // Try Metal first on macOS
    std::cout << "Attempting Metal backend..." << std::endl;

    // Create window without OpenGL context for Metal
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(800, 600, "RHI Window", nullptr, nullptr);

    if (window) {
      Device *metal_device = create_metal_device(window);
      if (metal_device) {
        std::cout << "Metal backend initialized successfully" << std::endl;
        return metal_device;
      }
      std::cout << "Metal backend failed, falling back to OpenGL" << std::endl;
      glfwDestroyWindow(window);
      window = nullptr;
    }
  }
#endif

  // Fall back to OpenGL (or use it directly if Metal not preferred)
  std::cout << "Initializing OpenGL backend..." << std::endl;

  // Request OpenGL 3.3 Core Profile
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  window = glfwCreateWindow(800, 600, "RHI Window", nullptr, nullptr);

  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return nullptr;
  }

  Device *gl_device = create_gl_device(window);
  if (gl_device) {
    std::cout << "OpenGL backend initialized successfully" << std::endl;
    return gl_device;
  }

  std::cerr << "Failed to create OpenGL device" << std::endl;
  glfwDestroyWindow(window);
  glfwTerminate();
  return nullptr;
}

} // namespace pixel::rhi
