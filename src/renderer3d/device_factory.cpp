// src/renderer3d/device_factory.cpp
// Helper to create appropriate RHI device for the renderer

#include "pixel/rhi/rhi.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/platform/window.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>

namespace pixel::renderer3d {

// Helper struct to hold device creation result
struct DeviceCreationResult {
  rhi::Device *device = nullptr;
  platform::Window *window = nullptr;
  std::string backend_name;
};

// Main device creation function
DeviceCreationResult create_renderer_device(const platform::WindowSpec &spec) {
  DeviceCreationResult result;

#ifdef PIXEL_USE_METAL
  // ============================================================================
  // Metal-only path (no OpenGL fallback)
  // ============================================================================
  std::cout << "Initializing Metal backend..." << std::endl;

  // Create window with Metal API
  auto window = platform::Window::create(spec, platform::GraphicsAPI::Metal);
  if (!window) {
    throw std::runtime_error("Failed to create window for Metal backend");
  }

  result.device = rhi::create_metal_device(window->native_handle());
  if (!result.device) {
    throw std::runtime_error("Failed to create Metal device. Metal is required "
                             "but initialization failed.");
  }

  result.window = window.release();  // Transfer ownership to result
  result.backend_name = "Metal";
  std::cout << "Renderer Backend: Metal" << std::endl;

#else
  // ============================================================================
  // OpenGL-only path (non-macOS or macOS without Metal)
  // ============================================================================
  std::cout << "Initializing OpenGL backend..." << std::endl;

  // Create window with OpenGL API
  auto window = platform::Window::create(spec, platform::GraphicsAPI::OpenGL);
  if (!window) {
    throw std::runtime_error("Failed to create window for OpenGL backend");
  }

  result.device = rhi::create_gl_device(window->native_handle());
  if (!result.device) {
    throw std::runtime_error("Failed to create OpenGL device");
  }

  result.window = window.release();  // Transfer ownership to result
  result.backend_name = "OpenGL";
  std::cout << "Renderer Backend: OpenGL" << std::endl;

#endif

  return result;
}

} // namespace pixel::renderer3d
