// ============================================================================
// Grass Field Demo - Dithered LOD Transitions
// ============================================================================

#include "pixel/platform/platform.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/lod.hpp"
#include <cmath>
#include <iostream>
#include <random>

using namespace pixel::renderer3d;

// ============================================================================
// Grass Blade Mesh Generator
// ============================================================================

std::unique_ptr<Mesh> create_grass_blade_high(Renderer &r) {
  // High detail: 6 triangles per blade (crossed quads + tapered top)
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  float width = 0.1f;
  float height = 1.0f;

  // Front quad
  vertices.push_back(
      {{-width / 2, 0, 0}, {0, 0, 1}, {0, 0}, Color(0.2f, 0.6f, 0.1f)});
  vertices.push_back(
      {{width / 2, 0, 0}, {0, 0, 1}, {1, 0}, Color(0.2f, 0.6f, 0.1f)});
  vertices.push_back({{width / 4, height * 0.7f, 0},
                      {0, 0, 1},
                      {0.7f, 0.7f},
                      Color(0.3f, 0.7f, 0.2f)});
  vertices.push_back({{-width / 4, height * 0.7f, 0},
                      {0, 0, 1},
                      {0.3f, 0.7f},
                      Color(0.3f, 0.7f, 0.2f)});
  vertices.push_back(
      {{0, height, 0}, {0, 0, 1}, {0.5f, 1}, Color(0.4f, 0.8f, 0.3f)});

  // Cross quad (rotated 90 degrees)
  vertices.push_back(
      {{0, 0, -width / 2}, {1, 0, 0}, {0, 0}, Color(0.2f, 0.6f, 0.1f)});
  vertices.push_back(
      {{0, 0, width / 2}, {1, 0, 0}, {1, 0}, Color(0.2f, 0.6f, 0.1f)});
  vertices.push_back({{0, height * 0.7f, width / 4},
                      {1, 0, 0},
                      {0.7f, 0.7f},
                      Color(0.3f, 0.7f, 0.2f)});
  vertices.push_back({{0, height * 0.7f, -width / 4},
                      {1, 0, 0},
                      {0.3f, 0.7f},
                      Color(0.3f, 0.7f, 0.2f)});
  vertices.push_back(
      {{0, height, 0}, {1, 0, 0}, {0.5f, 1}, Color(0.4f, 0.8f, 0.3f)});

  // Front quad triangles
  indices.insert(indices.end(), {0, 1, 2, 0, 2, 3, 3, 2, 4});
  // Cross quad triangles
  indices.insert(indices.end(), {5, 6, 7, 5, 7, 8, 8, 7, 9});

  return Mesh::create(vertices, indices);
}

std::unique_ptr<Mesh> create_grass_blade_medium(Renderer &r) {
  // Medium detail: 2 triangles (single crossed quad)
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  float width = 0.1f;
  float height = 1.0f;

  // Simple quad
  vertices.push_back(
      {{-width / 2, 0, 0}, {0, 0, 1}, {0, 0}, Color(0.2f, 0.6f, 0.1f)});
  vertices.push_back(
      {{width / 2, 0, 0}, {0, 0, 1}, {1, 0}, Color(0.2f, 0.6f, 0.1f)});
  vertices.push_back(
      {{0, height, 0}, {0, 0, 1}, {0.5f, 1}, Color(0.4f, 0.8f, 0.3f)});

  indices.insert(indices.end(), {0, 1, 2});

  return Mesh::create(vertices, indices);
}

std::unique_ptr<Mesh> create_grass_blade_low(Renderer &r) {
  // Low detail: single vertical line (2 triangles forming thin quad)
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  float width = 0.05f;
  float height = 0.8f;

  vertices.push_back(
      {{-width / 2, 0, 0}, {0, 0, 1}, {0, 0}, Color(0.3f, 0.7f, 0.2f)});
  vertices.push_back(
      {{width / 2, 0, 0}, {0, 0, 1}, {1, 0}, Color(0.3f, 0.7f, 0.2f)});
  vertices.push_back(
      {{0, height, 0}, {0, 0, 1}, {0.5f, 1}, Color(0.4f, 0.8f, 0.3f)});

  indices.insert(indices.end(), {0, 1, 2});

  return Mesh::create(vertices, indices);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
  pixel::platform::WindowSpec ws;
  ws.w = 1920;
  ws.h = 1080;
  ws.title = "Grass Field - Dithered LOD Demo";

  auto r = Renderer::create(ws);

  std::cout << "\n========================================" << std::endl;
  std::cout << "Grass Field - Dithered LOD Transitions" << std::endl;
  std::cout << "========================================\n" << std::endl;

  // ============================================================================
  // Create Grass Meshes
  // ============================================================================

  auto grass_high = create_grass_blade_high(*r);
  auto grass_medium = create_grass_blade_medium(*r);
  auto grass_low = create_grass_blade_low(*r);

  std::cout << "Grass blade detail levels:" << std::endl;
  std::cout << "  High:   " << grass_high->vertex_count() << " verts, "
            << grass_high->index_count() / 3 << " tris" << std::endl;
  std::cout << "  Medium: " << grass_medium->vertex_count() << " verts, "
            << grass_medium->index_count() / 3 << " tris" << std::endl;
  std::cout << "  Low:    " << grass_low->vertex_count() << " verts, "
            << grass_low->index_count() / 3 << " tris\n"
            << std::endl;

  // Ground plane
  auto ground = r->create_plane(200.0f, 200.0f, 100);

  // ============================================================================
  // Camera Setup
  // ============================================================================

  r->camera().position = {30, 20, 30};
  r->camera().target = {0, 0, 0};
  r->camera().mode = Camera::ProjectionMode::Perspective;
  r->camera().fov = 75.0f;
  r->camera().far_clip = 500.0f;

  // ============================================================================
  // Configure LOD with Dithering
  // ============================================================================

  LODConfig config;

  // Use hybrid mode for best results
  config.mode = LODMode::Hybrid;
  config.hybrid_screenspace_weight = 0.6f; // Favor screen-space

  // Distance thresholds
  config.distance_high = 15.0f;
  config.distance_medium = 35.0f;
  config.distance_cull = 80.0f;

  // Screen-space thresholds
  config.screenspace_high = 0.08f;   // 8% of screen
  config.screenspace_medium = 0.03f; // 3% of screen
  config.screenspace_low = 0.01f;    // 1% of screen

  // Temporal coherence (smooth transitions)
  config.temporal.enabled = true;
  config.temporal.upgrade_delay = 0.05f;  // Fast upgrade to quality
  config.temporal.downgrade_delay = 0.3f; // Slow downgrade (reduce popping)
  config.temporal.distance_hysteresis = 3.0f;
  config.temporal.screenspace_hysteresis = 0.015f;

  // Dithered transitions (NEW!)
  config.dither.enabled = true;
  config.dither.crossfade_duration = 0.25f; // 250ms smooth fade
  config.dither.temporal_jitter = true;     // Animated dither

  const int MAX_GRASS = 50000;
  auto grass_lod = LODMesh::create(r->device(), *grass_high, *grass_medium,
                                   *grass_low, MAX_GRASS / 3, config);

  // ============================================================================
  // Generate Grass Field
  // ============================================================================

  std::vector<InstanceData> grass_instances;
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> pos_dist(-80.0f, 80.0f);
  std::uniform_real_distribution<float> rot_dist(0.0f, 360.0f);
  std::uniform_real_distribution<float> scale_dist(0.7f, 1.3f);
  std::uniform_real_distribution<float> sway_dist(0.0f, 6.28f);
  std::uniform_real_distribution<float> color_var(-0.1f, 0.1f);

  std::cout << "Generating grass field..." << std::endl;

  for (int i = 0; i < MAX_GRASS; ++i) {
    InstanceData inst;

    float x = pos_dist(rng);
    float z = pos_dist(rng);

    // Cluster grass more in center
    float dist_from_center = std::sqrt(x * x + z * z);
    if (dist_from_center > 70.0f && (rng() % 3) != 0) {
      continue; // Skip some outer grass
    }

    inst.position = {x, 0, z};
    inst.rotation = {0, rot_dist(rng), 0};

    float s = scale_dist(rng);
    inst.scale = {s, s, s};

    // Varying green shades
    float green_var = color_var(rng);
    inst.color = Color(0.2f + green_var * 0.5f, 0.6f + green_var,
                       0.1f + green_var * 0.3f, 1.0f);

    inst.texture_index = 0.0f;
    inst.culling_radius = 1.0f;
    inst.lod_transition_alpha = 1.0f;

    grass_instances.push_back(inst);
  }

  grass_lod->set_instances(grass_instances);

  std::cout << "Generated " << grass_instances.size() << " grass blades"
            << std::endl;

  // ============================================================================
  // Animation State
  // ============================================================================

  bool auto_rotate = true;
  float camera_angle = 0.0f;
  float camera_radius = 40.0f;
  float camera_height = 20.0f;

  bool show_stats = true;
  double last_stats_time = 0.0;

  // ============================================================================
  // Main Loop
  // ============================================================================

  std::cout << "\nControls:" << std::endl;
  std::cout << "  WASD - Pan camera" << std::endl;
  std::cout << "  Mouse - Orbit camera" << std::endl;
  std::cout << "  Scroll - Zoom" << std::endl;
  std::cout << "  Space - Toggle auto-rotate" << std::endl;
  std::cout << "  Tab - Toggle stats" << std::endl;
  std::cout << "  ESC - Quit\n" << std::endl;

  Material grass_material;
  grass_material.ambient = {0.3f, 0.3f, 0.3f, 1.0f};
  grass_material.diffuse = {0.8f, 0.8f, 0.8f, 1.0f};

  Material ground_material;
  ground_material.diffuse = {0.4f, 0.3f, 0.2f, 1.0f};

  while (r->process_events()) {
    double current_time = r->time();
    auto &input = r->input();

    // ========================================================================
    // Input Handling
    // ========================================================================

    if (input.keys[256])
      break; // ESC

    if (input.keys[32] && !input.prev_keys[32]) { // Space
      auto_rotate = !auto_rotate;
    }

    if (input.keys[258] && !input.prev_keys[258]) { // Tab
      show_stats = !show_stats;
    }

    // Camera controls
    if (!auto_rotate) {
      if (input.keys['W'])
        r->camera().zoom(0.5f);
      if (input.keys['S'])
        r->camera().zoom(-0.5f);
      if (input.keys['A'])
        r->camera().pan(-0.3f, 0);
      if (input.keys['D'])
        r->camera().pan(0.3f, 0);
    }

    if (input.mouse_buttons[0]) { // Left mouse drag
      float dx = input.mouse_x - input.prev_mouse_x;
      float dy = input.mouse_y - input.prev_mouse_y;
      r->camera().orbit(dx, dy);
      auto_rotate = false;
    }

    if (input.scroll_delta != 0.0f) {
      camera_radius -= input.scroll_delta * 2.0f;
      camera_radius = std::max(10.0f, std::min(camera_radius, 100.0f));
    }

    // Auto-rotate camera
    if (auto_rotate) {
      camera_angle += 0.2f;
      r->camera().position.x = camera_radius * std::cos(camera_angle * 0.01f);
      r->camera().position.z = camera_radius * std::sin(camera_angle * 0.01f);
      r->camera().position.y = camera_height;
    }

    // ========================================================================
    // Gentle Wind Sway Animation
    // ========================================================================

    float wind_time = current_time * 0.5f;
    for (size_t i = 0; i < grass_instances.size(); ++i) {
      auto &inst = grass_instances[i];

      // Different phase for each blade
      float phase = (inst.position.x + inst.position.z) * 0.1f;
      float sway = std::sin(wind_time + phase) * 3.0f;

      inst.rotation.z = sway;
    }
    grass_lod->set_instances(grass_instances);

    // ========================================================================
    // Rendering
    // ========================================================================

    r->begin_frame(Color(0.5f, 0.7f, 0.9f, 1.0f)); // Sky blue

    // Draw ground
    r->draw_mesh(*ground, {0, -0.1f, 0}, {0, 0, 0}, {1, 1, 1}, ground_material);

    // Draw grass with dithered LOD transitions
    RendererLOD::draw_lod(*r, *grass_lod, grass_material);

    // ========================================================================
    // Stats Display
    // ========================================================================

    if (show_stats && (current_time - last_stats_time) > 0.5) {
      auto stats = grass_lod->get_stats();

      std::cout << "\r                                                         "
                   "       ";
      std::cout << "\rGrass LODs - High: " << stats.visible_per_lod[0]
                << " | Med: " << stats.visible_per_lod[1]
                << " | Low: " << stats.visible_per_lod[2] << " | FPS: ~"
                << (int)(1.0 / (current_time - last_stats_time + 0.001))
                << "      " << std::flush;

      last_stats_time = current_time;
    }

    r->end_frame();
  }

  std::cout << "\n\nShutting down..." << std::endl;
  return 0;
}

/*
 * GRASS FIELD DEMO FEATURES
 * ==========================
 *
 * This demo showcases the dithered LOD transition system with a field of
 * wind-swaying grass blades.
 *
 * VISUAL EFFECTS:
 * - 50,000 grass blade instances
 * - 3 LOD levels with different polygon counts
 * - Smooth dithered transitions between LODs (no popping!)
 * - Gentle wind animation
 * - Auto-rotating camera (toggle with Space)
 *
 * LOD SYSTEM HIGHLIGHTS:
 * - Hybrid LOD (distance + screen-space)
 * - Temporal coherence prevents rapid switching
 * - 250ms crossfade with temporal dithering
 * - Animated Bayer dither pattern
 *
 * EXPECTED BEHAVIOR:
 * - Close grass: High detail (crossed quads)
 * - Medium distance: Medium detail (single quad)
 * - Far grass: Low detail (thin blade)
 * - Smooth fading between levels (watch carefully!)
 *
 * PERFORMANCE:
 * - Should run at 60 FPS on modern hardware
 * - LOD system reduces polygon count dramatically
 * - Dithering adds minimal overhead
 */
