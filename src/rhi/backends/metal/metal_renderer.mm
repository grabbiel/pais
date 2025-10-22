#ifdef __APPLE__

#include "pixel/rhi/backends/metal/metal_renderer.hpp"

#include "metal_backend.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <memory>
#include <unordered_map>

namespace pixel::renderer3d::metal {

struct MetalRenderer::Impl {
  std::unique_ptr<MetalBackend> backend;
  std::unique_ptr<MetalShader> default_shader;
  std::unordered_map<const Mesh *, std::unique_ptr<MetalMesh>> mesh_cache;

  MetalMesh *get_or_create_mesh(const Mesh &mesh) {
    auto it = mesh_cache.find(&mesh);
    if (it != mesh_cache.end()) {
      return it->second.get();
    }

    if (!backend) {
      return nullptr;
    }

    auto metal_mesh = backend->create_mesh(mesh.vertices(), mesh.indices());
    if (!metal_mesh) {
      return nullptr;
    }

    MetalMesh *result = metal_mesh.get();
    mesh_cache.emplace(&mesh, std::move(metal_mesh));
    return result;
  }
};

namespace {

void glfw_error_callback(int error, const char *description) {
  std::cerr << "GLFW Error " << error << ": "
            << (description ? description : "<unknown>") << std::endl;
}

} // namespace

MetalRenderer::MetalRenderer() : impl_(std::make_unique<Impl>()) {}
MetalRenderer::~MetalRenderer() = default;

std::unique_ptr<MetalRenderer>
MetalRenderer::create(const pixel::platform::WindowSpec &spec) {
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW for Metal renderer" << std::endl;
    return nullptr;
  }

  auto renderer = std::unique_ptr<MetalRenderer>(new MetalRenderer());

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  renderer->window_ =
      glfwCreateWindow(spec.w, spec.h, spec.title.c_str(), nullptr, nullptr);

  if (!renderer->window_) {
    std::cerr << "Failed to create GLFW window for Metal renderer"
              << std::endl;
    glfwTerminate();
    return nullptr;
  }

  renderer->impl_->backend = MetalBackend::create(renderer->window_);
  if (!renderer->impl_->backend) {
    std::cerr << "Failed to initialize Metal backend" << std::endl;
    glfwDestroyWindow(renderer->window_);
    renderer->window_ = nullptr;
    glfwTerminate();
    return nullptr;
  }

  renderer->impl_->default_shader =
      renderer->impl_->backend->create_shader({}, {});

  if (!renderer->impl_->default_shader) {
    std::cerr << "Failed to create Metal default shader" << std::endl;
    renderer->impl_->backend.reset();
    glfwDestroyWindow(renderer->window_);
    renderer->window_ = nullptr;
    glfwTerminate();
    return nullptr;
  }

  return renderer;
}

void MetalRenderer::begin_frame(const Color &clear_color) {
  if (!impl_ || !impl_->backend) {
    return;
  }

  impl_->backend->begin_frame(clear_color);
}

void MetalRenderer::end_frame() {
  if (!impl_ || !impl_->backend) {
    return;
  }

  impl_->backend->end_frame();
}

void MetalRenderer::draw_mesh(const Mesh &mesh, const Vec3 &position,
                              const Vec3 &rotation, const Vec3 &scale,
                              const Material &material) {
  if (!impl_ || !impl_->backend || !impl_->default_shader) {
    return;
  }

  MetalMesh *metal_mesh = impl_->get_or_create_mesh(mesh);
  if (!metal_mesh) {
    std::cerr << "Failed to create Metal mesh for draw call" << std::endl;
    return;
  }

  MetalShader *shader = impl_->default_shader.get();

  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, glm::vec3(position.x, position.y, position.z));
  model = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  model = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
  model = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
  model = glm::scale(model, glm::vec3(scale.x, scale.y, scale.z));

  float view[16];
  float projection[16];
  camera_.get_view_matrix(view);
  camera_.get_projection_matrix(projection, window_width(), window_height());

  shader->set_mat4("model", glm::value_ptr(model));
  glm::mat3 normalMatrix3x3 = glm::transpose(glm::inverse(glm::mat3(model)));
  glm::mat4 normalMatrix4x4 = glm::mat4(normalMatrix3x3);
  shader->set_mat4("normalMatrix", glm::value_ptr(normalMatrix4x4));
  shader->set_mat4("view", view);
  shader->set_mat4("projection", projection);

  shader->set_vec3("lightPos", Vec3{10.0f, 10.0f, 10.0f});
  shader->set_vec3("viewPos", camera_.position);
  shader->set_float("time", static_cast<float>(glfwGetTime()));
  shader->set_int("useTexture", material.texture.id != 0 ? 1 : 0);
  shader->set_int("useTextureArray", 0);
  shader->set_int("ditherEnabled", 0);

  impl_->backend->draw_mesh(*metal_mesh, position, rotation, scale, shader,
                            material);
}

MetalBackend *MetalRenderer::metal_backend() {
  return impl_ ? impl_->backend.get() : nullptr;
}

const MetalBackend *MetalRenderer::metal_backend() const {
  return impl_ ? impl_->backend.get() : nullptr;
}

} // namespace pixel::renderer3d::metal

#endif // __APPLE__
