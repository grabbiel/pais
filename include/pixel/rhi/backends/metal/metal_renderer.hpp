// include/pixel/renderer3d/metal/metal_renderer.hpp
#pragma once

#ifdef __APPLE__

#include "../src/rhi/backends/metal/metal_backend.hpp"
#include "pixel/renderer3d/renderer.hpp"

#include <memory>

namespace pixel::renderer3d::metal {

class MetalBackend;

class MetalRenderer : public Renderer {
public:
  static std::unique_ptr<MetalRenderer>
  create(const pixel::platform::WindowSpec &spec);

  ~MetalRenderer();

  void begin_frame(const Color &clear_color = Color::Black()) override;
  void end_frame() override;

  void draw_mesh(const Mesh &mesh, const Vec3 &position, const Vec3 &rotation,
                 const Vec3 &scale, const Material &material) override;

  MetalBackend *metal_backend();
  const MetalBackend *metal_backend() const;

private:
  MetalRenderer();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pixel::renderer3d::metal

#endif // __APPLE__
