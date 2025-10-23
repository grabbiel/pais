// src/renderer3d/renderer.cpp (Updated for RHI)
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/primitives.hpp"
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

void Renderer::apply_material_state(rhi::CmdList *cmd,
                                    const Material &material) const {
  if (!cmd)
    return;

  rhi::DepthStencilState depth_state{};
  depth_state.depthTestEnable = material.depth_test;
  depth_state.depthWriteEnable = material.depth_write;
  depth_state.depthCompare = material.depth_compare;
  depth_state.stencilEnable = material.stencil_enable;
  depth_state.stencilCompare = material.stencil_compare;
  depth_state.stencilFailOp = material.stencil_fail_op;
  depth_state.stencilDepthFailOp = material.stencil_depth_fail_op;
  depth_state.stencilPassOp = material.stencil_pass_op;
  depth_state.stencilReadMask = material.stencil_read_mask;
  depth_state.stencilWriteMask = material.stencil_write_mask;
  depth_state.stencilReference = material.stencil_reference;
  cmd->setDepthStencilState(depth_state);

  rhi::DepthBiasState bias_state{};
  bias_state.enable = material.depth_bias_enable;
  bias_state.constantFactor = material.depth_bias_constant;
  bias_state.slopeFactor = material.depth_bias_slope;
  cmd->setDepthBias(bias_state);
}

std::unique_ptr<Mesh> Renderer::create_quad(float size) {
  std::vector<Vertex> verts = primitives::create_quad_vertices(size);
  std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
  return Mesh::create(device_, verts, indices);
}

std::unique_ptr<Mesh> Renderer::create_sprite_quad() { return create_quad(1.0f); }

std::unique_ptr<Mesh> Renderer::create_cube(float size) {
  std::vector<Vertex> verts = primitives::create_cube_vertices(size);
  std::vector<uint32_t> indices;
  for (uint32_t i = 0; i < verts.size(); ++i)
    indices.push_back(i);
  return Mesh::create(device_, verts, indices);
}

std::unique_ptr<Mesh> Renderer::create_plane(float width, float depth,
                                             int segments) {
  std::vector<Vertex> verts =
      primitives::create_plane_vertices(width, depth, segments);
  std::vector<uint32_t> indices;
  for (uint32_t i = 0; i < verts.size(); ++i)
    indices.push_back(i);
  return Mesh::create(device_, verts, indices);
}

void Renderer::draw_mesh(const Mesh &mesh, const Vec3 &position,
                         const Vec3 &rotation, const Vec3 &scale,
                         const Material &material) {
  Shader *shader = get_shader(default_shader_);
  if (!shader)
    return;

  auto *cmd = device_->getImmediate();
  cmd->setPipeline(
      shader->pipeline(material.shader_variant, material.blend_mode));
  apply_material_state(cmd, material);
  cmd->setVertexBuffer(mesh.vertex_buffer());
  cmd->setIndexBuffer(mesh.index_buffer());

  const ShaderReflection &reflection =
      shader->reflection(material.shader_variant);

  // Build model matrix
  glm::mat4 model = glm::mat4(1.0f);
  model =
      glm::translate(model, glm::vec3(position.x, position.y, position.z));
  model = glm::rotate(model, rotation.z, glm::vec3(0, 0, 1));
  model = glm::rotate(model, rotation.y, glm::vec3(0, 1, 0));
  model = glm::rotate(model, rotation.x, glm::vec3(1, 0, 0));
  model = glm::scale(model, glm::vec3(scale.x, scale.y, scale.z));

  // Get view and projection matrices
  float view[16], projection[16];
  camera_.get_view_matrix(view);
  camera_.get_projection_matrix(projection, window_width(), window_height());

  // Set transformation uniforms
  if (reflection.has_uniform("model")) {
    cmd->setUniformMat4("model", glm::value_ptr(model));
  }
  // Calculate and set normal matrix
  glm::mat3 normalMatrix3x3 = glm::transpose(glm::inverse(glm::mat3(model)));
  glm::mat4 normalMatrix4x4 = glm::mat4(normalMatrix3x3);
  if (reflection.has_uniform("normalMatrix")) {
    cmd->setUniformMat4("normalMatrix", glm::value_ptr(normalMatrix4x4));
  }
  if (reflection.has_uniform("view")) {
    cmd->setUniformMat4("view", view);
  }
  if (reflection.has_uniform("projection")) {
    cmd->setUniformMat4("projection", projection);
  }

  // Set lighting uniforms
  float light_pos[3] = {10.0f, 10.0f, 10.0f};
  float view_pos[3] = {camera_.position.x, camera_.position.y,
                       camera_.position.z};
  if (reflection.has_uniform("lightPos")) {
    cmd->setUniformVec3("lightPos", light_pos);
  }
  if (reflection.has_uniform("viewPos")) {
    cmd->setUniformVec3("viewPos", view_pos);
  }

  // Set material uniforms
  if (reflection.has_uniform("useTexture")) {
    cmd->setUniformInt("useTexture", (material.texture.id != 0) ? 1 : 0);
  }

  // Bind texture if available
  if (material.texture.id != 0 && reflection.has_sampler("uTexture")) {
    cmd->setTexture("uTexture", material.texture, 0);
  }

  // Set material color
  float mat_color[4] = {material.color.r, material.color.g, material.color.b,
                        material.color.a};
  if (reflection.has_uniform("materialColor")) {
    cmd->setUniformVec4("materialColor", mat_color);
  }

  // Draw
  cmd->drawIndexed(mesh.index_count(), 0, 1);
}

void Renderer::draw_sprite(rhi::TextureHandle texture, const Vec3 &position,
                           const Vec2 &size, const Color &tint) {
  Shader *shader = get_shader(sprite_shader_);
  if (!shader)
    return;

  auto *cmd = device_->getImmediate();
  cmd->setPipeline(shader->pipeline(Material::BlendMode::Alpha));

  Material sprite_material;
  sprite_material.blend_mode = Material::BlendMode::Alpha;
  sprite_material.color = tint;
  sprite_material.texture = texture;
  apply_material_state(cmd, sprite_material);

  if (sprite_mesh_) {
    cmd->setVertexBuffer(sprite_mesh_->vertex_buffer());
    cmd->setIndexBuffer(sprite_mesh_->index_buffer());
    cmd->drawIndexed(sprite_mesh_->index_count(), 0, 1);
  }
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
