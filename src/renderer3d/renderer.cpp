// src/renderer3d/renderer.cpp (Updated for RHI)
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_fwd.hpp"
#include "pixel/rhi/rhi.hpp"
#include "pixel/resources/texture_loader.hpp"
#include "pixel/platform/shader_loader.hpp"
#include "pixel/platform/window.hpp"
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

namespace pixel::renderer3d {

// ============================================================================
// Renderer Implementation
// ============================================================================

std::unique_ptr<Renderer>
Renderer::create(const pixel::platform::WindowSpec &spec) {
  auto renderer = std::unique_ptr<Renderer>(new Renderer());

  try {
    // Step 1: Create the Window with appropriate Graphics API
#ifdef PIXEL_USE_METAL
    auto window = platform::Window::create(spec, platform::GraphicsAPI::Metal);
    std::cout << "Created Window with Metal API" << std::endl;
#else
    auto window = platform::Window::create(spec, platform::GraphicsAPI::OpenGL);
    std::cout << "Created Window with OpenGL API" << std::endl;
#endif

    if (!window) {
      throw std::runtime_error("Failed to create window");
    }

    // Step 2: Create the Device from the Window
    auto device = rhi::create_device(window.get());

    // Transfer ownership to renderer
    renderer->window_ = window.release();
    renderer->device_ = device.release();

    // Initialize texture loader
    renderer->texture_loader_ = std::make_unique<resources::TextureLoader>(renderer->device_);

  } catch (const std::exception &e) {
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

  if (window_) {
    delete window_;
    window_ = nullptr;
  }
}

void Renderer::setup_default_shaders() {
  default_shader_ =
      load_shader("assets/shaders/default.vert", "assets/shaders/default.frag");
  instanced_shader_ = load_shader("assets/shaders/instanced.vert",
                                  "assets/shaders/instanced.frag");
  sprite_shader_ = default_shader_; // Use same for sprites
}

void Renderer::begin_frame(const Color &clear_color) {
  auto *cmd = device_->getImmediate();
  cmd->begin();

  rhi::RenderPassDesc pass{};
  pass.colorAttachmentCount = 1;
  pass.colorAttachments[0].texture = rhi::TextureHandle{0};
  pass.colorAttachments[0].loadOp = rhi::LoadOp::Clear;
  pass.colorAttachments[0].storeOp = rhi::StoreOp::Store;
  pass.colorAttachments[0].clearColor[0] = clear_color.r;
  pass.colorAttachments[0].clearColor[1] = clear_color.g;
  pass.colorAttachments[0].clearColor[2] = clear_color.b;
  pass.colorAttachments[0].clearColor[3] = clear_color.a;

  pass.hasDepthAttachment = true;
  pass.depthAttachment.texture = rhi::TextureHandle{0};
  pass.depthAttachment.depthLoadOp = rhi::LoadOp::Clear;
  pass.depthAttachment.depthStoreOp = rhi::StoreOp::DontCare;
  pass.depthAttachment.clearDepth = 1.0f;
  pass.depthAttachment.hasStencil = true;
  pass.depthAttachment.stencilLoadOp = rhi::LoadOp::Clear;
  pass.depthAttachment.stencilStoreOp = rhi::StoreOp::DontCare;
  pass.depthAttachment.clearStencil = 0;

  current_pass_desc_ = pass;
  cmd->beginRender(current_pass_desc_);
  render_pass_active_ = true;
}

void Renderer::end_frame() {
  auto *cmd = device_->getImmediate();
  if (render_pass_active_) {
    cmd->endRender();
    render_pass_active_ = false;
  }
  cmd->end();

  device_->present();
}

void Renderer::pause_render_pass() {
  if (!device_ || !render_pass_active_)
    return;

  auto *cmd = device_->getImmediate();
  cmd->endRender();
  render_pass_active_ = false;
}

void Renderer::resume_render_pass() {
  if (!device_ || render_pass_active_)
    return;

  auto *cmd = device_->getImmediate();
  cmd->beginRender(current_pass_desc_);
  render_pass_active_ = true;
}

bool Renderer::process_events() {
  if (!window_) {
    return false;
  }
  window_->poll_events();
  return !window_->should_close();
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
  return window_ ? window_->width() : 0;
}

int Renderer::window_height() const {
  return window_ ? window_->height() : 0;
}

double Renderer::time() const {
  return window_ ? window_->time() : 0.0;
}

const char *Renderer::backend_name() const { return "OpenGL 3.3 Core"; }

// ============================================================================
// Texture Loading (delegated to TextureLoader)
// ============================================================================

rhi::TextureHandle Renderer::load_texture(const std::string &path) {
  if (!texture_loader_) {
    std::cerr << "Renderer: TextureLoader not initialized" << std::endl;
    return {0};
  }
  return texture_loader_->load(path);
}

rhi::TextureHandle Renderer::create_texture(int width, int height,
                                            const uint8_t *data) {
  if (!texture_loader_) {
    std::cerr << "Renderer: TextureLoader not initialized" << std::endl;
    return {0};
  }
  return texture_loader_->create(width, height, data);
}

rhi::TextureHandle Renderer::create_texture_array(int width, int height,
                                                  int layers) {
  if (!texture_loader_) {
    std::cerr << "Renderer: TextureLoader not initialized" << std::endl;
    return {0};
  }
  return texture_loader_->create_array(width, height, layers);
}

rhi::TextureHandle
Renderer::load_texture_array(const std::vector<std::string> &paths) {
  if (!texture_loader_) {
    std::cerr << "Renderer: TextureLoader not initialized" << std::endl;
    return {0};
  }
  return texture_loader_->load_array(paths);
}

void Renderer::set_texture_array_layer(rhi::TextureHandle array_id, int layer,
                                       int width, int height,
                                       const uint8_t *data) {
  if (!texture_loader_) {
    std::cerr << "Renderer: TextureLoader not initialized" << std::endl;
    return;
  }
  texture_loader_->set_array_layer(array_id, layer, width, height, data);
}

} // namespace pixel::renderer3d
