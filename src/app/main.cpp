#include "pixel/platform/platform.hpp"
#include "pixel/platform/window.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
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

constexpr size_t kGrassBladeCount = 8000;
constexpr float kTerrainSize = 60.0f;
constexpr float kGrassHalfWidth = 0.08f;
constexpr float kGrassBaseHeight = 1.2f;

struct GrassWindState {
  Vec3 base_position{0.0f, 0.0f, 0.0f};
  float sway_phase = 0.0f;
  float sway_speed = 1.0f;
  float sway_strength = 0.05f;
};

std::unique_ptr<Mesh> create_grass_mesh(Renderer &renderer) {
  std::vector<pixel::renderer3d::Vertex> vertices;
  vertices.reserve(16);

  const float half_width = kGrassHalfWidth;
  const float height = kGrassBaseHeight;

  auto push_vertex = [&](const Vec3 &pos, const Vec3 &normal, const Vec2 &uv) {
    vertices.push_back({pos, normal, uv, Color::White()});
  };

  // Quad facing +Z
  push_vertex(Vec3{-half_width, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f},
              Vec2{0.0f, 0.0f});
  push_vertex(Vec3{half_width, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f},
              Vec2{1.0f, 0.0f});
  push_vertex(Vec3{half_width, height, 0.0f}, Vec3{0.0f, 0.0f, 1.0f},
              Vec2{1.0f, 1.0f});
  push_vertex(Vec3{-half_width, height, 0.0f}, Vec3{0.0f, 0.0f, 1.0f},
              Vec2{0.0f, 1.0f});

  // Quad facing -Z
  push_vertex(Vec3{-half_width, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f},
              Vec2{0.0f, 0.0f});
  push_vertex(Vec3{-half_width, height, 0.0f}, Vec3{0.0f, 0.0f, -1.0f},
              Vec2{0.0f, 1.0f});
  push_vertex(Vec3{half_width, height, 0.0f}, Vec3{0.0f, 0.0f, -1.0f},
              Vec2{1.0f, 1.0f});
  push_vertex(Vec3{half_width, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f},
              Vec2{1.0f, 0.0f});

  // Quad facing +X
  push_vertex(Vec3{0.0f, 0.0f, -half_width}, Vec3{1.0f, 0.0f, 0.0f},
              Vec2{0.0f, 0.0f});
  push_vertex(Vec3{0.0f, height, -half_width}, Vec3{1.0f, 0.0f, 0.0f},
              Vec2{0.0f, 1.0f});
  push_vertex(Vec3{0.0f, height, half_width}, Vec3{1.0f, 0.0f, 0.0f},
              Vec2{1.0f, 1.0f});
  push_vertex(Vec3{0.0f, 0.0f, half_width}, Vec3{1.0f, 0.0f, 0.0f},
              Vec2{1.0f, 0.0f});

  // Quad facing -X
  push_vertex(Vec3{0.0f, 0.0f, -half_width}, Vec3{-1.0f, 0.0f, 0.0f},
              Vec2{0.0f, 0.0f});
  push_vertex(Vec3{0.0f, 0.0f, half_width}, Vec3{-1.0f, 0.0f, 0.0f},
              Vec2{1.0f, 0.0f});
  push_vertex(Vec3{0.0f, height, half_width}, Vec3{-1.0f, 0.0f, 0.0f},
              Vec2{1.0f, 1.0f});
  push_vertex(Vec3{0.0f, height, -half_width}, Vec3{-1.0f, 0.0f, 0.0f},
              Vec2{0.0f, 1.0f});

  std::vector<uint32_t> indices = {
      0,  1,  2,  2,  3,  0,  // +Z
      4,  5,  6,  6,  7,  4,  // -Z
      8,  9,  10, 10, 11, 8,  // +X
      12, 13, 14, 14, 15, 12, // -X
  };

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
  spec.title = "Pixel Life - Instanced Grass Field";

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
  sunlight.direction = Vec3{-0.35f, -1.0f, -0.25f}.normalized();
  sunlight.position =
      Vec3{-sunlight.direction.x * 60.0f, 60.0f, -sunlight.direction.z * 60.0f};
  sunlight.color = Color(1.0f, 0.96f, 0.88f, 1.0f);
  sunlight.intensity = 1.25f;
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

  auto grass_mesh = create_grass_mesh(*renderer);
  if (!grass_mesh) {
    std::cerr << "Failed to create grass mesh" << std::endl;
    return EXIT_FAILURE;
  }

  auto *device = renderer->device();
  if (!device) {
    std::cerr << "Renderer device unavailable" << std::endl;
    return EXIT_FAILURE;
  }

  auto grass_instanced = RendererInstanced::create_instanced_mesh(
      device, *grass_mesh, kGrassBladeCount);
  if (!grass_instanced) {
    std::cerr << "Failed to create instanced grass mesh" << std::endl;
    return EXIT_FAILURE;
  }

  std::vector<InstanceData> base_instances;
  base_instances.reserve(kGrassBladeCount);
  std::vector<GrassWindState> wind_states;
  wind_states.reserve(kGrassBladeCount);
  std::vector<InstanceData> current_instances;
  current_instances.reserve(kGrassBladeCount);

  std::mt19937 rng(42);
  std::uniform_real_distribution<float> position_dist(-(kTerrainSize * 0.5f),
                                                      kTerrainSize * 0.5f);
  std::uniform_real_distribution<float> height_dist(0.8f, 1.6f);
  std::uniform_real_distribution<float> sway_speed_dist(0.6f, 1.4f);
  std::uniform_real_distribution<float> sway_strength_dist(0.02f, 0.08f);
  std::uniform_real_distribution<float> color_variation(0.85f, 1.05f);
  std::uniform_real_distribution<float> phase_dist(0.0f,
                                                   2.0f * glm::pi<float>());

  for (size_t i = 0; i < kGrassBladeCount; ++i) {
    float x = position_dist(rng);
    float z = position_dist(rng);
    float height_variation = height_dist(rng);

    InstanceData data;
    data.position = Vec3{x, 0.0f, z};
    data.rotation = Vec3{0.0f, phase_dist(rng), 0.0f};
    data.scale = Vec3{1.0f, height_variation, 1.0f};
    float green = std::clamp(color_variation(rng), 0.7f, 1.1f);
    float brightness = std::clamp(color_variation(rng), 0.7f, 1.1f);
    data.color = Color(0.3f * brightness, green, 0.2f * brightness, 1.0f);
    data.texture_index = 0.0f;
    data.culling_radius = 0.75f * height_variation;
    data.lod_transition_alpha = 1.0f;

    base_instances.push_back(data);

    GrassWindState wind_state;
    wind_state.base_position = data.position;
    wind_state.sway_phase = phase_dist(rng);
    wind_state.sway_speed = sway_speed_dist(rng);
    wind_state.sway_strength = sway_strength_dist(rng);
    wind_states.push_back(wind_state);
    current_instances.push_back(data);
  }

  grass_instanced->set_instances(base_instances);

  Material terrain_material;
  terrain_material.blend_mode = Material::BlendMode::Opaque;
  terrain_material.texture = renderer->load_texture("assets/textures/dirt.png");
  terrain_material.depth_test = true;
  terrain_material.depth_write = true;
  terrain_material.color = Color::White();
  if (terrain_material.texture.id == 0) {
    std::cerr << "Failed to load terrain texture" << std::endl;
    return EXIT_FAILURE;
  }

  Material grass_material;
  grass_material.blend_mode = Material::BlendMode::Alpha;
  grass_material.texture_array =
      renderer->load_texture_array({"assets/textures/grass.png"});
  grass_material.depth_test = true;
  grass_material.depth_write = false;
  grass_material.color = Color::White();
  if (grass_material.texture_array.id == 0) {
    std::cerr << "Failed to load grass texture" << std::endl;
    return EXIT_FAILURE;
  }

  double last_time = renderer->time();

  while (renderer->process_events()) {
    double now = renderer->time();
    float delta_time = static_cast<float>(now - last_time);
    last_time = now;

    camera_controller.update(glfw_window, delta_time);
    camera_controller.apply(renderer->camera());

    DirectionalLight active_light = renderer->directional_light();
    Vec3 camera_position = renderer->camera().position;
    Vec3 light_offset = active_light.direction * -60.0f;
    active_light.position =
        camera_position + light_offset + Vec3{0.0f, 25.0f, 0.0f};
    renderer->set_directional_light(active_light);

    for (size_t i = 0; i < base_instances.size(); ++i) {
      const GrassWindState &wind = wind_states[i];
      InstanceData updated = base_instances[i];
      float sway = std::sin(static_cast<float>(now) * wind.sway_speed +
                            wind.sway_phase) *
                   wind.sway_strength;
      updated.rotation.x = sway;
      updated.rotation.z = sway * 0.5f;
      current_instances[i] = updated;
      grass_instanced->update_instance(i, updated);
    }

    renderer->begin_shadow_pass();
    renderer->draw_shadow_mesh(*terrain_mesh, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f});
    renderer->draw_shadow_mesh_instanced(
        *grass_instanced, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
        Vec3{1.0f, 1.0f, 1.0f}, &grass_material);
    renderer->end_shadow_pass();

    renderer->begin_frame(Color(0.55f, 0.75f, 0.95f, 1.0f));

    renderer->draw_mesh(*terrain_mesh, Vec3{0.0f, 0.0f, 0.0f},
                        Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f},
                        terrain_material);

    RendererInstanced::draw_instanced(*renderer, *grass_instanced,
                                      grass_material);

    renderer->end_frame();
  }

  return EXIT_SUCCESS;
}
