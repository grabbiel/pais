// src/rhi/backends/vulkan/device_vulkan.cpp
// Placeholder implementation for the Vulkan backend. Phase 1 wires the build
// system and platform abstractions for Vulkan without providing a full runtime
// implementation yet.

#include "pixel/rhi/rhi.hpp"

#include <stdexcept>

#if defined(PIXEL_USE_VULKAN)
#  include <vulkan/vulkan.h>

namespace pixel::rhi {

Device *create_vulkan_device(void *window_handle) {
  (void)window_handle;

  throw std::runtime_error(
      "Vulkan backend scaffolding is in place, but the runtime implementation "
      "has not been provided yet.");
}

} // namespace pixel::rhi

#else

namespace pixel::rhi {

Device *create_vulkan_device(void *window_handle) {
  (void)window_handle;
  throw std::runtime_error(
      "Vulkan backend was not enabled in this build configuration.");
}

} // namespace pixel::rhi

#endif
