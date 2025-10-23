#pragma once
#include "pixel/platform/platform.hpp"
#include "pixel/rhi/rhi.hpp"
#include "pixel/renderer3d/shader_reflection.hpp"
#include "pixel/math/math.hpp"
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pixel::platform {
  class Window;
}

namespace pixel::renderer3d {

// ============================================================================
// Math Types (imported from pixel::math)
// ============================================================================

// Type aliases for backward compatibility
using Vec2 = pixel::math::Vec2;
using Vec3 = pixel::math::Vec3;
using Vec4 = pixel::math::Vec4;
using Color = pixel::math::Color;

// ============================================================================
// Camera
// ============================================================================

// ============================================================================
// Camera
// ============================================================================

class Camera {
public:
  enum class ProjectionMode { Perspective, Orthographic };

  Vec3 position{0, 2, 5};
  Vec3 target{0, 0, 0};
  Vec3 up{0, 1, 0};

  ProjectionMode mode = ProjectionMode::Perspective;

  // Perspective parameters
  float fov = 60.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;

  // Orthographic parameters
  float ortho_size = 10.0f;

  void get_view_matrix(float *out) const;
  void get_projection_matrix(float *out, int width, int height) const;

  // Camera controls
  void orbit(float dx, float dy);
  void pan(float dx, float dy);
  void zoom(float delta);
};

// ============================================================================
// Shader Variants
// ============================================================================

class ShaderVariantKey {
public:
  using DefineMap = std::map<std::string, std::string, std::less<>>;

  ShaderVariantKey() = default;

  void set_define(std::string name, std::string value = "1");
  void clear_define(std::string_view name);
  bool has_define(std::string_view name) const;
  bool empty() const { return defines_.empty(); }
  const DefineMap &defines() const { return defines_; }
  std::string cache_key() const;

  static ShaderVariantKey from_defines(
      std::initializer_list<std::pair<std::string, std::string>> defines);

private:
  DefineMap defines_{};
};

// ============================================================================
// Material
// ============================================================================

struct Material {
  enum class BlendMode : uint8_t {
    Alpha = 0,
    Additive = 1,
    Multiply = 2,
    Opaque = 3,
  };

  static constexpr size_t kBlendModeCount = 4;

  rhi::TextureHandle texture{0};
  rhi::TextureHandle texture_array{0};
  Color color = Color::White();
  float roughness = 0.5f;
  float metallic = 0.0f;
  BlendMode blend_mode = BlendMode::Alpha;
  ShaderVariantKey shader_variant{};
  bool depth_test = true;
  bool depth_write = true;
  rhi::CompareOp depth_compare = rhi::CompareOp::Less;
  bool depth_bias_enable = false;
  float depth_bias_constant = 0.0f;
  float depth_bias_slope = 0.0f;
  bool stencil_enable = false;
  rhi::CompareOp stencil_compare = rhi::CompareOp::Always;
  rhi::StencilOp stencil_fail_op = rhi::StencilOp::Keep;
  rhi::StencilOp stencil_depth_fail_op = rhi::StencilOp::Keep;
  rhi::StencilOp stencil_pass_op = rhi::StencilOp::Keep;
  uint32_t stencil_read_mask = 0xFF;
  uint32_t stencil_write_mask = 0xFF;
  uint32_t stencil_reference = 0;
};

// ============================================================================
// Vertex Structure
// ============================================================================

struct Vertex {
  Vec3 position;
  Vec3 normal;
  Vec2 texcoord;
  Color color;
};

// ============================================================================
// Mesh
// ============================================================================

class Mesh {
public:
  static std::unique_ptr<Mesh> create(rhi::Device *device,
                                      const std::vector<Vertex> &vertices,
                                      const std::vector<uint32_t> &indices);
  ~Mesh();

  rhi::BufferHandle vertex_buffer() const { return vertex_buffer_; }
  rhi::BufferHandle index_buffer() const { return index_buffer_; }

  size_t vertex_count() const { return vertex_count_; }
  size_t index_count() const { return index_count_; }

  const std::vector<Vertex> &vertices() const { return vertices_; }
  const std::vector<uint32_t> &indices() const { return indices_; }

private:
  Mesh() = default;

  rhi::BufferHandle vertex_buffer_{0};
  rhi::BufferHandle index_buffer_{0};
  size_t vertex_count_ = 0;
  size_t index_count_ = 0;

  std::vector<Vertex> vertices_;
  std::vector<uint32_t> indices_;
};

// ============================================================================
// Shader Management
// ============================================================================

using ShaderID = uint32_t;
constexpr ShaderID INVALID_SHADER = 0;

class Shader {
public:
  static std::unique_ptr<Shader> create(rhi::Device *device,
                                        const std::string &vert_path,
                                        const std::string &frag_path);
  ~Shader() = default;

  rhi::PipelineHandle pipeline(Material::BlendMode mode) const;
  rhi::PipelineHandle pipeline(const ShaderVariantKey &variant,
                               Material::BlendMode mode) const;

private:
  Shader() = default;
  struct VariantData {
    std::array<rhi::PipelineHandle, Material::kBlendModeCount> pipelines{};
    rhi::ShaderHandle vs{0};
    rhi::ShaderHandle fs{0};
    ShaderReflection reflection{};
  };

  VariantData &get_or_create_variant(const ShaderVariantKey &variant) const;
  VariantData build_variant(const ShaderVariantKey &variant) const;

  rhi::Device *device_{nullptr};
  std::string vert_source_;
  std::string frag_source_;
  std::string vs_stage_;
  std::string fs_stage_;
  mutable std::unordered_map<std::string, VariantData> variant_cache_;

public:
  const ShaderReflection &reflection() const;
  const ShaderReflection &reflection(const ShaderVariantKey &variant) const;
};

// ============================================================================
// Renderer
// ============================================================================

class Renderer {
public:
  static std::unique_ptr<Renderer> create(const platform::WindowSpec &spec);
  virtual ~Renderer();

  virtual void begin_frame(const Color &clear_color = Color::Black());
  virtual void end_frame();

  // Render pass control helpers - used when compute workloads need to run
  // mid-frame (e.g. GPU-driven LOD selection)
  void pause_render_pass();
  void resume_render_pass();
  bool render_pass_active() const { return render_pass_active_; }

  bool process_events();

  ShaderID load_shader(const std::string &vert_path,
                       const std::string &frag_path);
  Shader *get_shader(ShaderID id);

  rhi::TextureHandle load_texture(const std::string &path);
  rhi::TextureHandle create_texture(int width, int height, const uint8_t *data);

  rhi::TextureHandle create_texture_array(int width, int height, int layers);
  rhi::TextureHandle load_texture_array(const std::vector<std::string> &paths);
  void set_texture_array_layer(rhi::TextureHandle array_id, int layer,
                               int width, int height, const uint8_t *data);

  std::unique_ptr<Mesh> create_quad(float size = 1.0f);
  std::unique_ptr<Mesh> create_cube(float size = 1.0f);
  std::unique_ptr<Mesh> create_plane(float width, float depth,
                                     int segments = 1);
  std::unique_ptr<Mesh> create_sprite_quad();

  virtual void draw_mesh(const Mesh &mesh, const Vec3 &position,
                         const Vec3 &rotation, const Vec3 &scale,
                         const Material &material);
  void apply_material_state(rhi::CmdList *cmd,
                            const Material &material) const;

  void draw_sprite(rhi::TextureHandle texture, const Vec3 &position,
                   const Vec2 &size, const Color &tint = Color::White());

  Camera &camera() { return camera_; }
  const Camera &camera() const { return camera_; }

  int window_width() const;
  int window_height() const;
  double time() const;

  ShaderID default_shader() const { return default_shader_; }
  ShaderID sprite_shader() const { return sprite_shader_; }
  ShaderID instanced_shader() const { return instanced_shader_; }

  const char *backend_name() const;

  rhi::Device *device() { return device_; }
  const rhi::Device *device() const { return device_; }

  platform::Window *window() { return window_; }
  const platform::Window *window() const { return window_; }

protected:
  Renderer() = default;
  void setup_default_shaders();

  platform::Window *window_ = nullptr;
  rhi::Device *device_ = nullptr;

  std::unordered_map<ShaderID, std::unique_ptr<Shader>> shaders_;
  ShaderID next_shader_id_ = 1;
  ShaderID default_shader_ = INVALID_SHADER;
  ShaderID sprite_shader_ = INVALID_SHADER;
  ShaderID instanced_shader_ = INVALID_SHADER;

  std::unordered_map<std::string, rhi::TextureHandle> texture_path_to_id_;
  std::unordered_map<uint32_t, rhi::TextureHandle> textures_;
  uint32_t next_texture_id_ = 1;

  std::unique_ptr<Mesh> sprite_mesh_;

  Camera camera_;

  rhi::RenderPassDesc current_pass_desc_{};
  bool render_pass_active_ = false;
};

namespace primitives {
std::vector<Vertex> create_quad_vertices(float size);
std::vector<Vertex> create_cube_vertices(float size);
std::vector<Vertex> create_plane_vertices(float width, float depth, int segs);
} // namespace primitives

} // namespace pixel::renderer3d
