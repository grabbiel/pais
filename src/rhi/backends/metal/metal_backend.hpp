// src/renderer3d/metal/metal_backend.hpp
#pragma once

#ifdef __APPLE__

#include "pixel/renderer3d/renderer.hpp"

#include <Metal/Metal.h>
#include <MetalKit/MetalKit.h>
#include <QuartzCore/CAMetalLayer.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pixel::renderer3d::metal {

class MetalShader;
class MetalMesh;
class MetalTexture;
class MetalTextureArray;
class MetalInstanceBuffer;
class MetalLODMesh;

class MetalResource {
public:
  virtual ~MetalResource() = default;

protected:
  MetalResource() = default;
};

class MetalShader : public MetalResource {
public:
  static std::unique_ptr<MetalShader> create(id<MTLDevice> device,
                                             const std::string &vertex_src,
                                             const std::string &fragment_src);

  ~MetalShader();

  void bind(id<MTLRenderCommandEncoder> encoder);

  void set_int(const std::string &name, int value);
  void set_float(const std::string &name, float value);
  void set_vec2(const std::string &name, const Vec2 &value);
  void set_vec3(const std::string &name, const Vec3 &value);
  void set_vec4(const std::string &name, const Vec4 &value);
  void set_mat4(const std::string &name, const float *value);

  id<MTLRenderPipelineState> pipeline_state() const { return pipeline_state_; }
  id<MTLDepthStencilState> depth_stencil_state() const {
    return depth_stencil_state_;
  }

private:
  MetalShader() = default;

  struct UniformBuffer {
    id<MTLBuffer> buffer = nil;
    size_t size = 0;
    void *mapped_memory = nullptr;
  };

  id<MTLDevice> device_ = nil;
  id<MTLLibrary> library_ = nil;
  id<MTLFunction> vertex_function_ = nil;
  id<MTLFunction> fragment_function_ = nil;
  id<MTLRenderPipelineState> pipeline_state_ = nil;
  id<MTLDepthStencilState> depth_stencil_state_ = nil;

  std::unordered_map<std::string, UniformBuffer> uniform_buffers_;
  std::unordered_map<std::string, size_t> uniform_offsets_;
};

class MetalMesh : public MetalResource {
public:
  static std::unique_ptr<MetalMesh>
  create(id<MTLDevice> device, const std::vector<Vertex> &vertices,
         const std::vector<uint32_t> &indices);

  ~MetalMesh();

  void draw(id<MTLRenderCommandEncoder> encoder) const;

  size_t vertex_count() const { return vertex_count_; }
  size_t index_count() const { return index_count_; }

  id<MTLBuffer> vertex_buffer() const { return vertex_buffer_; }
  id<MTLBuffer> index_buffer() const { return index_buffer_; }

private:
  MetalMesh() = default;

  id<MTLDevice> device_ = nil;
  id<MTLBuffer> vertex_buffer_ = nil;
  id<MTLBuffer> index_buffer_ = nil;
  size_t vertex_count_ = 0;
  size_t index_count_ = 0;
};

class MetalTexture : public MetalResource {
public:
  static std::unique_ptr<MetalTexture> create(id<MTLDevice> device, int width,
                                              int height, const uint8_t *data);

  ~MetalTexture();

  void bind(id<MTLRenderCommandEncoder> encoder, int slot);

  int width() const { return width_; }
  int height() const { return height_; }
  id<MTLTexture> texture() const { return texture_; }
  id<MTLSamplerState> sampler() const { return sampler_; }

private:
  MetalTexture() = default;

  id<MTLDevice> device_ = nil;
  id<MTLTexture> texture_ = nil;
  id<MTLSamplerState> sampler_ = nil;
  int width_ = 0;
  int height_ = 0;
};

class MetalTextureArray : public MetalResource {
public:
  static std::unique_ptr<MetalTextureArray>
  create(id<MTLDevice> device, int width, int height, int layers);

  ~MetalTextureArray();

  void set_layer(int layer, const uint8_t *data);
  void bind(id<MTLRenderCommandEncoder> encoder, int slot);

  int width() const { return width_; }
  int height() const { return height_; }
  int layers() const { return layers_; }

private:
  MetalTextureArray() = default;

  id<MTLDevice> device_ = nil;
  id<MTLTexture> texture_ = nil;
  id<MTLSamplerState> sampler_ = nil;
  int width_ = 0;
  int height_ = 0;
  int layers_ = 0;
};

class MetalInstanceBuffer : public MetalResource {
public:
  static std::unique_ptr<MetalInstanceBuffer> create(id<MTLDevice> device,
                                                     size_t max_instances);

  ~MetalInstanceBuffer();

  void set_instances(const std::vector<InstanceData> &instances);
  void update_instance(size_t index, const InstanceData &data);

  void bind(id<MTLRenderCommandEncoder> encoder);

  size_t instance_count() const { return instance_count_; }
  size_t max_instances() const { return max_instances_; }

  void compute_gpu_culling(id<MTLComputeCommandEncoder> encoder,
                           const Camera &camera, int viewport_width,
                           int viewport_height);

  uint32_t get_visible_count() const;

private:
  MetalInstanceBuffer() = default;

  id<MTLDevice> device_ = nil;
  id<MTLBuffer> instance_buffer_ = nil;
  id<MTLBuffer> visible_indices_buffer_ = nil;
  id<MTLBuffer> draw_arguments_buffer_ = nil;

  id<MTLComputePipelineState> culling_pipeline_ = nil;

  size_t instance_count_ = 0;
  size_t max_instances_ = 0;

  std::vector<InstanceData> cpu_instance_data_;
  bool needs_update_ = false;
};

class MetalLODMesh : public MetalResource {
public:
  static std::unique_ptr<MetalLODMesh>
  create(id<MTLDevice> device, const MetalMesh &high_detail,
         const MetalMesh &medium_detail, const MetalMesh &low_detail,
         size_t max_instances_per_lod, const LODConfig &config);

  ~MetalLODMesh();

  void set_instances(const std::vector<InstanceData> &instances);
  void compute_lod_distribution(id<MTLComputeCommandEncoder> encoder,
                                const Camera &camera, int viewport_width,
                                int viewport_height);

  void draw_all_lods(id<MTLRenderCommandEncoder> encoder);

  LODMesh::LODStats get_stats() const;

private:
  MetalLODMesh() = default;

  id<MTLDevice> device_ = nil;

  std::array<std::unique_ptr<MetalInstanceBuffer>, 3> lod_buffers_;
  std::array<const MetalMesh *, 3> lod_meshes_;

  id<MTLComputePipelineState> lod_compute_pipeline_ = nil;
  id<MTLBuffer> source_instances_buffer_ = nil;
  id<MTLBuffer> lod_assignments_buffer_ = nil;
  id<MTLBuffer> lod_counters_buffer_ = nil;

  LODConfig config_;
  std::vector<InstanceData> source_instances_;
  size_t max_instances_per_lod_ = 0;

  mutable LODMesh::LODStats last_stats_;
};

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

  std::unique_ptr<MetalTextureArray> create_texture_array(int width, int height,
                                                          int layers);

  std::unique_ptr<MetalInstanceBuffer>
  create_instance_buffer(size_t max_instances);

  std::unique_ptr<MetalLODMesh> create_lod_mesh(const MetalMesh &high_detail,
                                                const MetalMesh &medium_detail,
                                                const MetalMesh &low_detail,
                                                size_t max_instances_per_lod,
                                                const LODConfig &config);

  void draw_mesh(const MetalMesh &mesh, const Vec3 &position,
                 const Vec3 &rotation, const Vec3 &scale, MetalShader *shader,
                 const Material &material);

  void draw_instanced(const MetalMesh &mesh, MetalInstanceBuffer &instances,
                      MetalShader *shader, const Material &material);

  void draw_lod(MetalLODMesh &lod_mesh, MetalShader *shader,
                const Material &material);

  id<MTLDevice> device() const { return device_; }
  id<MTLCommandQueue> command_queue() const { return command_queue_; }
  id<MTLRenderCommandEncoder> current_encoder() const {
    return render_encoder_;
  }

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
  id<MTLComputeCommandEncoder> compute_encoder_ = nil;
  MTLRenderPassDescriptor *render_pass_descriptor_ = nil;

  id<MTLTexture> depth_texture_ = nil;

  int viewport_width_ = 0;
  int viewport_height_ = 0;

  std::unique_ptr<MetalShader> default_shader_;
  std::unique_ptr<MetalShader> instanced_shader_;
  std::unique_ptr<MetalShader> sprite_shader_;

  std::vector<std::unique_ptr<MetalResource>> resources_;
};

} // namespace pixel::renderer3d::metal

#endif // __APPLE__
