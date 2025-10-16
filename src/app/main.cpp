// Screen-Space LOD Demo
// Demonstrates the difference between distance-based, screen-space, and hybrid
// LOD

#include "pixel/core/clock.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/platform/resources.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/lod.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>

using namespace pixel::renderer3d;

int main(int argc, char **argv) {
  pixel::platform::WindowSpec ws;
  ws.w = 1920;
  ws.h = 1080;
  ws.title = "Pixel-Life - Screen-Space LOD Demo";

  auto r = Renderer::create(ws);

  // ============================================================================
  // Create LOD Meshes
  // ============================================================================

  auto cube_high = RendererLOD::create_cube_high_detail(*r, 1.0f);
  auto cube_medium = RendererLOD::create_cube_medium_detail(*r, 1.0f);
  auto cube_low = RendererLOD::create_cube_low_detail(*r, 1.0f);

  std::cout << "\n========================================" << std::endl;
  std::cout << "Screen-Space LOD Demo" << std::endl;
  std::cout << "========================================\n" << std::endl;

  std::cout << "Mesh detail levels:" << std::endl;
  std::cout << "  High:   " << cube_high->vertex_count() << " vertices"
            << std::endl;
  std::cout << "  Medium: " << cube_medium->vertex_count() << " vertices"
            << std::endl;
  std::cout << "  Low:    " << cube_low->vertex_count() << " vertices\n"
            << std::endl;

  // Floor
  auto floor_mesh = r->create_plane(300.0f, 300.0f, 50);

  // ============================================================================
  // Camera Setup
  // ============================================================================

  r->camera().position = {60, 40, 60};
  r->camera().target = {0, 0, 0};
  r->camera().mode = Camera::ProjectionMode::Perspective;
  r->camera().fov = 60.0f;
  r->camera().far_clip = 500.0f;

  // ============================================================================
  // Texture Array
  // ============================================================================

  std::vector<std::string> texture_paths = {
      pixel::platform::get_resource_file("assets/textures/brick.png"),
      pixel::platform::get_resource_file("assets/textures/stone.png"),
      pixel::platform::get_resource_file("assets/textures/wood.png"),
      pixel::platform::get_resource_file("assets/textures/metal.png"),
      pixel::platform::get_resource_file("assets/textures/grass.png"),
      pixel::platform::get_resource_file("assets/textures/dirt.png")};

  TextureArrayID tex_array = INVALID_TEXTURE_ARRAY;

  try {
    tex_array = r->load_texture_array(texture_paths);
  } catch (const std::exception &e) {
    // Fallback to procedural textures
    const int TEX_SIZE = 128;
    const int NUM_TEXTURES = 6;
    tex_array = r->create_texture_array(TEX_SIZE, TEX_SIZE, NUM_TEXTURES);

    std::vector<uint8_t> tex_data(TEX_SIZE * TEX_SIZE * 4);
    for (int layer = 0; layer < NUM_TEXTURES; ++layer) {
      for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
          int idx = (y * TEX_SIZE + x) * 4;
          bool checker = ((x / 8) + (y / 8)) % 2 == 0;
          float base = checker ? 0.8f : 0.3f;
          float hue = layer / (float)NUM_TEXTURES;

          tex_data[idx + 0] =
              (uint8_t)(base * 255 * (0.5f + 0.5f * std::sin(hue * 6.28f)));
          tex_data[idx + 1] =
              (uint8_t)(base * 255 *
                        (0.5f + 0.5f * std::sin(hue * 6.28f + 2.0f)));
          tex_data[idx + 2] =
              (uint8_t)(base * 255 *
                        (0.5f + 0.5f * std::sin(hue * 6.28f + 4.0f)));
          tex_data[idx + 3] = 255;
        }
      }
      r->set_texture_array_layer(tex_array, layer, tex_data.data());
    }
  }

  auto array_info = r->get_texture_array_info(tex_array);

  // ============================================================================
  // Create Three LOD Systems for Comparison
  // ============================================================================

  const int GRID_SIZE = 50;
  const int MAX_INSTANCES = GRID_SIZE * GRID_SIZE;

  // 1. Distance-based LOD
  LODConfig distance_config;
  distance_config.mode = LODMode::Distance;
  distance_config.distance_high = 30.0f;
  distance_config.distance_medium = 80.0f;
  distance_config.distance_cull = 200.0f;

  auto lod_distance = RendererLOD::create_lod_mesh(
      *cube_high, *cube_medium, *cube_low, MAX_INSTANCES / 3, distance_config);

  // 2. Screen-space LOD
  LODConfig screenspace_config;
  screenspace_config.mode = LODMode::ScreenSpace;
  screenspace_config.screenspace_high = 0.15f;   // 15% of screen height
  screenspace_config.screenspace_medium = 0.05f; // 5% of screen height
  screenspace_config.screenspace_low = 0.01f;    // 1% of screen height

  auto lod_screenspace =
      RendererLOD::create_lod_mesh(*cube_high, *cube_medium, *cube_low,
                                   MAX_INSTANCES / 3, screenspace_config);

  // 3. Hybrid LOD
  LODConfig hybrid_config;
  hybrid_config.mode = LODMode::Hybrid;
  hybrid_config.distance_high = 30.0f;
  hybrid_config.distance_medium = 80.0f;
  hybrid_config.distance_cull = 200.0f;
  hybrid_config.screenspace_high = 0.15f;
  hybrid_config.screenspace_medium = 0.05f;
  hybrid_config.screenspace_low = 0.01f;
  hybrid_config.hybrid_screenspace_weight = 0.5f; // 50/50 blend

  auto lod_hybrid = RendererLOD::create_lod_mesh(
      *cube_high, *cube_medium, *cube_low, MAX_INSTANCES / 3, hybrid_config);

  std::cout << "Created 3 LOD systems:" << std::endl;
  std::cout << "  1. Distance-based (traditional)" << std::endl;
  std::cout << "  2. Screen-space (pixel coverage)" << std::endl;
  std::cout << "  3. Hybrid (50% distance + 50% screen-space)\n" << std::endl;

  // ============================================================================
  // Generate Instance Data with Varying Sizes
  // ============================================================================

  // Create grid with varying sizes to demonstrate screen-space LOD
  auto instances =
      RendererInstanced::create_grid(GRID_SIZE, GRID_SIZE, 3.0f, 0.5f);

  // Add size variation - objects further from center are larger
  for (size_t i = 0; i < instances.size(); ++i) {
    int x = i % GRID_SIZE;
    int z = i / GRID_SIZE;

    float center_x = GRID_SIZE / 2.0f;
    float center_z = GRID_SIZE / 2.0f;
    float dist_from_center = std::sqrt((x - center_x) * (x - center_x) +
                                       (z - center_z) * (z - center_z));

    // Objects near edges are 3x larger than center objects
    float scale = 0.8f + 2.2f * (dist_from_center / (GRID_SIZE * 0.7f));
    scale = std::min(scale, 3.0f);

    instances[i].scale = {scale, scale, scale};
    instances[i].culling_radius = 0.866f * scale;
  }

  RendererInstanced::assign_texture_indices(instances, array_info.layers);

  lod_distance->set_instances(instances);
  lod_screenspace->set_instances(instances);
  lod_hybrid->set_instances(instances);

  std::cout << "Instance configuration:" << std::endl;
  std::cout << "  Total instances: " << MAX_INSTANCES << std::endl;
  std::cout << "  Grid: " << GRID_SIZE << "x" << GRID_SIZE << std::endl;
  std::cout << "  Size variation: 0.8x - 3.0x (larger at edges)\n" << std::endl;
  std::cout << "This demonstrates how screen-space LOD adapts to object size!"
            << std::endl;
  std::cout
      << "Large distant objects = same screen size as small nearby objects\n"
      << std::endl;

  // Floor material
  Material floor_mat;
  floor_mat.diffuse = Color(0.15f, 0.18f, 0.15f, 1.0f);

  // Material with texture array
  Material textured_mat;
  textured_mat.texture_array = tex_array;

  // ============================================================================
  // Main Loop Setup
  // ============================================================================

  const double dt = 1.0 / 60.0;
  double acc = 0.0, t0 = pixel::core::now_sec();
  float time_elapsed = 0.0f;

  bool mouse_was_pressed = false;
  bool show_stats = true;
  bool animate_instances = true;

  // Current LOD mode: 0=distance, 1=screen-space, 2=hybrid
  int current_mode = 2; // Start with hybrid
  LODMesh *current_lod_mesh = lod_hybrid.get();

  // Performance tracking
  double last_fps_update = t0;
  int frame_count = 0;
  double fps = 0.0;
  LODMesh::LODStats last_stats;

  std::cout << "\n========================================" << std::endl;
  std::cout << "Controls:" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Camera:" << std::endl;
  std::cout << "  Mouse drag: Orbit camera" << std::endl;
  std::cout << "  W/S: Zoom in/out" << std::endl;
  std::cout << "  A/D: Pan left/right" << std::endl;
  std::cout << "  Q/E: Pan up/down" << std::endl;
  std::cout << "  [ / ]: Adjust FOV" << std::endl;
  std::cout << "  R: Reset camera" << std::endl;
  std::cout << "\nLOD Controls:" << std::endl;
  std::cout << "  Tab: Cycle LOD modes (Distance/Screen-Space/Hybrid)"
            << std::endl;
  std::cout << "  +/-: Adjust distance thresholds" << std::endl;
  std::cout << "  9/0: Adjust screen-space thresholds" << std::endl;
  std::cout << "  7/8: Adjust hybrid blend (more distance/more screen-space)"
            << std::endl;
  std::cout << "\nOther:" << std::endl;
  std::cout << "  Space: Toggle animation" << std::endl;
  std::cout << "  T: Toggle stats display" << std::endl;
  std::cout << "  1/2: Projection mode" << std::endl;
  std::cout << "  ESC: Exit" << std::endl;
  std::cout << "========================================\n" << std::endl;

  // Print current mode
  const char *mode_names[] = {"DISTANCE-BASED", "SCREEN-SPACE", "HYBRID"};
  std::cout << "Starting with: " << mode_names[current_mode] << " LOD\n"
            << std::endl;

  // Main loop
  while (r->process_events()) {
    const double t1 = pixel::core::now_sec();
    acc += (t1 - t0);
    t0 = t1;

    // ========================================================================
    // Input Handling
    // ========================================================================

    const auto &input = r->input();

    // Camera controls
    if (input.key_pressed(KEY_W))
      r->camera().zoom(-1.0f);
    if (input.key_pressed(KEY_S))
      r->camera().zoom(1.0f);
    if (input.key_pressed(KEY_A))
      r->camera().pan(-1.0f, 0);
    if (input.key_pressed(KEY_D))
      r->camera().pan(1.0f, 0);

    // Tab: Cycle LOD modes
    static bool tab_pressed = false;
    if (input.key_pressed(KEY_TAB)) {
      if (!tab_pressed) {
        current_mode = (current_mode + 1) % 3;

        if (current_mode == 0)
          current_lod_mesh = lod_distance.get();
        else if (current_mode == 1)
          current_lod_mesh = lod_screenspace.get();
        else
          current_lod_mesh = lod_hybrid.get();

        std::cout << "\n>>> Switched to: " << mode_names[current_mode]
                  << " LOD <<<" << std::endl;

        // Print relevant parameters
        if (current_mode == 0) {
          std::cout << "  Distance thresholds: "
                    << lod_distance->config().distance_high << " / "
                    << lod_distance->config().distance_medium << " / "
                    << lod_distance->config().distance_cull << std::endl;
        } else if (current_mode == 1) {
          std::cout << "  Screen-space thresholds: "
                    << (lod_screenspace->config().screenspace_high * 100)
                    << "% / "
                    << (lod_screenspace->config().screenspace_medium * 100)
                    << "% / "
                    << (lod_screenspace->config().screenspace_low * 100) << "%"
                    << std::endl;
        } else {
          std::cout << "  Hybrid weight: "
                    << (lod_hybrid->config().hybrid_screenspace_weight * 100)
                    << "% screen-space" << std::endl;
        }

        tab_pressed = true;
      }
    } else {
      tab_pressed = false;
    }

    // Projection mode
    if (input.key_pressed(KEY_1)) {
      r->camera().mode = Camera::ProjectionMode::Perspective;
      std::cout << "Perspective projection" << std::endl;
    }
    if (input.key_pressed(KEY_2)) {
      r->camera().mode = Camera::ProjectionMode::Orthographic;
      std::cout << "Orthographic projection" << std::endl;
    }

    // FOV adjustment
    static bool bracket_left_pressed = false;
    if (input.key_pressed('[')) {
      if (!bracket_left_pressed) {
        r->camera().fov = std::max(10.0f, r->camera().fov - 5.0f);
        std::cout << "FOV: " << r->camera().fov
                  << "° (narrower FOV = larger screen objects)" << std::endl;
        bracket_left_pressed = true;
      }
    } else {
      bracket_left_pressed = false;
    }

    static bool bracket_right_pressed = false;
    if (input.key_pressed(']')) {
      if (!bracket_right_pressed) {
        r->camera().fov = std::min(120.0f, r->camera().fov + 5.0f);
        std::cout << "FOV: " << r->camera().fov
                  << "° (wider FOV = smaller screen objects)" << std::endl;
        bracket_right_pressed = true;
      }
    } else {
      bracket_right_pressed = false;
    }

    // Distance threshold adjustment
    static bool plus_pressed = false;
    if (input.key_pressed('=') || input.key_pressed('+')) {
      if (!plus_pressed) {
        lod_distance->config().distance_high += 5.0f;
        lod_distance->config().distance_medium += 5.0f;
        lod_hybrid->config().distance_high += 5.0f;
        lod_hybrid->config().distance_medium += 5.0f;

        std::cout << "Distance thresholds increased: "
                  << lod_distance->config().distance_high << " / "
                  << lod_distance->config().distance_medium << std::endl;
        plus_pressed = true;
      }
    } else {
      plus_pressed = false;
    }

    static bool minus_pressed = false;
    if (input.key_pressed('-') || input.key_pressed('_')) {
      if (!minus_pressed) {
        lod_distance->config().distance_high =
            std::max(10.0f, lod_distance->config().distance_high - 5.0f);
        lod_distance->config().distance_medium =
            std::max(20.0f, lod_distance->config().distance_medium - 5.0f);
        lod_hybrid->config().distance_high =
            lod_distance->config().distance_high;
        lod_hybrid->config().distance_medium =
            lod_distance->config().distance_medium;

        std::cout << "Distance thresholds decreased: "
                  << lod_distance->config().distance_high << " / "
                  << lod_distance->config().distance_medium << std::endl;
        minus_pressed = true;
      }
    } else {
      minus_pressed = false;
    }

    // Screen-space threshold adjustment
    static bool nine_pressed = false;
    if (input.key_pressed('9')) {
      if (!nine_pressed) {
        lod_screenspace->config().screenspace_high -= 0.02f;
        lod_screenspace->config().screenspace_medium -= 0.02f;
        lod_screenspace->config().screenspace_low = std::max(
            0.005f, lod_screenspace->config().screenspace_low - 0.005f);
        lod_hybrid->config().screenspace_high =
            lod_screenspace->config().screenspace_high;
        lod_hybrid->config().screenspace_medium =
            lod_screenspace->config().screenspace_medium;
        lod_hybrid->config().screenspace_low =
            lod_screenspace->config().screenspace_low;

        std::cout << "Screen-space thresholds decreased: "
                  << (lod_screenspace->config().screenspace_high * 100)
                  << "% / "
                  << (lod_screenspace->config().screenspace_medium * 100)
                  << "% / " << (lod_screenspace->config().screenspace_low * 100)
                  << "%" << std::endl;
        nine_pressed = true;
      }
    } else {
      nine_pressed = false;
    }

    static bool zero_pressed = false;
    if (input.key_pressed(KEY_0) || input.key_pressed('0')) {
      if (!zero_pressed) {
        lod_screenspace->config().screenspace_high =
            std::min(0.5f, lod_screenspace->config().screenspace_high + 0.02f);
        lod_screenspace->config().screenspace_medium = std::min(
            0.3f, lod_screenspace->config().screenspace_medium + 0.02f);
        lod_screenspace->config().screenspace_low += 0.005f;
        lod_hybrid->config().screenspace_high =
            lod_screenspace->config().screenspace_high;
        lod_hybrid->config().screenspace_medium =
            lod_screenspace->config().screenspace_medium;
        lod_hybrid->config().screenspace_low =
            lod_screenspace->config().screenspace_low;

        std::cout << "Screen-space thresholds increased: "
                  << (lod_screenspace->config().screenspace_high * 100)
                  << "% / "
                  << (lod_screenspace->config().screenspace_medium * 100)
                  << "% / " << (lod_screenspace->config().screenspace_low * 100)
                  << "%" << std::endl;
        zero_pressed = true;
      }
    } else {
      zero_pressed = false;
    }

    // Hybrid weight adjustment
    static bool seven_pressed = false;
    if (input.key_pressed('7')) {
      if (!seven_pressed) {
        lod_hybrid->config().hybrid_screenspace_weight = std::max(
            0.0f, lod_hybrid->config().hybrid_screenspace_weight - 0.1f);
        std::cout << "Hybrid weight: "
                  << (lod_hybrid->config().hybrid_screenspace_weight * 100)
                  << "% screen-space (more distance-based)" << std::endl;
        seven_pressed = true;
      }
    } else {
      seven_pressed = false;
    }

    static bool eight_pressed = false;
    if (input.key_pressed('8')) {
      if (!eight_pressed) {
        lod_hybrid->config().hybrid_screenspace_weight = std::min(
            1.0f, lod_hybrid->config().hybrid_screenspace_weight + 0.1f);
        std::cout << "Hybrid weight: "
                  << (lod_hybrid->config().hybrid_screenspace_weight * 100)
                  << "% screen-space (more screen-space-based)" << std::endl;
        eight_pressed = true;
      }
    } else {
      eight_pressed = false;
    }

    // Reset camera
    if (input.key_pressed(KEY_R)) {
      r->camera().position = {60, 40, 60};
      r->camera().target = {0, 0, 0};
      r->camera().fov = 60.0f;
      std::cout << "Camera reset" << std::endl;
    }

    // Toggle animation
    static bool space_pressed = false;
    if (input.key_pressed(KEY_SPACE)) {
      if (!space_pressed) {
        animate_instances = !animate_instances;
        std::cout << "Animation: " << (animate_instances ? "ON" : "OFF")
                  << std::endl;
        space_pressed = true;
      }
    } else {
      space_pressed = false;
    }

    // Toggle stats
    static bool t_pressed = false;
    if (input.key_pressed('T')) {
      if (!t_pressed) {
        show_stats = !show_stats;
        t_pressed = true;
      }
    } else {
      t_pressed = false;
    }

    if (input.key_pressed(KEY_ESCAPE))
      break;

    // Mouse orbit
    if (input.mouse_pressed(0)) {
      if (!mouse_was_pressed) {
        mouse_was_pressed = true;
      } else {
        float dx = static_cast<float>(input.mouse_delta_x);
        float dy = static_cast<float>(input.mouse_delta_y);
        r->camera().orbit(dx * 0.3f, dy * 0.3f);
      }
    } else {
      mouse_was_pressed = false;
    }

    // ========================================================================
    // Fixed Timestep Update
    // ========================================================================

    while (acc >= dt) {
      if (animate_instances) {
        time_elapsed += static_cast<float>(dt);

        // Gentle wave animation
        for (size_t i = 0; i < instances.size(); ++i) {
          int x = i % GRID_SIZE;
          int z = i / GRID_SIZE;

          float phase = (x + z) * 0.1f;
          float base_y = 0.5f + 0.3f * std::sin(time_elapsed * 1.2f + phase);

          instances[i].position.y = base_y;
          instances[i].rotation.y = time_elapsed * 20.0f + phase * 10.0f;
        }

        // Update all LOD systems
        lod_distance->set_instances(instances);
        lod_screenspace->set_instances(instances);
        lod_hybrid->set_instances(instances);
      }

      acc -= dt;
    }

    // ========================================================================
    // Performance Stats
    // ========================================================================

    frame_count++;
    if (t1 - last_fps_update >= 1.0) {
      fps = frame_count / (t1 - last_fps_update);
      frame_count = 0;
      last_fps_update = t1;

      last_stats = current_lod_mesh->get_stats();

      if (show_stats) {
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "\r[" << mode_names[current_mode] << "] "
                  << "FPS: " << fps
                  << " | Total: " << last_stats.total_instances
                  << " | High: " << last_stats.instances_per_lod[0] << " ("
                  << last_stats.visible_per_lod[0] << " vis)"
                  << " | Med: " << last_stats.instances_per_lod[1] << " ("
                  << last_stats.visible_per_lod[1] << " vis)"
                  << " | Low: " << last_stats.instances_per_lod[2] << " ("
                  << last_stats.visible_per_lod[2] << " vis)"
                  << " | Culled: " << last_stats.culled << "      "
                  << std::flush;
      }
    }

    // ========================================================================
    // Rendering
    // ========================================================================

    r->begin_frame(Color(0.05f, 0.05f, 0.08f, 1.0f));

    // Draw floor
    r->draw_mesh(*floor_mesh, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, floor_mat);

    // Draw current LOD system
    RendererLOD::draw_lod(*r, *current_lod_mesh, textured_mat);

    r->end_frame();
  }

  // ============================================================================
  // Cleanup & Final Stats
  // ============================================================================

  std::cout << "\n\nShutting down..." << std::endl;
  std::cout << "\nFinal Statistics (" << mode_names[current_mode]
            << " LOD):" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Total instances: " << last_stats.total_instances << std::endl;
  std::cout << "High LOD: " << last_stats.instances_per_lod[0] << " ("
            << (100.0f * last_stats.instances_per_lod[0] /
                last_stats.total_instances)
            << "%)" << std::endl;
  std::cout << "Medium LOD: " << last_stats.instances_per_lod[1] << " ("
            << (100.0f * last_stats.instances_per_lod[1] /
                last_stats.total_instances)
            << "%)" << std::endl;
  std::cout << "Low LOD: " << last_stats.instances_per_lod[2] << " ("
            << (100.0f * last_stats.instances_per_lod[2] /
                last_stats.total_instances)
            << "%)" << std::endl;
  std::cout << "Culled: " << last_stats.culled << " ("
            << (100.0f * last_stats.culled / last_stats.total_instances) << "%)"
            << std::endl;

  uint32_t total_visible = last_stats.visible_per_lod[0] +
                           last_stats.visible_per_lod[1] +
                           last_stats.visible_per_lod[2];
  std::cout << "\nRendering efficiency:" << std::endl;
  std::cout << "  Visible: " << total_visible << "/"
            << last_stats.total_instances << " ("
            << (100.0f * total_visible / last_stats.total_instances) << "%)"
            << std::endl;
  std::cout << "========================================" << std::endl;

  return 0;
}
