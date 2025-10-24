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

  switch (preferred_api) {
  case GraphicsAPI::Default:
    break;
  case GraphicsAPI::Metal:
#if !defined(PIXEL_USE_METAL)
    throw std::runtime_error("Metal backend requested but PIXEL_USE_METAL is disabled in this build.");
#endif
    break;
  case GraphicsAPI::DirectX12:
#if !defined(PIXEL_USE_DX12)
    throw std::runtime_error("DirectX 12 backend requested but PIXEL_USE_DX12 is disabled in this build.");
#endif
    break;
  case GraphicsAPI::Vulkan:
#if !defined(PIXEL_USE_VULKAN)
    throw std::runtime_error("Vulkan backend requested but PIXEL_USE_VULKAN is disabled in this build.");
#endif
    break;
  }

#if defined(PIXEL_USE_VULKAN)
  if (preferred_api == GraphicsAPI::Default ||
      preferred_api == GraphicsAPI::Vulkan) {
    std::cout << "Creating Vulkan device..." << std::endl;

    Device *device = create_vulkan_device(window->native_handle());
    if (!device) {
      throw std::runtime_error(
          "Failed to create Vulkan device. The backend may not be implemented "
          "yet.");
    }

    std::cout << "Device Backend: " << device->backend_name() << std::endl;
    return std::unique_ptr<Device>(device);
  }
#endif

#if defined(PIXEL_USE_DX12)
  if (preferred_api == GraphicsAPI::Default ||
      preferred_api == GraphicsAPI::DirectX12) {
    std::cout << "Creating DirectX 12 device..." << std::endl;

    Device *device = create_dx12_device(window->native_handle());
    if (!device) {
      throw std::runtime_error(
          "Failed to create DirectX 12 device. The backend may not be "
          "implemented yet.");
    }

    std::cout << "Device Backend: " << device->backend_name() << std::endl;
    return std::unique_ptr<Device>(device);
  }
#endif

#if defined(PIXEL_USE_METAL)
  if (preferred_api == GraphicsAPI::Default ||
      preferred_api == GraphicsAPI::Metal) {
    std::cout << "Creating Metal device..." << std::endl;

    Device *device = create_metal_device(window->native_handle());
    if (!device) {
      throw std::runtime_error("Failed to create Metal device. Metal is required "
                               "but initialization failed.");
    }

    std::cout << "Device Backend: " << device->backend_name() << std::endl;
    return std::unique_ptr<Device>(device);
  }
#endif

  throw std::runtime_error(
      "No supported graphics backend configured. Enable PIXEL_USE_METAL, "
      "PIXEL_USE_VULKAN, or PIXEL_USE_DX12 when generating your build files.");
}

} // namespace pixel::rhi
