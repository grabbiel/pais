#include "pixel/app/orbit_camera_controller.hpp"
#include "pixel/input/input_manager.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/platform/window.hpp"
#include "pixel/renderer3d/renderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

using pixel::app::OrbitCameraController;
using pixel::input::InputManager;
using pixel::renderer3d::Camera;
using pixel::renderer3d::Color;
using pixel::renderer3d::DirectionalLight;
using pixel::renderer3d::Material;
using pixel::renderer3d::Mesh;
using pixel::renderer3d::Renderer;
using pixel::renderer3d::ShadowMap;
using pixel::renderer3d::Vec2;
using pixel::renderer3d::Vec3;

namespace {

constexpr float kSphereRadius = 1.0f;
constexpr float kTerrainSize = 40.0f;

std::unique_ptr<Mesh> create_sphere_mesh(Renderer &renderer, int segments = 48,
                                         int rings = 24,
                                         float radius = kSphereRadius) {
  if (segments < 3) {
    segments = 3;
  }
  if (rings < 2) {
    rings = 2;
  }

  std::vector<pixel::renderer3d::Vertex> vertices;
  std::vector<uint32_t> indices;

  const int ring_stride = segments + 1;
  vertices.reserve(static_cast<size_t>((rings + 1) * ring_stride));
  indices.reserve(static_cast<size_t>(rings * segments * 6));

  for (int y = 0; y <= rings; ++y) {
    float v = static_cast<float>(y) / static_cast<float>(rings);
    float theta = glm::pi<float>() * v;
    float sin_theta = std::sin(theta);
    float cos_theta = std::cos(theta);

    for (int x = 0; x <= segments; ++x) {
      float u = static_cast<float>(x) / static_cast<float>(segments);
      float phi = glm::two_pi<float>() * u;
      float sin_phi = std::sin(phi);
      float cos_phi = std::cos(phi);

      Vec3 normal{cos_phi * sin_theta, cos_theta, sin_phi * sin_theta};
      Vec3 position = normal * radius;
      Vec2 uv{u, 1.0f - v};

      vertices.push_back({position, normal.normalized(), uv,
                          Color(1.0f, 0.0f, 0.0f, 1.0f)});
    }
  }

  for (int y = 0; y < rings; ++y) {
    for (int x = 0; x < segments; ++x) {
      uint32_t first = static_cast<uint32_t>(y * ring_stride + x);
      uint32_t second = static_cast<uint32_t>((y + 1) * ring_stride + x);

      indices.push_back(first);
      indices.push_back(second);
      indices.push_back(first + 1);

      indices.push_back(second);
      indices.push_back(second + 1);
      indices.push_back(first + 1);
    }
  }

  return Mesh::create(renderer.device(), vertices, indices);
}

void configure_camera(Camera &camera) {
  camera.position = Vec3{12.0f, 10.0f, 18.0f};
  camera.target = Vec3{0.0f, 2.0f, 0.0f};
  camera.up = Vec3{0.0f, 1.0f, 0.0f};
  camera.near_clip = 0.1f;
  camera.far_clip = 200.0f;
  camera.fov = 55.0f;
}

DirectionalLight create_directional_light() {
  DirectionalLight light;
  light.direction = Vec3{-0.5f, -1.0f, -0.3f}.normalized();
  light.position = Vec3{15.0f, 25.0f, 15.0f};
  light.color = Color(1.0f, 0.97f, 0.92f, 1.0f);
  light.intensity = 1.6f;
  light.ambient_intensity = 0.15f;
  return light;
}

void configure_shadow_map(Renderer &renderer) {
  ShadowMap *shadow_map = renderer.shadow_map();
  if (!shadow_map) {
    return;
  }

  ShadowMap::Settings settings = shadow_map->settings();
  settings.near_plane = 1.0f;
  settings.far_plane = 80.0f;
  settings.ortho_size = 30.0f;
  settings.depth_bias_constant = 0.6f;
  settings.depth_bias_slope = 1.2f;
  settings.shadow_bias = 0.0012f;
  shadow_map->update_settings(settings);
}

Material make_opaque_material(const Color &color, float roughness,
                              float metallic = 0.0f) {
  Material material;
  material.blend_mode = Material::BlendMode::Opaque;
  material.depth_test = true;
  material.depth_write = true;
  material.color = color;
  material.roughness = roughness;
  material.metallic = metallic;
  material.glare_intensity = 0.0f;
  return material;
}

} // namespace

int main() {
  pixel::platform::WindowSpec spec;
  spec.w = 1280;
  spec.h = 720;
  spec.title = "Pixel Life - Shadow Demo";

  auto renderer = Renderer::create(spec);
  if (!renderer) {
    std::cerr << "Failed to create renderer" << std::endl;
    return EXIT_FAILURE;
  }

  configure_camera(renderer->camera());
  configure_shadow_map(*renderer);
  renderer->set_directional_light(create_directional_light());

  auto ground_mesh = renderer->create_plane(kTerrainSize, kTerrainSize, 1);
  if (!ground_mesh) {
    std::cerr << "Failed to create ground plane mesh" << std::endl;
    return EXIT_FAILURE;
  }

  auto sphere_mesh = create_sphere_mesh(*renderer);
  if (!sphere_mesh) {
    std::cerr << "Failed to create dynamic sphere mesh" << std::endl;
    return EXIT_FAILURE;
  }

  Material ground_material =
      make_opaque_material(Color(0.82f, 0.79f, 0.73f, 1.0f), 0.7f);
  Material sphere_material =
      make_opaque_material(Color(0.9f, 0.1f, 0.1f, 1.0f), 0.35f, 0.2f);

  InputManager input_manager(renderer->window());
  OrbitCameraController camera_controller(renderer->camera(), input_manager);
  camera_controller.set_zoom_limits(2.0f, 60.0f);

  double last_time = renderer->time();
  float rotation = 0.0f;

  const Vec3 sphere_position{0.0f, 2.0f, 0.0f};

  while (renderer->process_events()) {
    double now = renderer->time();
    float delta_time = static_cast<float>(now - last_time);
    last_time = now;

    input_manager.update();

    if (input_manager.key_pressed(GLFW_KEY_C)) {
      camera_controller.set_enabled(!camera_controller.enabled());
    }

    camera_controller.update(delta_time);

    rotation += delta_time * glm::radians(20.0f);

    renderer->begin_shadow_pass();
    renderer->draw_shadow_mesh(*ground_mesh, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f},
                               &ground_material);
    renderer->draw_shadow_mesh(*sphere_mesh, sphere_position,
                               Vec3{rotation, rotation * 0.5f, 0.0f},
                               Vec3{1.0f, 1.0f, 1.0f}, &sphere_material);
    renderer->end_shadow_pass();

    renderer->begin_frame(Color(0.55f, 0.78f, 0.93f, 1.0f));

    renderer->draw_mesh(*ground_mesh, Vec3{0.0f, 0.0f, 0.0f},
                        Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f},
                        ground_material);
    renderer->draw_mesh(*sphere_mesh, sphere_position,
                        Vec3{rotation, rotation * 0.5f, 0.0f},
                        Vec3{1.0f, 1.0f, 1.0f}, sphere_material);

    renderer->end_frame();
  }

  return EXIT_SUCCESS;
}

