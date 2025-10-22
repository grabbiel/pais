// src/rhi/backends/metal/metal_backend.hpp
#pragma once

#ifdef __APPLE__

#include "pixel/renderer3d/renderer.hpp"

#include <Metal/Metal.h>
#include <MetalKit/MetalKit.h>
#include <QuartzCore/CAMetalLayer.h>

#include <array>
#include <cstdint>
#include <condition_variable>
#include <dispatch/dispatch.h>
#include <functional>
#include <memory>
#include <mutex>
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
  friend class MetalBackend;

  void mark_used(uint64_t frame_id) const { last_used_frame_ = frame_id; }
  uint64_t last_used_frame() const { return last_used_frame_; }
  void mark_pending_destruction() const { pending_destruction_ = true; }
  bool pending_destruction() const { return pending_destruction_; }

private:
  mutable uint64_t last_used_frame_ = 0;
  mutable bool pending_destruction_ = false;
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

  void bind(id<MTLRenderCommandEncoder> encoder,
            id<MTLDepthStencilState> depth_stencil_state);

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
  create(id<MTLDevice> device, id<MTLCommandQueue> command_queue,
         const std::vector<Vertex> &vertices,
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

  void wait_for_resource(const MetalResource *resource);
  void defer_resource_destruction(std::unique_ptr<MetalResource> resource);

  id<MTLDevice> device() const { return device_; }
  id<MTLCommandQueue> command_queue() const { return command_queue_; }

  int viewport_width() const { return viewport_width_; }
  int viewport_height() const { return viewport_height_; }

private:
  struct FrameContext;
  MetalBackend() = default;

  bool initialize(GLFWwindow *window);
  void create_default_shaders();
  bool setup_render_pass_descriptor();
  void log_nserror(const std::string &context, NSError *error) const;
  void on_command_buffer_complete(id<MTLCommandBuffer> command_buffer,
                                  struct FrameContext *frame_context);
  void track_inflight_command_buffer(id<MTLCommandBuffer> command_buffer);
  std::string describe_command_buffer(id<MTLCommandBuffer> command_buffer) const;
  void mark_resource_in_use(const MetalResource *resource);
  void enqueue_completion_task(std::function<void()> callback);
  void abort_frame(struct FrameContext &context);

  static constexpr size_t kMaxFramesInFlight = 3;

  struct FrameContext {
    dispatch_semaphore_t fence = nullptr;
    uint64_t frame_id = 0;
    bool active = false;
    std::vector<std::function<void()>> completion_callbacks;
  };

  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> command_queue_ = nil;
  CAMetalLayer *metal_layer_ = nil;

  id<CAMetalDrawable> current_drawable_ = nil;
  id<MTLCommandBuffer> command_buffer_ = nil;
  id<MTLRenderCommandEncoder> render_encoder_ = nil;
  MTLRenderPassDescriptor *render_pass_descriptor_ = nil;

  id<MTLTexture> depth_texture_ = nil;

  dispatch_semaphore_t inflight_semaphore_ = nullptr;
  NSMutableArray<id<MTLCommandBuffer>> *inflight_command_buffers_ = nil;
  mutable std::mutex inflight_mutex_;
  uint64_t frame_counter_ = 0;
  uint64_t current_frame_id_ = 0;
  uint64_t last_completed_frame_id_ = 0;
  bool validation_enabled_ = false;

  int viewport_width_ = 0;
  int viewport_height_ = 0;

  std::array<FrameContext, kMaxFramesInFlight> frame_contexts_;
  FrameContext *current_frame_context_ = nullptr;
  std::mutex frame_mutex_;

  std::unordered_map<const MetalResource *, uint64_t> resource_last_usage_;
  std::condition_variable frame_completion_cv_;
  std::mutex resource_mutex_;
  struct DeferredResource {
    uint64_t frame_id = 0;
    std::unique_ptr<MetalResource> resource;
  };
  std::vector<DeferredResource> deferred_destruction_queue_;

  struct DepthStencilStateKey {
    bool depth_test{true};
    bool depth_write{true};
    rhi::CompareOp depth_compare{rhi::CompareOp::Less};
    bool stencil_enable{false};
    rhi::CompareOp stencil_compare{rhi::CompareOp::Always};
    rhi::StencilOp stencil_fail_op{rhi::StencilOp::Keep};
    rhi::StencilOp stencil_depth_fail_op{rhi::StencilOp::Keep};
    rhi::StencilOp stencil_pass_op{rhi::StencilOp::Keep};
    uint32_t stencil_read_mask{0xFF};
    uint32_t stencil_write_mask{0xFF};

    bool operator==(const DepthStencilStateKey &other) const {
      return depth_test == other.depth_test &&
             depth_write == other.depth_write &&
             depth_compare == other.depth_compare &&
             stencil_enable == other.stencil_enable &&
             stencil_compare == other.stencil_compare &&
             stencil_fail_op == other.stencil_fail_op &&
             stencil_depth_fail_op == other.stencil_depth_fail_op &&
             stencil_pass_op == other.stencil_pass_op &&
             stencil_read_mask == other.stencil_read_mask &&
             stencil_write_mask == other.stencil_write_mask;
    }
  };

  struct DepthStencilStateKeyHash {
    std::size_t operator()(const DepthStencilStateKey &key) const noexcept {
      std::size_t seed = 0;
      auto hash_combine = [&seed](std::size_t value) {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
      };
      hash_combine(std::hash<bool>{}(key.depth_test));
      hash_combine(std::hash<bool>{}(key.depth_write));
      hash_combine(std::hash<int>{}(static_cast<int>(key.depth_compare)));
      hash_combine(std::hash<bool>{}(key.stencil_enable));
      hash_combine(std::hash<int>{}(static_cast<int>(key.stencil_compare)));
      hash_combine(std::hash<int>{}(static_cast<int>(key.stencil_fail_op)));
      hash_combine(
          std::hash<int>{}(static_cast<int>(key.stencil_depth_fail_op)));
      hash_combine(std::hash<int>{}(static_cast<int>(key.stencil_pass_op)));
      hash_combine(std::hash<uint32_t>{}(key.stencil_read_mask));
      hash_combine(std::hash<uint32_t>{}(key.stencil_write_mask));
      return seed;
    }
  };

  DepthStencilStateKey make_depth_stencil_key(const Material &material) const;
  id<MTLDepthStencilState>
  get_depth_stencil_state(const DepthStencilStateKey &key);

  std::unordered_map<DepthStencilStateKey, id<MTLDepthStencilState>,
                     DepthStencilStateKeyHash>
      depth_stencil_states_;

  std::unique_ptr<MetalShader> default_shader_;
  std::vector<std::unique_ptr<MetalResource>> resources_;
};

} // namespace pixel::renderer3d::metal

#endif // __APPLE__
