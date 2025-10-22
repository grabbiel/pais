#include "pixel/platform/platform.hpp"
#include "pixel/renderer3d/lod.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

using namespace pixel::renderer3d;

namespace {
std::vector<InstanceData> create_demo_instances() {
  struct RowSpec {
    int count;
    float z;
    float scale;
    Color base_color;
  };

  const std::array<RowSpec, 3> rows{
      RowSpec{5, -6.0f, 1.0f, Color(0.95f, 0.55f, 0.35f, 1.0f)},
      RowSpec{7, -18.0f, 1.35f, Color(0.35f, 0.82f, 0.64f, 1.0f)},
      RowSpec{9, -32.0f, 1.75f, Color(0.45f, 0.65f, 0.95f, 1.0f)}};

  const float spacing = 4.0f;

  std::vector<InstanceData> instances;
  size_t total_instances = 0;
  for (const auto &row : rows) {
    total_instances += static_cast<size_t>(row.count);
  }
  instances.reserve(total_instances);

  for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const RowSpec &row = rows[row_index];
    float start_x = -0.5f * static_cast<float>(row.count - 1) * spacing;
    for (int c = 0; c < row.count; ++c) {
      float blend = row.count > 1
                        ? static_cast<float>(c) / static_cast<float>(row.count - 1)
                        : 0.0f;
      float tint = 0.85f + 0.15f * blend;

      InstanceData data;
      data.position = {start_x + static_cast<float>(c) * spacing,
                       row.scale * 0.5f, row.z};
      data.rotation = {0.0f, 0.35f * static_cast<float>(c), 0.0f};
      data.scale = {row.scale, row.scale, row.scale};
      data.color = Color(std::min(row.base_color.r * tint, 1.0f),
                         std::min(row.base_color.g * tint, 1.0f),
                         std::min(row.base_color.b * tint, 1.0f), 1.0f);
      data.texture_index = static_cast<float>(row_index);
      data.culling_radius = row.scale * 0.75f;
      data.lod_transition_alpha = 1.0f;
      instances.push_back(data);
    }
  }

  return instances;
}

void handle_camera_input(Renderer &renderer, const InputState &input,
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
    float dx = static_cast<float>(input.mouse_delta_x) * 1.25f;
    float dy = static_cast<float>(input.mouse_delta_y) * 1.25f;
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

} // namespace

int main(int, char **) {
  pixel::platform::WindowSpec spec;
  spec.w = 1280;
  spec.h = 720;
  spec.title = "Basic LOD Demo";

  auto renderer = Renderer::create(spec);
  if (!renderer) {
    std::cerr << "Failed to create renderer" << std::endl;
    return 1;
  }

  std::cout << "Basic LOD demo\n";
  std::cout << "Controls: Left mouse drag = orbit, Right mouse drag = pan, "
               "Scroll = zoom, ESC = quit\n";

  auto high_mesh = renderer->create_cube(1.0f);
  auto medium_mesh = renderer->create_cube(0.8f);
  auto low_mesh = renderer->create_quad(0.9f);

  LODConfig lod_config;
  lod_config.mode = LODMode::Distance;
  lod_config.distance_high = 6.0f;
  lod_config.distance_medium = 12.0f;
  lod_config.distance_cull = 20.0f;
  lod_config.temporal.enabled = false;
  lod_config.dither.enabled = false;

  auto instances = create_grid_instances(3, 5, 2.5f);

  auto lod_mesh = LODMesh::create(renderer->device(), *high_mesh, *medium_mesh,
                                  *low_mesh, instances.size(), lod_config);
  lod_mesh->set_instances(instances);

  Material object_material{};
  object_material.blend_mode = Material::BlendMode::Opaque;
  object_material.color = Color(0.9f, 0.9f, 0.9f, 1.0f);

  renderer->camera().mode = Camera::ProjectionMode::Perspective;
  renderer->camera().position = {0.0f, 5.0f, 12.0f};
  renderer->camera().target = {0.0f, 1.5f, -6.0f};
  renderer->camera().fov = 55.0f;
  renderer->camera().far_clip = 200.0f;

  double last_stats_time = 0.0;

  while (renderer->process_events()) {
    const auto &input = renderer->input();
    if (input.keys[256]) {
      break; // ESC
    }

    handle_camera_input(*renderer, input);

    renderer->begin_frame(Color(0.1f, 0.12f, 0.16f, 1.0f));
    RendererLOD::draw_lod(*renderer, *lod_mesh, object_material);
    renderer->end_frame();

    double now = renderer->time();
    if (now - last_stats_time > 1.0) {
      auto stats = lod_mesh->get_stats();
      std::cout << "High: " << stats.visible_per_lod[0]
                << ", Medium: " << stats.visible_per_lod[1]
                << ", Low: " << stats.visible_per_lod[2]
                << ", Culled: " << stats.culled << '\n';
      last_stats_time = now;
    }
  }

  return 0;
}
