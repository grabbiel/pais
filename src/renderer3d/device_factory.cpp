// src/renderer3d/device_factory.cpp
// Helper to create appropriate RHI device for the renderer

#include "pixel/rhi/rhi.hpp"
#include "pixel/platform/platform.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>

namespace pixel::renderer3d {

// Helper struct to hold device creation result
struct DeviceCreationResult {
  rhi::Device *device = nullptr;
  GLFWwindow *window = nullptr;
  std::string backend_name;
};

// Create a window with appropriate hints for the backend
static GLFWwindow *create_window_for_backend(const platform::WindowSpec &spec,
                                             bool use_metal) {

  if (use_metal) {
#ifdef __APPLE__
    // For Metal, don't create an OpenGL context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
  } else {
    // For OpenGL, request 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  }

  return glfwCreateWindow(spec.w, spec.h, spec.title.c_str(), nullptr, nullptr);
}

// Main device creation function
DeviceCreationResult create_renderer_device(const platform::WindowSpec &spec) {
  DeviceCreationResult result;

  // Determine preferred backend
  bool prefer_metal = false;
#ifdef __APPLE__
  const char *backend_env = std::getenv("PIXEL_RENDERER_BACKEND");
  // On macOS, default to Metal unless explicitly set to OPENGL
  prefer_metal = !(backend_env && std::strcmp(backend_env, "OPENGL") == 0);
#endif

#ifdef __APPLE__
  // Try Metal first if preferred
  if (prefer_metal) {
    result.window = create_window_for_backend(spec, true);
    if (!result.window) {
      throw std::runtime_error("Failed to create window for Metal backend");
    }

    try {
      result.device = rhi::create_metal_device(result.window);
      if (result.device) {
        result.backend_name = "Metal";
        return result;
      }
    } catch (const std::exception &e) {
      std::cerr << "Metal initialization failed: " << e.what() << std::endl;
    }

    // Metal failed, clean up and try OpenGL
    std::cout << "Falling back to OpenGL..." << std::endl;
    glfwDestroyWindow(result.window);
    result.window = nullptr;
    result.device = nullptr;
  }
#endif

  // Create OpenGL device (either as fallback or primary choice)
  result.window = create_window_for_backend(spec, false);
  if (!result.window) {
    throw std::runtime_error("Failed to create window for OpenGL backend");
  }

  result.device = rhi::create_gl_device(result.window);
  if (!result.device) {
    glfwDestroyWindow(result.window);
    throw std::runtime_error("Failed to create OpenGL device");
  }

  result.backend_name = "OpenGL";
  return result;
}

} // namespace pixel::renderer3d
