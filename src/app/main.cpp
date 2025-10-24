#include "pixel/platform/platform.hpp"
#include "pixel/platform/window.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

using pixel::renderer3d::Color;
using pixel::renderer3d::DirectionalLight;
using pixel::renderer3d::InstanceData;
using pixel::renderer3d::Material;
using pixel::renderer3d::Mesh;
using pixel::renderer3d::Renderer;
using pixel::renderer3d::RendererInstanced;
using pixel::renderer3d::Vec2;
using pixel::renderer3d::Vec3;

namespace {

constexpr size_t kSphereCount = 4096;
constexpr float kTerrainSize = 100.0f;
constexpr float kSphereRadius = 0.5f;

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

      vertices.push_back({position, normal.normalized(), uv, Color::White()});
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

class FlyCameraController {
public:
  void apply(pixel::renderer3d::Camera &camera) const {
    Vec3 fwd = forward();
    Vec3 forward_dir = fwd.normalized();
    Vec3 up_dir{0.0f, 1.0f, 0.0f};

    camera.position = position;
    camera.target = position + forward_dir;
    camera.up = up_dir;
  }

  void update(GLFWwindow *window, float delta_time) {
    if (!window) {
      return;
    }

    update_mouse(window);

    Vec3 move{0.0f, 0.0f, 0.0f};
    Vec3 forward_dir = forward().normalized();
    Vec3 right_dir = right().normalized();
    Vec3 up_dir{0.0f, 1.0f, 0.0f};

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
      move = move + forward_dir;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
      move = move + (forward_dir * -1.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
      move = move + (right_dir * -1.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
      move = move + right_dir;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
      move = move + up_dir;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
      move = move + (up_dir * -1.0f);
    }

    float speed = move_speed;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
      speed *= 2.5f;
    }

    if (move.length() > 0.0f) {
      Vec3 delta = move.normalized() * (speed * delta_time);
      position = position + delta;
    }
  }

  Vec3 position{0.0f, 16.0f, 24.0f};
  float yaw = -135.0f;
  float pitch = -28.0f;
  float move_speed = 12.0f;
  float mouse_sensitivity = 0.15f;

private:
  Vec3 forward() const {
    float yaw_rad = glm::radians(yaw);
    float pitch_rad = glm::radians(pitch);
    glm::vec3 dir{std::cos(yaw_rad) * std::cos(pitch_rad), std::sin(pitch_rad),
                  std::sin(yaw_rad) * std::cos(pitch_rad)};
    return Vec3::from_glm(glm::normalize(dir));
  }

  Vec3 right() const {
    glm::vec3 dir = forward().to_glm();
    glm::vec3 right_vec =
        glm::normalize(glm::cross(dir, glm::vec3{0.0f, 1.0f, 0.0f}));
    return Vec3::from_glm(right_vec);
  }

  void update_mouse(GLFWwindow *window) {
    int state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
    if (state == GLFW_PRESS) {
      double x = 0.0;
      double y = 0.0;
      glfwGetCursorPos(window, &x, &y);
      if (!dragging_) {
        dragging_ = true;
        last_x_ = x;
        last_y_ = y;
      }

      double dx = x - last_x_;
      double dy = y - last_y_;
      last_x_ = x;
      last_y_ = y;

      yaw += static_cast<float>(dx) * mouse_sensitivity;
      pitch -= static_cast<float>(dy) * mouse_sensitivity;
      pitch = std::clamp(pitch, -89.0f, 89.0f);
    } else {
      dragging_ = false;
    }
  }

  bool dragging_ = false;
  double last_x_ = 0.0;
  double last_y_ = 0.0;
};

} // namespace

int main() {
  pixel::platform::WindowSpec spec;
  spec.w = 1600;
  spec.h = 900;
  spec.title = "Pixel Life - Sunset Sphere Field";

  auto renderer = Renderer::create(spec);
  if (!renderer) {
    std::cerr << "Failed to create renderer" << std::endl;
    return EXIT_FAILURE;
  }

  auto *window = renderer->window();
  GLFWwindow *glfw_window = window ? window->native_handle() : nullptr;

  renderer->camera().near_clip = 0.1f;
  renderer->camera().far_clip = 500.0f;

  DirectionalLight sunlight;
  sunlight.direction = Vec3{-0.55f, -0.8f, -0.25f}.normalized();
  sunlight.position = sunlight.direction * -120.0f;
  sunlight.color = Color(1.0f, 0.58f, 0.38f, 1.0f);
  sunlight.intensity = 2.2f;
  renderer->set_directional_light(sunlight);

  if (auto *shadow_map = renderer->shadow_map()) {
    auto settings = shadow_map->settings();
    settings.near_plane = 0.5f;
    settings.far_plane = 150.0f;
    settings.ortho_size = 80.0f;
    settings.depth_bias_constant = 2.0f;
    settings.depth_bias_slope = 2.5f;
    settings.shadow_bias = 0.0015f;
    shadow_map->update_settings(settings);
  }

  FlyCameraController camera_controller;
  camera_controller.apply(renderer->camera());

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

  auto *device = renderer->device();
  if (!device) {
    std::cerr << "Renderer device unavailable" << std::endl;
    return EXIT_FAILURE;
  }

  auto sphere_instanced = RendererInstanced::create_instanced_mesh(
      device, *sphere_mesh, kSphereCount);
  if (!sphere_instanced) {
    std::cerr << "Failed to create instanced sphere mesh" << std::endl;
    return EXIT_FAILURE;
  }

  std::vector<InstanceData> sphere_instances;
  sphere_instances.reserve(kSphereCount);

  std::mt19937 rng(1337);
  std::uniform_real_distribution<float> jitter(-0.3f, 0.3f);
  std::uniform_real_distribution<float> hue_shift(0.85f, 1.15f);

  int grid_size = static_cast<int>(std::sqrt(static_cast<float>(kSphereCount)));
  if (grid_size * grid_size < static_cast<int>(kSphereCount)) {
    ++grid_size;
  }

  float spacing = (kTerrainSize - 10.0f) / static_cast<float>(grid_size);
  float start_offset = -0.5f * spacing * static_cast<float>(grid_size - 1);

  size_t instance_index = 0;
  for (int z = 0; z < grid_size && instance_index < kSphereCount; ++z) {
    for (int x = 0; x < grid_size && instance_index < kSphereCount; ++x) {
      float px = start_offset + spacing * static_cast<float>(x) + jitter(rng);
      float pz = start_offset + spacing * static_cast<float>(z) + jitter(rng);

      float gradient =
          glm::clamp((static_cast<float>(z) / grid_size), 0.0f, 1.0f);
      float warm_factor = glm::mix(0.6f, 1.0f, gradient);

      InstanceData data;
      data.position = Vec3{px, kSphereRadius, pz};
      data.rotation = Vec3{0.0f, 0.0f, 0.0f};
      data.scale = Vec3{1.0f, 1.0f, 1.0f};
      float tint = hue_shift(rng);
      data.color =
          Color(0.9f * warm_factor * tint, 0.85f * tint, 1.0f * tint, 1.0f);
      data.texture_index = 0.0f;
      data.culling_radius = kSphereRadius * 1.5f;
      data.lod_transition_alpha = 1.0f;

      sphere_instances.push_back(data);
      ++instance_index;
    }
  }

  sphere_instanced->set_instances(sphere_instances);

  Material terrain_material;
  terrain_material.blend_mode = Material::BlendMode::Opaque;
  constexpr int kWoodTextureSize = 4;
  std::array<uint8_t, kWoodTextureSize * kWoodTextureSize * 4> wood_pixels{};
  const glm::vec4 dark_tone{96.0f, 56.0f, 32.0f, 255.0f};
  const glm::vec4 mid_tone{132.0f, 82.0f, 46.0f, 255.0f};
  const glm::vec4 light_tone{174.0f, 112.0f, 66.0f, 255.0f};
  for (int y = 0; y < kWoodTextureSize; ++y) {
    for (int x = 0; x < kWoodTextureSize; ++x) {
      int index = (y * kWoodTextureSize + x) * 4;
      glm::vec4 color = ((x + y) % 3 == 0)
                            ? dark_tone
                            : (((x + y) % 3 == 1) ? mid_tone : light_tone);
      wood_pixels[index + 0] = static_cast<uint8_t>(color.r);
      wood_pixels[index + 1] = static_cast<uint8_t>(color.g);
      wood_pixels[index + 2] = static_cast<uint8_t>(color.b);
      wood_pixels[index + 3] = static_cast<uint8_t>(color.a);
    }
  }
  terrain_material.texture = renderer->create_texture(
      kWoodTextureSize, kWoodTextureSize, wood_pixels.data());
  terrain_material.depth_test = true;
  terrain_material.depth_write = true;
  terrain_material.color = Color::White();
  if (terrain_material.texture.id == 0) {
    std::cerr << "Failed to create terrain texture" << std::endl;
    return EXIT_FAILURE;
  }

  Material sphere_material;
  sphere_material.blend_mode = Material::BlendMode::Opaque;
  sphere_material.depth_test = true;
  sphere_material.depth_write = true;
  sphere_material.color = Color::White();
  sphere_material.roughness = 0.12f;
  sphere_material.metallic = 0.9f;

  camera_controller.position = Vec3{26.0f, 20.0f, 42.0f};
  camera_controller.yaw = -135.0f;
  camera_controller.pitch = -25.0f;
  camera_controller.apply(renderer->camera());

  double last_time = renderer->time();

  while (renderer->process_events()) {
    double now = renderer->time();
    float delta_time = static_cast<float>(now - last_time);
    last_time = now;

    camera_controller.update(glfw_window, delta_time);
    camera_controller.apply(renderer->camera());

    DirectionalLight active_light = renderer->directional_light();
    active_light.position = renderer->camera().position +
                            active_light.direction * -80.0f +
                            Vec3{0.0f, 20.0f, 0.0f};
    renderer->set_directional_light(active_light);

    renderer->begin_shadow_pass();
    renderer->draw_shadow_mesh(*terrain_mesh, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f});
    renderer->draw_shadow_mesh_instanced(
        *sphere_instanced, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
        Vec3{1.0f, 1.0f, 1.0f}, &sphere_material);
    renderer->end_shadow_pass();

    renderer->begin_frame(Color(0.32f, 0.23f, 0.35f, 1.0f));

    renderer->draw_mesh(*terrain_mesh, Vec3{0.0f, 0.0f, 0.0f},
                        Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f},
                        terrain_material);

    RendererInstanced::draw_instanced(*renderer, *sphere_instanced,
                                      sphere_material);

    renderer->end_frame();
  }

  return EXIT_SUCCESS;
}
