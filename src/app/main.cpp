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
  std::cout << "Controls: Left mouse = orbit, Right mouse = pan, Scroll = zoom\n";
  std::cout << "          WASD = dolly/strafe, Q/E = vertical, ESC = quit\n";
  std::cout << "Scene: near/mid/far rows should map to High/Medium/Low LOD.\n";

  auto high_mesh = renderer->create_cube(1.0f);
  auto medium_mesh = renderer->create_cube(1.0f);
  auto low_mesh = renderer->create_quad(1.2f);
  auto ground_mesh = renderer->create_plane(80.0f, 80.0f, 4);

  LODConfig lod_config;
  lod_config.mode = LODMode::Distance;
  lod_config.distance_high = 24.0f;
  lod_config.distance_medium = 42.0f;
  lod_config.distance_cull = 65.0f;
  lod_config.temporal.enabled = false;
  lod_config.dither.enabled = false;

  auto base_instances = create_demo_instances();
  auto animated_instances = base_instances;

  auto lod_mesh =
      LODMesh::create(renderer->device(), *high_mesh, *medium_mesh, *low_mesh,
                      base_instances.size(), lod_config);
  lod_mesh->set_instances(base_instances);

  Material object_material{};
  object_material.blend_mode = Material::BlendMode::Opaque;
  object_material.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
  object_material.roughness = 0.4f;

  Material ground_material{};
  ground_material.blend_mode = Material::BlendMode::Opaque;
  ground_material.color = Color(0.22f, 0.25f, 0.30f, 1.0f);
  ground_material.roughness = 1.0f;

  renderer->camera().mode = Camera::ProjectionMode::Perspective;
  renderer->camera().position = {0.0f, 6.0f, 12.0f};
  renderer->camera().target = {0.0f, 3.0f, -20.0f};
  renderer->camera().fov = 55.0f;
  renderer->camera().far_clip = 150.0f;

  double last_frame_time = renderer->time();
  double last_stats_time = last_frame_time;

  while (renderer->process_events()) {
    const auto &input = renderer->input();
    if (input.keys[256]) {
      break; // ESC
    }

    double now = renderer->time();
    float delta_time = static_cast<float>(now - last_frame_time);
    last_frame_time = now;

    handle_camera_input(*renderer, input, delta_time);

    float animation_time = static_cast<float>(now);
    for (size_t i = 0; i < animated_instances.size(); ++i) {
      InstanceData inst = base_instances[i];
      inst.rotation.y += animation_time * 0.35f;
      float pulse =
          1.0f + 0.05f * static_cast<float>(std::sin(animation_time * 0.6f +
                                                     static_cast<float>(i) *
                                                         0.35f));
      inst.scale = {inst.scale.x * pulse, inst.scale.y * pulse,
                    inst.scale.z * pulse};
      inst.position.y = inst.scale.y * 0.5f;
      inst.culling_radius = inst.scale.x * 0.85f;
      animated_instances[i] = inst;
      lod_mesh->update_instance(i, inst);
    }

    renderer->begin_frame(Color(0.06f, 0.08f, 0.12f, 1.0f));
    renderer->draw_mesh(*ground_mesh, {0.0f, -0.01f, -20.0f}, {0.0f, 0.0f, 0.0f},
                        {1.0f, 1.0f, 1.0f}, ground_material);
    RendererLOD::draw_lod(*renderer, *lod_mesh, object_material);
    renderer->end_frame();

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
