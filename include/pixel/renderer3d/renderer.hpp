#pragma once
#include "../platform/platform.hpp"
#include "../rhi/rhi.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace pixel::renderer3d {

// ============================================================================
// Math Types
// ============================================================================

struct Vec2 {
  float x, y;
  Vec2() : x(0), y(0) {}
  Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3 {
  float x, y, z;
  Vec3() : x(0), y(0), z(0) {}
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  Vec3(const glm::vec3 &v) : x(v.x), y(v.y), z(v.z) {}
  glm::vec3 to_glm() const;
  static Vec3 from_glm(const glm::vec3 &v) { return {v.x, v.y, v.z}; }
  Vec3 operator-(const Vec3 &other) const;
  Vec3 operator+(const Vec3 &other) const;
  Vec3 normalized() const;
  Vec3 operator*(float scalar) const;
  friend Vec3 operator*(float scalar, const Vec3 &v);
  float length() const;
};

struct Vec4 {
  float x, y, z, w;
  Vec4() : x(0), y(0), z(0), w(1) {}
  Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct Color {
  float r, g, b, a;
  Color() : r(1), g(1), b(1), a(1) {}
  Color(float r_, float g_, float b_, float a_ = 1.0f)
      : r(r_), g(g_), b(b_), a(a_) {}
  static Color White() { return {1, 1, 1, 1}; }
  static Color Black() { return {0, 0, 0, 1}; }
  static Color Red() { return {1, 0, 0, 1}; }
  static Color Green() { return {0, 1, 0, 1}; }
  static Color Blue() { return {0, 0, 1, 1}; }
};

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
// Input State
// ============================================================================

struct InputState {
  bool keys[512] = {false};
  bool prev_keys[512] = {false};
  bool mouse_buttons[8] = {false};
  bool prev_mouse_buttons[8] = {false};
  double mouse_x = 0.0;
  double mouse_y = 0.0;
  double prev_mouse_x = 0.0;
  double prev_mouse_y = 0.0;
  double mouse_delta_x = 0.0;
  double mouse_delta_y = 0.0;
  double scroll_delta = 0.0;

  bool key_pressed(int key) const {
    return key >= 0 && key < 512 && keys[key] && !prev_keys[key];
  }
  bool key_down(int key) const { return key >= 0 && key < 512 && keys[key]; }
  bool key_released(int key) const {
    return key >= 0 && key < 512 && !keys[key] && prev_keys[key];
  }

  bool mouse_pressed(int button) const {
    return button >= 0 && button < 8 && mouse_buttons[button] &&
           !prev_mouse_buttons[button];
  }
  bool mouse_down(int button) const {
    return button >= 0 && button < 8 && mouse_buttons[button];
  }
  bool mouse_released(int button) const {
    return button >= 0 && button < 8 && !mouse_buttons[button] &&
           prev_mouse_buttons[button];
  }
};

// ============================================================================
// Material
// ============================================================================

struct Material {
  rhi::TextureHandle texture{0};
  rhi::TextureHandle texture_array{0};
  Color color = Color::White();
  float roughness = 0.5f;
  float metallic = 0.0f;
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
                                        const std::string &vert_src,
                                        const std::string &frag_src);
  ~Shader() = default;

  rhi::PipelineHandle pipeline() const { return pipeline_; }

private:
  Shader() = default;
  rhi::PipelineHandle pipeline_{0};
  rhi::ShaderHandle vs_{0};
  rhi::ShaderHandle fs_{0};
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

  bool process_events();

  const InputState &input() const { return input_state_; }

  ShaderID load_shader(const std::string &vert_path,
                       const std::string &frag_path);
  ShaderID create_shader_from_source(const std::string &vert_src,
                                     const std::string &frag_src);
  Shader *get_shader(ShaderID id);

  rhi::TextureHandle load_texture(const std::string &path);
  rhi::TextureHandle create_texture(int width, int height, const uint8_t *data);

  rhi::TextureHandle create_texture_array(int width, int height, int layers);
  rhi::TextureHandle load_texture_array(const std::vector<std::string> &paths);
  void set_texture_array_layer(rhi::TextureHandle array_id, int layer,
                               const uint8_t *data);

  std::unique_ptr<Mesh> create_quad(float size = 1.0f);
  std::unique_ptr<Mesh> create_cube(float size = 1.0f);
  std::unique_ptr<Mesh> create_plane(float width, float depth,
                                     int segments = 1);
  std::unique_ptr<Mesh> create_sprite_quad();

  virtual void draw_mesh(const Mesh &mesh, const Vec3 &position,
                         const Vec3 &rotation, const Vec3 &scale,
                         const Material &material);

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

protected:
  Renderer() = default;
  void setup_default_shaders();
  void update_input_state();

  struct GLFWwindow *window_ = nullptr;
  rhi::Device *device_ = nullptr;

  InputState input_state_;
  double last_mouse_x_ = 0.0;
  double last_mouse_y_ = 0.0;

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
};

namespace primitives {
std::vector<Vertex> create_quad_vertices(float size);
std::vector<Vertex> create_cube_vertices(float size);
std::vector<Vertex> create_plane_vertices(float width, float depth, int segs);
} // namespace primitives

} // namespace pixel::renderer3d
