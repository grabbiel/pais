// src/renderer3d/renderer.cpp (Updated for RHI)
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_fwd.hpp"
#include "pixel/rhi/rhi.hpp"
#include "pixel/platform/shader_loader.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

struct GLFWwindow;

namespace pixel::renderer3d {

struct DeviceCreationResult {
  rhi::Device *device = nullptr;
  GLFWwindow *window = nullptr;
  std::string backend_name;
};
DeviceCreationResult create_renderer_device(const platform::WindowSpec &spec);

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

  auto renderer = std::unique_ptr<Renderer>(new Renderer());

  try {
    // Use the device factory helper - it handles all backend selection logic
    auto result = create_renderer_device(spec);

    renderer->window_ = result.window;
    renderer->device_ = result.device;

    std::cout << "Renderer Backend: " << result.backend_name << std::endl;

  } catch (const std::exception &e) {
    glfwTerminate();
    throw std::runtime_error(std::string("Failed to initialize renderer: ") +
                             e.what());
  }

  renderer->setup_default_shaders();
  renderer->sprite_mesh_ = renderer->create_sprite_quad();

  return renderer;
}

Renderer::~Renderer() {
  if (device_) {
    delete device_;
    device_ = nullptr;
  }

  if (window_)
    glfwDestroyWindow(window_);
  glfwTerminate();
}

void Renderer::setup_default_shaders() {
  default_shader_ = load_shader("assets/shaders/default.vert",
                                "assets/shaders/default.frag");
  instanced_shader_ = load_shader("assets/shaders/instanced.vert",
                                  "assets/shaders/instanced.frag");
  sprite_shader_ = default_shader_; // Use same for sprites
}

void Renderer::begin_frame(const Color &clear_color) {
  auto *cmd = device_->getImmediate();
  cmd->begin();

  float clear[4] = {clear_color.r, clear_color.g, clear_color.b, clear_color.a};

  rhi::TextureHandle backbuffer{0};
  rhi::TextureHandle depth_buffer{0};

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
  std::memcpy(input_state_.prev_keys, input_state_.keys,
              sizeof(input_state_.keys));
  std::memcpy(input_state_.prev_mouse_buttons, input_state_.mouse_buttons,
              sizeof(input_state_.mouse_buttons));
  input_state_.prev_mouse_x = input_state_.mouse_x;
  input_state_.prev_mouse_y = input_state_.mouse_y;

  for (int key = 0; key < 512; ++key) {
    input_state_.keys[key] = (glfwGetKey(window_, key) == GLFW_PRESS);
  }

  for (int btn = 0; btn < 8; ++btn) {
    input_state_.mouse_buttons[btn] =
        (glfwGetMouseButton(window_, btn) == GLFW_PRESS);
  }

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

ShaderID Renderer::load_shader(const std::string &vert_path,
                               const std::string &frag_path) {
  ShaderID id = next_shader_id_++;
  shaders_[id] = Shader::create(device_, vert_path, frag_path);
  return id;
}

Shader *Renderer::get_shader(ShaderID id) {
  auto it = shaders_.find(id);
  return it != shaders_.end() ? it->second.get() : nullptr;
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

const char *Renderer::backend_name() const { return "OpenGL 3.3 Core"; }

} // namespace pixel::renderer3d
