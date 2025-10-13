#pragma once
#include "../platform/platform.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Platform-specific OpenGL headers (no GLAD)
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#include <GL/gl.h>
#include <windows.h>
// Windows needs extension definitions
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#endif
#else
// Linux
#include <GL/gl.h>
#include <GL/glext.h>
#endif

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
// Input State
// ============================================================================

struct InputState {
  bool keys[512] = {false};
  bool mouse_buttons[8] = {false};
  double mouse_x = 0.0;
  double mouse_y = 0.0;
  double mouse_delta_x = 0.0;
  double mouse_delta_y = 0.0;
  double scroll_delta = 0.0;

  bool key_pressed(int key) const { return key >= 0 && key < 512 && keys[key]; }
  bool mouse_pressed(int button) const {
    return button >= 0 && button < 8 && mouse_buttons[button];
  }
};

enum Key {
  KEY_ESCAPE = 256,
  KEY_ENTER = 257,
  KEY_TAB = 258,
  KEY_BACKSPACE = 259,
  KEY_SPACE = 32,
  KEY_LEFT = 263,
  KEY_RIGHT = 262,
  KEY_UP = 265,
  KEY_DOWN = 264,
  KEY_W = 87,
  KEY_A = 65,
  KEY_S = 83,
  KEY_D = 68,
  KEY_R = 82,
  KEY_1 = 49,
  KEY_2 = 50,
  KEY_0 = 48
};

// ============================================================================
// Shader Management
// ============================================================================

using ShaderID = uint32_t;
constexpr ShaderID INVALID_SHADER = 0;

class Shader {
public:
  static std::unique_ptr<Shader> create(const std::string &vert_src,
                                        const std::string &frag_src);
  ~Shader();

  void bind() const;
  void unbind() const;

  void set_int(const std::string &name, int value);
  void set_float(const std::string &name, float value);
  void set_vec2(const std::string &name, const Vec2 &value);
  void set_vec3(const std::string &name, const Vec3 &value);
  void set_vec4(const std::string &name, const Vec4 &value);
  void set_mat4(const std::string &name, const float *value);

  uint32_t program() const { return program_; }

private:
  Shader() = default;
  uint32_t program_ = 0;
};

// ============================================================================
// Texture Management
// ============================================================================

using TextureID = uint32_t;
constexpr TextureID INVALID_TEXTURE = 0;

struct TextureInfo {
  int width = 0;
  int height = 0;
  uint32_t gl_id = 0;
};

// ============================================================================
// Mesh / Geometry
// ============================================================================

struct Vertex {
  Vec3 position;
  Vec3 normal;
  Vec2 texcoord;
  Color color;
};

class Mesh {
public:
  static std::unique_ptr<Mesh> create(const std::vector<Vertex> &vertices,
                                      const std::vector<uint32_t> &indices);
  ~Mesh();

  void draw() const;

  size_t vertex_count() const { return vertex_count_; }
  size_t index_count() const { return index_count_; }

private:
  Mesh() = default;
  uint32_t vao_ = 0;
  uint32_t vbo_ = 0;
  uint32_t ebo_ = 0;
  size_t vertex_count_ = 0;
  size_t index_count_ = 0;
};

// ============================================================================
// Camera (2.5D)
// ============================================================================

class Camera {
public:
  enum class ProjectionMode { Perspective, Orthographic };

  Vec3 position{0, 5, 10};
  Vec3 target{0, 0, 0};
  Vec3 up{0, 1, 0};

  ProjectionMode mode = ProjectionMode::Perspective;
  float fov = 45.0f;
  float ortho_size = 10.0f;
  float near_clip = 0.1f;
  float far_clip = 100.0f;

  Camera() = default;

  void get_view_matrix(float *out_mat4) const;
  void get_projection_matrix(float *out_mat4, int screen_w, int screen_h) const;

  void orbit(float dx, float dy);
  void pan(float dx, float dy);
  void zoom(float delta);
};

// ============================================================================
// Material
// ============================================================================

struct Material {
  Color ambient{0.2f, 0.2f, 0.2f, 1.0f};
  Color diffuse{0.8f, 0.8f, 0.8f, 1.0f};
  Color specular{1.0f, 1.0f, 1.0f, 1.0f};
  float shininess = 32.0f;
  TextureID texture = INVALID_TEXTURE;
};

// ============================================================================
// Main Renderer
// ============================================================================

class Renderer {
public:
  static std::unique_ptr<Renderer> create(const pixel::platform::WindowSpec &);
  ~Renderer();

  void begin_frame(const Color &clear_color = Color::Black());
  void end_frame();

  bool process_events();

  const InputState &input() const { return input_state_; }

  ShaderID load_shader(const std::string &vert_path,
                       const std::string &frag_path);
  ShaderID create_shader_from_source(const std::string &vert_src,
                                     const std::string &frag_src);
  Shader *get_shader(ShaderID id);

  TextureID load_texture(const std::string &path);
  TextureID create_texture(int width, int height, const uint8_t *data);
  void bind_texture(TextureID id, int slot = 0);
  TextureInfo get_texture_info(TextureID id) const;

  std::unique_ptr<Mesh> create_quad(float size = 1.0f);
  std::unique_ptr<Mesh> create_cube(float size = 1.0f);
  std::unique_ptr<Mesh> create_plane(float width, float depth,
                                     int segments = 1);
  std::unique_ptr<Mesh> create_sprite_quad();

  void draw_mesh(const Mesh &mesh, const Vec3 &position, const Vec3 &rotation,
                 const Vec3 &scale, const Material &material);

  void draw_sprite(TextureID texture, const Vec3 &position, const Vec2 &size,
                   const Color &tint = Color::White());

  Camera &camera() { return camera_; }
  const Camera &camera() const { return camera_; }

  int window_width() const;
  int window_height() const;
  double time() const;

  ShaderID default_shader() const { return default_shader_; }
  ShaderID sprite_shader() const { return sprite_shader_; }

private:
  Renderer() = default;
  void setup_default_shaders();
  void update_input_state();
  void load_gl_functions(); // Load OpenGL function pointers on Windows

  GLFWwindow *window_ = nullptr;

  InputState input_state_;
  double last_mouse_x_ = 0.0;
  double last_mouse_y_ = 0.0;

  std::unordered_map<ShaderID, std::unique_ptr<Shader>> shaders_;
  ShaderID next_shader_id_ = 1;
  ShaderID default_shader_ = INVALID_SHADER;
  ShaderID sprite_shader_ = INVALID_SHADER;

  std::unordered_map<std::string, TextureID> texture_path_to_id_;
  std::unordered_map<TextureID, TextureInfo> textures_;
  TextureID next_texture_id_ = 1;

  std::unique_ptr<Mesh> sprite_mesh_;

  Camera camera_;
};

namespace primitives {
std::vector<Vertex> create_quad_vertices(float size);
std::vector<Vertex> create_cube_vertices(float size);
std::vector<Vertex> create_plane_vertices(float width, float depth, int segs);
} // namespace primitives

} // namespace pixel::renderer3d
