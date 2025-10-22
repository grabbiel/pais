// Grass Field LOD + Instancing demo
#include "pixel/platform/platform.hpp"
#include "pixel/renderer3d/lod.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

using namespace pixel::renderer3d;

namespace {
constexpr float kPi = 3.1415926535f;

std::unique_ptr<Mesh> make_grass_mesh(Renderer &renderer,
                                      const std::vector<float> &angles_rad,
                                      float width, float height,
                                      float tip_offset) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  auto add_card = [&](float angle_rad) {
    float c = std::cos(angle_rad);
    float s = std::sin(angle_rad);
    float half_width = width * 0.5f;

    auto rotate = [&](float x, float y, float z) {
      float rx = x * c - z * s;
      float rz = x * s + z * c;
      return Vec3{rx, y, rz};
    };

    Vec3 bottom_left = rotate(-half_width, 0.0f, 0.0f);
    Vec3 bottom_right = rotate(half_width, 0.0f, 0.0f);
    Vec3 top_left = rotate(-half_width, height, -tip_offset);
    Vec3 top_right = rotate(half_width, height, -tip_offset);

    Vec3 front_normal{-s, 0.0f, c};
    Vec3 back_normal{s, 0.0f, -c};

    size_t base = vertices.size();

    // Front face
    vertices.push_back({bottom_left, front_normal, {0.0f, 0.0f}, Color::White()});
    vertices.push_back({bottom_right, front_normal, {1.0f, 0.0f}, Color::White()});
    vertices.push_back({top_left, front_normal, {0.0f, 1.0f}, Color::White()});
    vertices.push_back({top_right, front_normal, {1.0f, 1.0f}, Color::White()});

    // Back face (duplicate to avoid back-face culling)
    vertices.push_back({bottom_right, back_normal, {0.0f, 0.0f}, Color::White()});
    vertices.push_back({bottom_left, back_normal, {1.0f, 0.0f}, Color::White()});
    vertices.push_back({top_right, back_normal, {0.0f, 1.0f}, Color::White()});
    vertices.push_back({top_left, back_normal, {1.0f, 1.0f}, Color::White()});

    std::array<uint32_t, 6> front = {0, 1, 2, 2, 1, 3};
    std::array<uint32_t, 6> back = {4, 5, 6, 6, 5, 7};

    for (auto idx : front)
      indices.push_back(static_cast<uint32_t>(base + idx));
    for (auto idx : back)
      indices.push_back(static_cast<uint32_t>(base + idx));
  };

  for (float angle : angles_rad) {
    add_card(angle);
  }

  return Mesh::create(renderer.device(), vertices, indices);
}

float deg_to_rad(float deg) { return deg * (kPi / 180.0f); }

std::vector<InstanceData> generate_grass_instances(size_t count, float half_extent,
                                                   float min_scale,
                                                   float max_scale,
                                                   uint32_t seed) {
  std::vector<InstanceData> instances;
  instances.reserve(count);

  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> pos_dist(-half_extent, half_extent);
  std::uniform_real_distribution<float> rot_dist(0.0f, 2.0f * kPi);
  std::uniform_real_distribution<float> scale_dist(min_scale, max_scale);
  std::uniform_real_distribution<float> hue_dist(0.85f, 1.05f);

  for (size_t i = 0; i < count; ++i) {
    InstanceData inst;
    inst.position = {pos_dist(rng), 0.0f, pos_dist(rng)};
    inst.rotation = {0.0f, rot_dist(rng), 0.0f};

    float scale = scale_dist(rng);
    inst.scale = {scale, scale * 1.4f, scale};

    float hue = hue_dist(rng);
    inst.color = Color(0.6f * hue, 0.9f * hue, 0.5f * hue, 1.0f);

    inst.texture_index = 0.0f;
    inst.culling_radius = 0.6f * scale;
    inst.lod_transition_alpha = 1.0f;

    instances.push_back(inst);
  }

  return instances;
}

} // namespace

int main(int, char **) {
  pixel::platform::WindowSpec spec;
  spec.w = 1280;
  spec.h = 720;
  spec.title = "Grass Field LOD Demo";

  auto renderer = Renderer::create(spec);

  const float field_size = 40.0f;
  const size_t grass_count = 6000;

  auto ground_mesh = renderer->create_plane(field_size, field_size, 4);

  auto grass_high =
      make_grass_mesh(*renderer, {deg_to_rad(0.0f), deg_to_rad(60.0f),
                                  deg_to_rad(-60.0f)},
                      0.30f, 1.0f, 0.20f);
  auto grass_medium =
      make_grass_mesh(*renderer, {deg_to_rad(0.0f), deg_to_rad(90.0f)},
                      0.26f, 0.9f, 0.18f);
  auto grass_low =
      make_grass_mesh(*renderer, {deg_to_rad(0.0f)}, 0.22f, 0.8f, 0.15f);

  LODConfig lod_config;
  lod_config.mode = LODMode::Hybrid;
  lod_config.distance_high = 12.0f;
  lod_config.distance_medium = 28.0f;
  lod_config.distance_cull = 80.0f;
  lod_config.screenspace_high = 120.0f;
  lod_config.screenspace_medium = 40.0f;
  lod_config.screenspace_cull = 6.0f;
  lod_config.dither.enabled = true;
  lod_config.dither.crossfade_duration = 0.35f;
  lod_config.dither.dither_pattern_scale = 1.5f;

  auto grass_instances = generate_grass_instances(grass_count, field_size * 0.45f,
                                                  0.7f, 1.25f, 1337);

  auto grass_lod = LODMesh::create(renderer->device(), *grass_high, *grass_medium,
                                   *grass_low, grass_instances.size(), lod_config);
  grass_lod->set_instances(grass_instances);

  auto ground_texture = renderer->load_texture("assets/textures/dirt.png");
  auto grass_array =
      renderer->load_texture_array({"assets/textures/grass.png"});

  Material ground_material{};
  ground_material.texture = ground_texture;
  ground_material.blend_mode = Material::BlendMode::Opaque;
  ground_material.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
  ground_material.roughness = 0.9f;
  ground_material.metallic = 0.0f;

  Material grass_material{};
  grass_material.texture_array = grass_array;
  grass_material.blend_mode = Material::BlendMode::Alpha;
  grass_material.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
  grass_material.roughness = 0.8f;
  grass_material.metallic = 0.0f;
  grass_material.depth_write = false;

  renderer->camera().mode = Camera::ProjectionMode::Perspective;
  renderer->camera().position = {0.0f, 5.0f, 12.0f};
  renderer->camera().target = {0.0f, 0.5f, 0.0f};
  renderer->camera().fov = 55.0f;
  renderer->camera().far_clip = 200.0f;

  std::cout << "Grass Field Demo\n";
  std::cout << "Controls: LMB drag = orbit, RMB drag = pan, scroll = zoom, "
               "ESC = quit\n";

  double last_stats_time = 0.0;

  while (renderer->process_events()) {
    const auto &input = renderer->input();

    if (input.keys[256])
      break; // ESC

    if (input.mouse_buttons[0]) {
      float dx = static_cast<float>(input.mouse_x - input.prev_mouse_x);
      float dy = static_cast<float>(input.mouse_y - input.prev_mouse_y);
      renderer->camera().orbit(dx, dy);
    }

    if (input.mouse_buttons[1]) {
      float dx = static_cast<float>(input.mouse_x - input.prev_mouse_x) * 0.01f;
      float dy = static_cast<float>(input.mouse_y - input.prev_mouse_y) * 0.01f;
      renderer->camera().pan(-dx, dy);
    }

    if (input.scroll_delta != 0.0) {
      renderer->camera().zoom(static_cast<float>(input.scroll_delta) * 0.5f);
    }

    renderer->begin_frame(Color(0.45f, 0.65f, 0.85f, 1.0f));

    renderer->draw_mesh(*ground_mesh, {0.0f, -0.02f, 0.0f}, {0.0f, 0.0f, 0.0f},
                        {1.0f, 1.0f, 1.0f}, ground_material);

    RendererLOD::draw_lod(*renderer, *grass_lod, grass_material);

    renderer->end_frame();

    double now = renderer->time();
    if (now - last_stats_time > 1.5) {
      auto stats = grass_lod->get_stats();
      std::cout << "Grass instances: " << stats.total_instances
                << " | visible high/med/low: " << stats.visible_per_lod[0] << "/"
                << stats.visible_per_lod[1] << "/" << stats.visible_per_lod[2]
                << " | culled: " << stats.culled << std::endl;
      last_stats_time = now;
    }
  }

  return 0;
}
