#include "pixel/app/orbit_camera_controller.hpp"
#include "pixel/input/input_manager.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/platform/window.hpp"
#include "pixel/renderer3d/renderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

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
constexpr float kTerrainSize = 20.0f;

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
  camera.position = Vec3{6.0f, 6.0f, 10.0f};
  camera.target = Vec3{0.0f, kSphereRadius * 0.75f, 0.0f};
  camera.up = Vec3{0.0f, 1.0f, 0.0f};
  camera.near_clip = 0.1f;
  camera.far_clip = 100.0f;
  camera.fov = 50.0f;
}

DirectionalLight create_key_light() {
  DirectionalLight light;
  light.direction = Vec3{-0.55f, -1.0f, -0.25f}.normalized();
  light.position = Vec3{12.0f, 18.0f, 12.0f};
  light.color = Color(1.0f, 0.98f, 0.9f, 1.0f);
  light.intensity = 2.0f;
  light.ambient_intensity = 0.3f;
  return light;
}

void configure_shadow_map(Renderer &renderer) {
  ShadowMap *shadow_map = renderer.shadow_map();
  if (!shadow_map) {
    return;
  }

  ShadowMap::Settings settings = shadow_map->settings();
  settings.near_plane = 0.5f;
  settings.far_plane = 60.0f;
  settings.ortho_size = 25.0f;
  settings.depth_bias_constant = 0.7f;
  settings.depth_bias_slope = 1.5f;
  settings.shadow_bias = 0.0015f;
  shadow_map->update_settings(settings);
}

} // namespace

int main() {
  pixel::platform::WindowSpec spec;
  spec.w = 1280;
  spec.h = 720;
  spec.title = "Pixel Life - Shadowed Sphere";

  auto renderer = Renderer::create(spec);
  if (!renderer) {
    std::cerr << "Failed to create renderer" << std::endl;
    return EXIT_FAILURE;
  }

  configure_camera(renderer->camera());
  configure_shadow_map(*renderer);
  renderer->set_directional_light(create_key_light());

  auto terrain_mesh = renderer->create_plane(kTerrainSize, kTerrainSize, 1);
  if (!terrain_mesh) {
    std::cerr << "Failed to create terrain mesh" << std::endl;
    return EXIT_FAILURE;
  }

  auto sphere_mesh = create_sphere_mesh(*renderer);
  if (!sphere_mesh) {
    std::cerr << "Failed to create sphere mesh" << std::endl;
    return EXIT_FAILURE;
  }

  Material terrain_material;
  terrain_material.blend_mode = Material::BlendMode::Opaque;
  terrain_material.depth_test = true;
  terrain_material.depth_write = true;
  terrain_material.color = Color(0.38f, 0.24f, 0.12f, 1.0f);
  terrain_material.roughness = 0.8f;
  terrain_material.metallic = 0.05f;
  terrain_material.glare_intensity = 0.0f;

  Material sphere_material;
  sphere_material.blend_mode = Material::BlendMode::Opaque;
  sphere_material.depth_test = true;
  sphere_material.depth_write = true;
  sphere_material.color = Color(0.9f, 0.05f, 0.05f, 1.0f);
  sphere_material.roughness = 0.35f;
  sphere_material.metallic = 0.25f;
  sphere_material.glare_intensity = 0.6f;

  InputManager input_manager(renderer->window());
  OrbitCameraController camera_controller(renderer->camera(), input_manager);
  camera_controller.set_zoom_limits(1.5f, 45.0f);

  double last_time = renderer->time();
  float rotation = 0.0f;

  while (renderer->process_events()) {
    double now = renderer->time();
    float delta_time = static_cast<float>(now - last_time);
    last_time = now;

    input_manager.update();

    if (input_manager.key_pressed(GLFW_KEY_C)) {
      camera_controller.set_enabled(!camera_controller.enabled());
    }

    camera_controller.update(delta_time);

    rotation += delta_time * glm::radians(15.0f);

    renderer->begin_shadow_pass();
    renderer->draw_shadow_mesh(*terrain_mesh, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f});
    renderer->draw_shadow_mesh(*sphere_mesh, Vec3{0.0f, kSphereRadius, 0.0f},
                               Vec3{0.0f, rotation, 0.0f},
                               Vec3{1.0f, 1.0f, 1.0f});
    renderer->end_shadow_pass();

    renderer->begin_frame(Color(0.08f, 0.09f, 0.12f, 1.0f));

    renderer->draw_mesh(*terrain_mesh, Vec3{0.0f, 0.0f, 0.0f},
                        Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f},
                        terrain_material);

    renderer->draw_mesh(*sphere_mesh, Vec3{0.0f, kSphereRadius, 0.0f},
                        Vec3{0.0f, rotation, 0.0f}, Vec3{1.0f, 1.0f, 1.0f},
                        sphere_material);

    renderer->end_frame();
  }

  return EXIT_SUCCESS;
}
