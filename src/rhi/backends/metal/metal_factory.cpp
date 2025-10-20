// src/renderer3d/metal/metal_factory.cpp
// This file is only compiled when PIXEL_USE_METAL is defined

#if PIXEL_USE_METAL && __APPLE__

#include "pixel/renderer3d/renderer.hpp"
#include <iostream>

namespace pixel::renderer3d {

// This function attempts to create a Metal renderer
// The actual implementation would be in metal_backend.mm
// For now, this is a stub that always returns nullptr to fall back to OpenGL
std::unique_ptr<Renderer>
create_metal_renderer(const pixel::platform::WindowSpec &spec) {
  std::cout << "Metal backend not yet implemented, using OpenGL fallback"
            << std::endl;
  return nullptr;

  // When Metal implementation is ready, this would call:
  // return metal::MetalRenderer::create(spec);
}

} // namespace pixel::renderer3d

#endif // PIXEL_USE_METAL && __APPLE__
