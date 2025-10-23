#include "pixel/platform/platform.hpp"
#include "pixel/input/input_manager.hpp"
#include "pixel/renderer3d/mesh.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"
#include "pixel/renderer3d/types.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace pixel::renderer3d;

namespace {

constexpr float kPi = 3.14159265359f;
constexpr float kTwoPi = 6.28318530718f;

std::unique_ptr<Mesh> create_uv_sphere(Renderer &renderer, float radius,
                                       int longitude_segments,
                                       int latitude_segments) {
  longitude_segments = std::max(3, longitude_segments);
  latitude_segments = std::max(2, latitude_segments);

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  vertices.reserve(static_cast<size_t>((latitude_segments + 1) *
                                       (longitude_segments + 1)));

  for (int lat = 0; lat <= latitude_segments; ++lat) {
    float v = static_cast<float>(lat) / static_cast<float>(latitude_segments);
    float phi = v * kPi;
    float sin_phi = std::sin(phi);
    float cos_phi = std::cos(phi);

    for (int lon = 0; lon <= longitude_segments; ++lon) {
      float u = static_cast<float>(lon) / static_cast<float>(longitude_segments);
      float theta = u * kTwoPi;
      float sin_theta = std::sin(theta);
      float cos_theta = std::cos(theta);

      Vec3 normal{cos_theta * sin_phi, cos_phi, sin_theta * sin_phi};
      Vec3 position = normal * radius;
      Vec2 texcoord{u, 1.0f - v};

      vertices.push_back(Vertex{position, normal, texcoord, Color::White()});
    }
  }

  indices.reserve(static_cast<size_t>(latitude_segments * longitude_segments * 6));
  for (int lat = 0; lat < latitude_segments; ++lat) {
    for (int lon = 0; lon < longitude_segments; ++lon) {
      uint32_t current = static_cast<uint32_t>(lat * (longitude_segments + 1) + lon);
      uint32_t next = current + static_cast<uint32_t>(longitude_segments + 1);

      indices.push_back(current);
      indices.push_back(next);
      indices.push_back(current + 1);

      indices.push_back(current + 1);
      indices.push_back(next);
      indices.push_back(next + 1);
    }
  }

  return Mesh::create(renderer.device(), vertices, indices);
}

void handle_camera_input(Renderer &renderer, const pixel::input::InputState &input,
                         float delta_time) {
  Camera &camera = renderer.camera();
  const Vec3 cam_pos = camera.position;
  const Vec3 cam_target = camera.target;

  auto squared = [](float value) { return value * value; };
  float distance = std::sqrt(squared(cam_pos.x - cam_target.x) +
                             squared(cam_pos.y - cam_target.y) +
                             squared(cam_pos.z - cam_target.z));
  distance = std::max(distance, 1.0f);

  if (input.mouse_buttons[0]) {
    float dx = static_cast<float>(input.mouse_delta_x) * 1.15f;
    float dy = static_cast<float>(input.mouse_delta_y) * 1.15f;
    camera.orbit(dx, dy);
  }

  if (input.mouse_buttons[1]) {
    float pan_speed = std::max(distance * 0.0025f, 0.0025f);
    float dx = static_cast<float>(input.mouse_delta_x) * pan_speed;
    float dy = static_cast<float>(input.mouse_delta_y) * pan_speed;
    camera.pan(-dx, dy);
  }

  if (input.scroll_delta != 0.0) {
    float zoom_speed = std::max(distance * 0.08f, 0.4f);
    camera.zoom(static_cast<float>(input.scroll_delta) * zoom_speed);
  }

  float dolly = 0.0f;
  if (input.key_down('W'))
    dolly -= 1.0f;
  if (input.key_down('S'))
    dolly += 1.0f;
  if (dolly != 0.0f) {
    camera.zoom(dolly * delta_time * distance);
  }

  float strafe = 0.0f;
  if (input.key_down('D'))
    strafe += 1.0f;
  if (input.key_down('A'))
    strafe -= 1.0f;
  if (strafe != 0.0f) {
    float strafe_speed = std::max(distance * 0.6f, 1.0f);
    camera.pan(-strafe * strafe_speed * delta_time, 0.0f);
  }

  float vertical = 0.0f;
  if (input.key_down('E'))
    vertical += 1.0f;
  if (input.key_down('Q'))
    vertical -= 1.0f;
  if (vertical != 0.0f) {
    float vertical_speed = std::max(distance * 0.5f, 1.0f) * delta_time;
    camera.position.y += vertical_speed * vertical;
    camera.target.y += vertical_speed * vertical;
  }
}

struct SphereAnimation {
  float base_height = 0.0f;
  float amplitude = 0.0f;
  float speed = 1.0f;
  float phase = 0.0f;
  float rotation_speed = 0.0f;
};

struct SphereField {
  std::vector<InstanceData> instances;
  std::vector<SphereAnimation> animation;
};

SphereField create_sphere_field(int rows, int cols, float spacing, float base_height,
                                float base_amplitude, int texture_count) {
  SphereField field;
  field.instances.reserve(static_cast<size_t>(rows * cols));
  field.animation.reserve(static_cast<size_t>(rows * cols));

  std::mt19937 rng(1337);
  std::uniform_real_distribution<float> scale_dist(0.65f, 1.35f);
  std::uniform_real_distribution<float> speed_dist(0.6f, 1.6f);
  std::uniform_real_distribution<float> phase_dist(0.0f, kTwoPi);
  std::uniform_real_distribution<float> amplitude_dist(0.7f, 1.3f);
  std::uniform_real_distribution<float> height_jitter(-0.4f, 0.4f);
  std::uniform_real_distribution<float> rotation_dist(0.25f, 1.1f);

  float start_x = -0.5f * static_cast<float>(cols - 1) * spacing;
  float start_z = -0.5f * static_cast<float>(rows - 1) * spacing;

  for (int row = 0; row < rows; ++row) {
    float v = rows > 1 ? static_cast<float>(row) / static_cast<float>(rows - 1) : 0.0f;
    for (int col = 0; col < cols; ++col) {
      float u = cols > 1 ? static_cast<float>(col) / static_cast<float>(cols - 1) : 0.0f;

      float scale = scale_dist(rng);
      SphereAnimation anim;
      anim.speed = speed_dist(rng);
      anim.phase = phase_dist(rng);
      anim.amplitude = base_amplitude * amplitude_dist(rng);
      anim.base_height = base_height + height_jitter(rng);
      anim.rotation_speed = rotation_dist(rng);

      InstanceData data;
      data.position = {start_x + static_cast<float>(col) * spacing,
                       anim.base_height +
                           static_cast<float>(std::sin(anim.phase)) * anim.amplitude,
                       start_z + static_cast<float>(row) * spacing};
      data.scale = {scale, scale, scale};
      data.rotation = {0.0f, static_cast<float>(std::fmod(anim.phase, kTwoPi)), 0.0f};
      data.color =
          Color(std::clamp(0.65f + 0.35f * v, 0.0f, 1.0f),
                std::clamp(0.6f + 0.4f * (1.0f - v), 0.0f, 1.0f),
                std::clamp(0.7f + 0.3f * u, 0.0f, 1.0f), 1.0f);
      data.texture_index =
          static_cast<float>((row * cols + col) % std::max(1, texture_count));
      float base_radius = 0.5f * scale;
      data.culling_radius = base_radius + anim.amplitude;
      data.lod_transition_alpha = 1.0f;

      field.instances.push_back(data);
      field.animation.push_back(anim);
    }
  }

  return field;
}

void update_sphere_field(std::vector<InstanceData> &instances,
                         const std::vector<SphereAnimation> &animation,
                         float elapsed_time, float delta_time) {
  size_t count = std::min(instances.size(), animation.size());
  for (size_t i = 0; i < count; ++i) {
    const SphereAnimation &anim = animation[i];
    float wave =
        static_cast<float>(std::sin(elapsed_time * anim.speed + anim.phase));
    instances[i].position.y = anim.base_height + wave * anim.amplitude;

    float yaw = instances[i].rotation.y + delta_time * anim.rotation_speed;
    if (yaw > kTwoPi) {
      yaw = std::fmod(yaw, kTwoPi);
    }
    instances[i].rotation.y = yaw;
    instances[i].rotation.x =
        0.15f * static_cast<float>(std::cos((elapsed_time * anim.speed * 0.5f) +
                                            anim.phase));
  }
}

} // namespace

int main(int, char **) {
  pixel::platform::WindowSpec spec;
  spec.w = 1600;
  spec.h = 900;
  spec.title = "Floating Sphere Field";

  auto renderer = Renderer::create(spec);
  if (!renderer) {
    std::cerr << "Failed to create renderer" << std::endl;
    return 1;
  }

  pixel::input::InputManager input_manager(renderer->window());

  std::vector<std::string> texture_paths = {
      "assets/textures/brick.png", "assets/textures/dirt.png",
      "assets/textures/grass.png", "assets/textures/metal.png",
      "assets/textures/stone.png", "assets/textures/wood.png"};

  auto texture_array = renderer->load_texture_array(texture_paths);
  if (texture_array.id == 0) {
    std::cerr << "Failed to load texture array" << std::endl;
    return 1;
  }

  auto sphere_mesh = create_uv_sphere(*renderer, 0.5f, 48, 24);
  if (!sphere_mesh) {
    std::cerr << "Failed to create sphere mesh" << std::endl;
    return 1;
  }

  const int rows = 32;
  const int cols = 32;
  const float spacing = 2.25f;
  const float base_height = 1.6f;
  const float base_amplitude = 0.75f;

  SphereField sphere_field = create_sphere_field(
      rows, cols, spacing, base_height, base_amplitude,
      static_cast<int>(texture_paths.size()));

  auto instanced_mesh = RendererInstanced::create_instanced_mesh(
      renderer->device(), *sphere_mesh, sphere_field.instances.size());
  instanced_mesh->set_instances(sphere_field.instances);

  Material sphere_material{};
  sphere_material.blend_mode = Material::BlendMode::Opaque;
  sphere_material.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
  sphere_material.texture_array = texture_array;
  sphere_material.depth_test = true;
  sphere_material.depth_write = true;

  renderer->camera().mode = Camera::ProjectionMode::Perspective;
  renderer->camera().position = {0.0f, 28.0f, 68.0f};
  renderer->camera().target = {0.0f, 8.0f, 0.0f};
  renderer->camera().fov = 50.0f;
  renderer->camera().far_clip = 400.0f;

  std::cout << "Floating sphere field demo\n";
  std::cout << "Controls: Left mouse drag = orbit, Right mouse drag = pan, "
               "Scroll = zoom, WASD = move, Q/E = vertical, ESC = quit\n";
  std::cout << "Instances: " << sphere_field.instances.size()
            << ", textures used: " << texture_paths.size() << "\n";

  double last_frame_time = renderer->time();
  double stats_time = last_frame_time;
  size_t frames_since_stats = 0;

  while (renderer->process_events()) {
    input_manager.update();
    const auto &input = input_manager.state();
    if (input.keys[256]) {
      break; // ESC
    }

    double current_time = renderer->time();
    float delta_time = static_cast<float>(current_time - last_frame_time);
    last_frame_time = current_time;

    handle_camera_input(*renderer, input, delta_time);

    float elapsed_time = static_cast<float>(current_time);
    update_sphere_field(sphere_field.instances, sphere_field.animation,
                        elapsed_time, delta_time);
    instanced_mesh->set_instances(sphere_field.instances);

    renderer->begin_frame(Color(0.03f, 0.04f, 0.07f, 1.0f));
    RendererInstanced::draw_instanced(*renderer, *instanced_mesh, sphere_material);
    renderer->end_frame();

    ++frames_since_stats;
    if (current_time - stats_time >= 2.0) {
      double fps = static_cast<double>(frames_since_stats) /
                   (current_time - stats_time);
      std::cout << "FPS: " << std::fixed << std::setprecision(1) << fps
                << std::defaultfloat
                << " | Visible spheres: " << instanced_mesh->instance_count()
                << " | Textures: " << texture_paths.size() << '\n';
      stats_time = current_time;
      frames_since_stats = 0;
    }
  }

  return 0;
}
