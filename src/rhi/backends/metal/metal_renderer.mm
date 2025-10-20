#ifdef __APPLE__

#include "pixel/rhi/backends/metal/metal_renderer.hpp"

#include "metal_backend.hpp"

#include <memory>
#include <unordered_map>

namespace pixel::renderer3d::metal {

struct MetalRenderer::Impl {
  std::unique_ptr<MetalBackend> backend;
  std::unordered_map<const Mesh *, std::unique_ptr<MetalMesh>> mesh_cache;
};

MetalRenderer::MetalRenderer() = default;
MetalRenderer::~MetalRenderer() = default;

std::unique_ptr<MetalRenderer>
MetalRenderer::create(const pixel::platform::WindowSpec & /*spec*/) {
  // Metal renderer is not yet fully implemented; fall back to OpenGL by
  // returning nullptr just like the factory stub does today.
  return nullptr;
}

void MetalRenderer::begin_frame(const Color & /*clear_color*/) {}

void MetalRenderer::end_frame() {}

void MetalRenderer::draw_mesh(const Mesh & /*mesh*/, const Vec3 & /*position*/,
                              const Vec3 & /*rotation*/, const Vec3 & /*scale*/,
                              const Material & /*material*/) {}

MetalBackend *MetalRenderer::metal_backend() {
  return impl_ ? impl_->backend.get() : nullptr;
}

const MetalBackend *MetalRenderer::metal_backend() const {
  return impl_ ? impl_->backend.get() : nullptr;
}

} // namespace pixel::renderer3d::metal

#endif // __APPLE__
