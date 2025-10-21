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

#ifdef PIXEL_USE_METAL
  // ============================================================================
  // Metal-only path (no OpenGL fallback)
  // ============================================================================
  std::cout << "Initializing Metal backend..." << std::endl;

  result.window = create_window_for_backend(spec, true);
  if (!result.window) {
    throw std::runtime_error("Failed to create window for Metal backend");
  }

  result.device = rhi::create_metal_device(result.window);
  if (!result.device) {
    glfwDestroyWindow(result.window);
    throw std::runtime_error("Failed to create Metal device. Metal is required "
                             "but initialization failed.");
  }

  result.backend_name = "Metal";
  std::cout << "Renderer Backend: Metal" << std::endl;

#else
  // ============================================================================
  // OpenGL-only path (non-macOS or macOS without Metal)
  // ============================================================================
  std::cout << "Initializing OpenGL backend..." << std::endl;

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
  std::cout << "Renderer Backend: OpenGL" << std::endl;

#endif

  return result;
}

} // namespace pixel::renderer3d
