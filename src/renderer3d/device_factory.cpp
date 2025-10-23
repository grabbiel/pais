// src/renderer3d/device_factory.cpp
// Helper to create appropriate RHI device for the renderer

#include "pixel/rhi/rhi.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/platform/window.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace pixel::rhi {

std::unique_ptr<Device> create_device(platform::Window *window,
                                      GraphicsAPI preferred_api) {

  if (!window) {
    throw std::invalid_argument("Window pointer cannot be null");
  }

#ifdef PIXEL_USE_METAL
  // ============================================================================
  // Metal backend
  // ============================================================================
  std::cout << "Creating Metal device..." << std::endl;

  Device *device = create_metal_device(window->native_handle());
  if (!device) {
    throw std::runtime_error("Failed to create Metal device. Metal is required "
                             "but initialization failed.");
  }

  std::cout << "Device Backend: Metal" << std::endl;
  return std::unique_ptr<Device>(device);

#else
  // ============================================================================
  // OpenGL backend
  // ============================================================================
  std::cout << "Creating OpenGL device..." << std::endl;

  Device *device = create_gl_device(window->native_handle());
  if (!device) {
    throw std::runtime_error("Failed to create OpenGL device");
  }

  std::cout << "Device Backend: OpenGL" << std::endl;
  return std::unique_ptr<Device>(device);

#endif
}

} // namespace pixel::rhi
