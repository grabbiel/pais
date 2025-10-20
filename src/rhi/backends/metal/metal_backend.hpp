// src/rhi/backends/metal/metal_backend.hpp
#pragma once

#ifdef __APPLE__

#include "pixel/renderer3d/renderer.hpp"

#include <Metal/Metal.h>
#include <MetalKit/MetalKit.h>
#include <QuartzCore/CAMetalLayer.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pixel::renderer3d::metal {

// Forward declarations
class MetalShader;
class MetalMesh;
class MetalTexture;
class MetalTextureArray;

// ============================================================================
// Metal Resource Base
// ============================================================================

class MetalResource {
public:
  virtual ~MetalResource() = default;

protected:
  MetalResource() = default;
};

// ============================================================================
// Metal Shader (Stub)
// ============================================================================

class MetalShader : public MetalResource {
public:
  static std::unique_ptr<MetalShader> create(id<MTLDevice> device,
                                             const std::string &vertex_src,
                                             const std::string &fragment_src);

  ~MetalShader();

  void bind(id<MTLRenderCommandEncoder> encoder);

  void set_int(const std::string &name, int value);
  void set_float(const std::string &name, float value);
  void set_vec3(const std::string &name, const Vec3 &value);
  void set_mat4(const std::string &name, const float *value);

  id<MTLRenderPipelineState> pipeline_state() const { return pipeline_state_; }

private:
  MetalShader() = default;

  id<MTLDevice> device_ = nil;
  id<MTLLibrary> library_ = nil;
  id<MTLFunction> vertex_function_ = nil;
  id<MTLFunction> fragment_function_ = nil;
  id<MTLRenderPipelineState> pipeline_state_ = nil;
  id<MTLDepthStencilState> depth_stencil_state_ = nil;

  struct UniformBuffer {
    id<MTLBuffer> buffer = nil;
    size_t size = 0;
  };
  std::unordered_map<std::string, UniformBuffer> uniform_buffers_;
};

// ============================================================================
// Metal Mesh (Stub)
// ============================================================================

class MetalMesh : public MetalResource {
public:
  static std::unique_ptr<MetalMesh>
  create(id<MTLDevice> device, const std::vector<Vertex> &vertices,
         const std::vector<uint32_t> &indices);

  ~MetalMesh();

  void draw(id<MTLRenderCommandEncoder> encoder) const;

  size_t vertex_count() const { return vertex_count_; }
  size_t index_count() const { return index_count_; }

private:
  MetalMesh() = default;

  id<MTLDevice> device_ = nil;
  id<MTLBuffer> vertex_buffer_ = nil;
  id<MTLBuffer> index_buffer_ = nil;
  size_t vertex_count_ = 0;
  size_t index_count_ = 0;
};

// ============================================================================
// Metal Texture (Stub)
// ============================================================================

class MetalTexture : public MetalResource {
public:
  static std::unique_ptr<MetalTexture> create(id<MTLDevice> device, int width,
                                              int height, const uint8_t *data);

  ~MetalTexture();

  void bind(id<MTLRenderCommandEncoder> encoder, int slot);

  int width() const { return width_; }
  int height() const { return height_; }

private:
  MetalTexture() = default;

  id<MTLDevice> device_ = nil;
  id<MTLTexture> texture_ = nil;
  id<MTLSamplerState> sampler_ = nil;
  int width_ = 0;
  int height_ = 0;
};

// ============================================================================
// Metal Backend (Stub)
// ============================================================================

class MetalBackend {
public:
  static std::unique_ptr<MetalBackend> create(GLFWwindow *window);

  ~MetalBackend();

  void begin_frame(const Color &clear_color);
  void end_frame();

  std::unique_ptr<MetalShader> create_shader(const std::string &vertex_src,
                                             const std::string &fragment_src);

  std::unique_ptr<MetalMesh> create_mesh(const std::vector<Vertex> &vertices,
                                         const std::vector<uint32_t> &indices);

  std::unique_ptr<MetalTexture> create_texture(int width, int height,
                                               const uint8_t *data);

  void draw_mesh(const MetalMesh &mesh, const Vec3 &position,
                 const Vec3 &rotation, const Vec3 &scale, MetalShader *shader,
                 const Material &material);

  id<MTLDevice> device() const { return device_; }
  id<MTLCommandQueue> command_queue() const { return command_queue_; }

  int viewport_width() const { return viewport_width_; }
  int viewport_height() const { return viewport_height_; }

private:
  MetalBackend() = default;

  bool initialize(GLFWwindow *window);
  void create_default_shaders();
  void setup_render_pass_descriptor();

  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> command_queue_ = nil;
  CAMetalLayer *metal_layer_ = nil;

  id<CAMetalDrawable> current_drawable_ = nil;
  id<MTLCommandBuffer> command_buffer_ = nil;
  id<MTLRenderCommandEncoder> render_encoder_ = nil;
  MTLRenderPassDescriptor *render_pass_descriptor_ = nil;

  id<MTLTexture> depth_texture_ = nil;

  int viewport_width_ = 0;
  int viewport_height_ = 0;

  std::unique_ptr<MetalShader> default_shader_;
  std::vector<std::unique_ptr<MetalResource>> resources_;
};

} // namespace pixel::renderer3d::metal

#endif // __APPLE__
