#include "pixel/renderer3d/renderer.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

namespace pixel::renderer3d {

static void glfw_error_callback(int error, const char *description) {
  std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

// ============================================================================
// Renderer Implementation
// ============================================================================

std::unique_ptr<Renderer>
Renderer::create(const pixel::platform::WindowSpec &spec) {
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit())
    throw std::runtime_error("Failed to initialize GLFW");

  // Request no specific API - we'll use the RHI
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  auto renderer = std::unique_ptr<Renderer>(new Renderer());

  renderer->window_ =
      glfwCreateWindow(spec.w, spec.h, spec.title.c_str(), nullptr, nullptr);
  if (!renderer->window_) {
    glfwTerminate();
    throw std::runtime_error("Failed to create GLFW window");
  }

  // Create RHI device
  // Check for Metal preference on macOS
  bool prefer_metal = false;
#ifdef __APPLE__
  const char *backend_env = std::getenv("PIXEL_RENDERER_BACKEND");
  prefer_metal = !(backend_env && std::strcmp(backend_env, "OPENGL") == 0);
#endif

  renderer->device_ = rhi::create_device_from_config(prefer_metal);
  if (!renderer->device_) {
    glfwDestroyWindow(renderer->window_);
    glfwTerminate();
    throw std::runtime_error("Failed to create RHI device");
  }

  std::cout << "RHI Backend: " << renderer->backend_name() << std::endl;

  renderer->setup_default_shaders();
  renderer->sprite_mesh_ = renderer->create_sprite_quad();

  return renderer;
}

Renderer::~Renderer() {
  if (window_)
    glfwDestroyWindow(window_);
  glfwTerminate();

  // Device cleanup handled by RHI
}

void Renderer::begin_frame(const Color &clear_color) {
  auto *cmd = device_->getImmediate();
  cmd->begin();

  float clear[4] = {clear_color.r, clear_color.g, clear_color.b, clear_color.a};

  // For now, we'll need a way to get the backbuffer texture handle
  // This would be provided by the swapchain/device
  rhi::TextureHandle backbuffer{0};   // Placeholder
  rhi::TextureHandle depth_buffer{0}; // Placeholder

  cmd->beginRender(backbuffer, depth_buffer, clear, 1.0f, 0);
}

void Renderer::end_frame() {
  auto *cmd = device_->getImmediate();
  cmd->endRender();
  cmd->end();

  device_->present();
}

bool Renderer::process_events() {
  glfwPollEvents();
  update_input_state();
  return !glfwWindowShouldClose(window_);
}

void Renderer::update_input_state() {
  // Copy current state to previous state
  std::memcpy(input_state_.prev_keys, input_state_.keys,
              sizeof(input_state_.keys));
  std::memcpy(input_state_.prev_mouse_buttons, input_state_.mouse_buttons,
              sizeof(input_state_.mouse_buttons));
  input_state_.prev_mouse_x = input_state_.mouse_x;
  input_state_.prev_mouse_y = input_state_.mouse_y;

  // Update current key states
  for (int key = 0; key < 512; ++key) {
    input_state_.keys[key] = (glfwGetKey(window_, key) == GLFW_PRESS);
  }

  // Update current mouse button states
  for (int btn = 0; btn < 8; ++btn) {
    input_state_.mouse_buttons[btn] =
        (glfwGetMouseButton(window_, btn) == GLFW_PRESS);
  }

  // Update mouse position and delta
  double x, y;
  glfwGetCursorPos(window_, &x, &y);
  input_state_.mouse_delta_x = x - last_mouse_x_;
  input_state_.mouse_delta_y = y - last_mouse_y_;
  input_state_.mouse_x = x;
  input_state_.mouse_y = y;
  last_mouse_x_ = x;
  last_mouse_y_ = y;

  input_state_.scroll_delta = 0.0;
}

// ============================================================================
// Shader Management & Drawing
// ============================================================================

void Renderer::setup_default_shaders() {
  // Default 3D shader source (simplified for RHI)
  std::string vert_3d = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aNormal;
    layout (location = 2) in vec2 aTexCoord;
    layout (location = 3) in vec4 aColor;
    
    out vec3 FragPos;
    out vec3 Normal;
    out vec2 TexCoord;
    out vec4 Color;
    
    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;
    
    void main() {
      FragPos = vec3(model * vec4(aPos, 1.0));
      Normal = mat3(transpose(inverse(model))) * aNormal;
      TexCoord = aTexCoord;
      Color = aColor;
      gl_Position = projection * view * vec4(FragPos, 1.0);
    }
  )";

  std::string frag_3d = R"(
    #version 330 core
    out vec4 FragColor;
    
    in vec3 FragPos;
    in vec3 Normal;
    in vec2 TexCoord;
    in vec4 Color;
    
    uniform sampler2D uTexture;
    uniform bool useTexture;
    uniform vec3 lightPos;
    uniform vec3 viewPos;
    
    void main() {
      vec3 lightColor = vec3(1.0, 1.0, 1.0);
      float ambientStrength = 0.3;
      vec3 ambient = ambientStrength * lightColor;
      vec3 norm = normalize(Normal);
      vec3 lightDir = normalize(lightPos - FragPos);
      float diff = max(dot(norm, lightDir), 0.0);
      vec3 diffuse = diff * lightColor;
      vec4 texColor = useTexture ? texture(uTexture, TexCoord) : Color;
      vec3 result = (ambient + diffuse) * texColor.rgb;
      FragColor = vec4(result, texColor.a);
    }
  )";

  default_shader_ = create_shader_from_source(vert_3d, frag_3d);

  // Similar setup for instanced and sprite shaders...
  instanced_shader_ = default_shader_; // Placeholder
  sprite_shader_ = default_shader_;    // Placeholder
}

ShaderID Renderer::create_shader_from_source(const std::string &vert_src,
                                             const std::string &frag_src) {
  ShaderID id = next_shader_id_++;
  shaders_[id] = Shader::create(device_, vert_src, frag_src);
  return id;
}

Shader *Renderer::get_shader(ShaderID id) {
  auto it = shaders_.find(id);
  return it != shaders_.end() ? it->second.get() : nullptr;
}

void Renderer::draw_mesh(const Mesh &mesh, const Vec3 &position,
                         const Vec3 &rotation, const Vec3 &scale,
                         const Material &material) {
  Shader *shader = get_shader(default_shader_);
  if (!shader)
    return;

  auto *cmd = device_->getImmediate();

  cmd->setPipeline(shader->pipeline());
  cmd->setVertexBuffer(mesh.vertex_buffer());
  cmd->setIndexBuffer(mesh.index_buffer());

  // TODO: Set uniforms (model, view, projection matrices)
  // This requires extending the RHI with uniform buffer support

  cmd->drawIndexed(mesh.index_count(), 0, 1);
}

void Renderer::draw_sprite(rhi::TextureHandle texture, const Vec3 &position,
                           const Vec2 &size, const Color &tint) {
  Shader *shader = get_shader(sprite_shader_);
  if (!shader)
    return;

  // Similar to draw_mesh but for sprites
  auto *cmd = device_->getImmediate();
  cmd->setPipeline(shader->pipeline());

  if (sprite_mesh_) {
    cmd->setVertexBuffer(sprite_mesh_->vertex_buffer());
    cmd->setIndexBuffer(sprite_mesh_->index_buffer());
    cmd->drawIndexed(sprite_mesh_->index_count(), 0, 1);
  }
}

std::unique_ptr<Mesh> Renderer::create_sprite_quad() {
  std::vector<Vertex> verts = {
      {{-0.5f, -0.5f, 0}, {0, 0, 1}, {0, 0}, Color::White()},
      {{0.5f, -0.5f, 0}, {0, 0, 1}, {1, 0}, Color::White()},
      {{0.5f, 0.5f, 0}, {0, 0, 1}, {1, 1}, Color::White()},
      {{-0.5f, 0.5f, 0}, {0, 0, 1}, {0, 1}, Color::White()}};
  std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
  return Mesh::create(device_, verts, indices);
}

std::unique_ptr<Mesh> Renderer::create_cube(float size) {
  std::vector<Vertex> verts = primitives::create_cube_vertices(size);
  std::vector<uint32_t> indices;
  for (uint32_t i = 0; i < verts.size(); ++i)
    indices.push_back(i);
  return Mesh::create(device_, verts, indices);
}

std::unique_ptr<Mesh> Renderer::create_plane(float w, float d, int segs) {
  std::vector<Vertex> verts = primitives::create_plane_vertices(w, d, segs);
  std::vector<uint32_t> indices;
  for (uint32_t i = 0; i < verts.size(); ++i)
    indices.push_back(i);
  return Mesh::create(device_, verts, indices);
}

int Renderer::window_width() const {
  int w;
  glfwGetWindowSize(window_, &w, nullptr);
  return w;
}

int Renderer::window_height() const {
  int h;
  glfwGetWindowSize(window_, nullptr, &h);
  return h;
}

double Renderer::time() const { return glfwGetTime(); }

const char *Renderer::backend_name() const {
  // This would be queried from the RHI device
  return "RHI Backend";
}

} // namespace pixel::renderer3d
