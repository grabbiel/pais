#include "pixel/platform/platform.hpp"
#include "pixel/renderer3d/lod.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"

#include <iostream>
#include <vector>

using namespace pixel::renderer3d;

namespace {
std::vector<InstanceData> create_grid_instances(int rows, int cols,
                                                float spacing) {
  std::vector<InstanceData> instances;
  instances.reserve(static_cast<size_t>(rows * cols));

  float start_x = -0.5f * (cols - 1) * spacing;

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      InstanceData data;
      data.position = {start_x + static_cast<float>(c) * spacing, 0.0f,
                       -static_cast<float>(r) * spacing * 1.5f};
      data.rotation = {0.0f, 0.0f, 0.0f};
      data.scale = {1.0f, 1.0f, 1.0f};
      float hue = 0.45f + 0.05f * static_cast<float>(r);
      data.color = Color(0.4f + 0.2f * static_cast<float>(c) / cols,
                         0.7f * hue, 0.9f, 1.0f);
      data.culling_radius = 0.9f;
      data.lod_transition_alpha = 1.0f;
      instances.push_back(data);
    }
  }

  return instances;
}

void handle_camera_input(Renderer &renderer, const InputState &input) {
  if (input.mouse_buttons[0]) {
    float dx = static_cast<float>(input.mouse_x - input.prev_mouse_x);
    float dy = static_cast<float>(input.mouse_y - input.prev_mouse_y);
    renderer.camera().orbit(dx, dy);
  }

  if (input.mouse_buttons[1]) {
    float dx = static_cast<float>(input.mouse_x - input.prev_mouse_x) * 0.01f;
    float dy = static_cast<float>(input.mouse_y - input.prev_mouse_y) * 0.01f;
    renderer.camera().pan(-dx, dy);
  }

  if (input.scroll_delta != 0.0) {
    renderer.camera().zoom(static_cast<float>(input.scroll_delta) * 0.4f);
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
