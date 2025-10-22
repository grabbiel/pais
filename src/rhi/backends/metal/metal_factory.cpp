// src/renderer3d/metal/metal_factory.cpp
// This file is only compiled when PIXEL_USE_METAL is defined

#if PIXEL_USE_METAL && __APPLE__

#include "pixel/renderer3d/renderer.hpp"
#include "pixel/rhi/backends/metal/metal_renderer.hpp"

#include <iostream>
#include <memory>

namespace pixel::renderer3d {

// This function attempts to create a Metal renderer
// The actual implementation would be in metal_backend.mm
// For now, this is a stub that always returns nullptr to fall back to OpenGL
std::unique_ptr<Renderer>
create_metal_renderer(const pixel::platform::WindowSpec &spec) {
  auto metal_renderer = metal::MetalRenderer::create(spec);
  if (!metal_renderer) {
    std::cerr << "Failed to create Metal renderer" << std::endl;
    return nullptr;
  }

  return std::unique_ptr<Renderer>(metal_renderer.release());
}

} // namespace pixel::renderer3d

#endif // PIXEL_USE_METAL && __APPLE__
