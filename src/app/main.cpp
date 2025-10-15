#include "pixel/core/clock.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"
#include <cmath>
#include <iostream>

using namespace pixel::renderer3d;

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  // Setup window
  pixel::platform::WindowSpec ws;
  ws.w = 1280;
  ws.h = 720;
  ws.title = "Pixel-Life - Texture Array Demo";

  auto r = Renderer::create(ws);

  // Create base mesh
  auto cube_mesh = r->create_cube(1.0f);
  auto floor_mesh = r->create_plane(40.0f, 40.0f, 10);

  // Setup camera
  r->camera().position = {20, 15, 20};
  r->camera().target = {0, 0, 0};
  r->camera().mode = Camera::ProjectionMode::Perspective;

  // ============================================================================
  // TEXTURE ARRAY SETUP
  // ============================================================================

  // Option 1: Load from files
  std::vector<std::string> texture_paths = {
      "assets/textures/brick.png", "assets/textures/stone.png",
      "assets/textures/wood.png",  "assets/textures/metal.png",
      "assets/textures/grass.png", "assets/textures/dirt.png"};

  TextureArrayID tex_array = INVALID_TEXTURE_ARRAY;

  try {
    // Try to load textures from files
    tex_array = r->load_texture_array(texture_paths);
  } catch (const std::exception &e) {
    std::cout << "Could not load textures from files: " << e.what()
              << std::endl;
    std::cout << "Creating procedural texture array instead..." << std::endl;

    // Option 2: Create procedural texture array
    const int TEX_SIZE = 64;
    const int NUM_TEXTURES = 6;
    tex_array = r->create_texture_array(TEX_SIZE, TEX_SIZE, NUM_TEXTURES);

    // Generate procedural textures for each layer
    std::vector<uint8_t> tex_data(TEX_SIZE * TEX_SIZE * 4);

    for (int layer = 0; layer < NUM_TEXTURES; ++layer) {
      // Generate different pattern for each layer
      for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
          int idx = (y * TEX_SIZE + x) * 4;

          // Create checkerboard pattern with layer-specific colors
          bool checker = ((x / 8) + (y / 8)) % 2 == 0;
          float base = checker ? 0.8f : 0.3f;

          // Different color for each layer
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

    std::cout << "Created procedural texture array with " << NUM_TEXTURES
              << " layers" << std::endl;
  }

  auto array_info = r->get_texture_array_info(tex_array);
  std::cout << "Texture array: " << array_info.width << "x" << array_info.height
            << " with " << array_info.layers << " layers" << std::endl;

  // ============================================================================
  // CREATE INSTANCED MESHES WITH TEXTURE INDICES
  // ============================================================================

  // Create instanced mesh
  auto instanced_cubes =
      RendererInstanced::create_instanced_mesh(*cube_mesh, 500);

  // Generate instance data
  auto instances = RendererInstanced::create_grid(20, 20, 2.5f, 0.5f);

  // Assign texture indices - each cube gets a different texture from the array
  RendererInstanced::assign_texture_indices(instances, array_info.layers);
  // Or use random assignment:
  // RendererInstanced::assign_random_texture_indices(instances,
  // array_info.layers);

  instanced_cubes->set_instances(instances);

  std::cout << "Created " << instances.size()
            << " instanced cubes with textures" << std::endl;

  // Create another set with random textures
  auto random_cubes = RendererInstanced::create_instanced_mesh(*cube_mesh, 200);
  auto random_instances = RendererInstanced::create_random(
      100, Vec3{-30, 10, -30}, Vec3{30, 30, 30});
  RendererInstanced::assign_random_texture_indices(random_instances,
                                                   array_info.layers);
  random_cubes->set_instances(random_instances);

  // Floor material
  Material floor_mat;
  floor_mat.diffuse = Color(0.3f, 0.3f, 0.35f, 1.0f);

  // Material with texture array
  Material textured_mat;
  textured_mat.texture_array = tex_array;

  const double dt = 1.0 / 60.0;
  double acc = 0.0, t0 = pixel::core::now_sec();
  float time_elapsed = 0.0f;

  bool mouse_was_pressed = false;
  int render_mode = 0; // 0 = grid, 1 = random, 2 = both

  std::cout << "\nControls:" << std::endl;
  std::cout << "  Mouse drag: Orbit camera" << std::endl;
  std::cout << "  W/S: Zoom in/out" << std::endl;
  std::cout << "  1/2: Switch projection mode" << std::endl;
  std::cout << "  Space: Cycle render modes" << std::endl;
  std::cout << "  T: Cycle texture assignment" << std::endl;
  std::cout << "  R: Reset camera" << std::endl;
  std::cout << "  ESC: Exit" << std::endl;

  // Main loop
  while (r->process_events()) {
    const double t1 = pixel::core::now_sec();
    acc += (t1 - t0);
    t0 = t1;

    // Handle input
    const auto &input = r->input();

    if (input.key_pressed(KEY_W)) {
      r->camera().zoom(-0.5f);
    }
    if (input.key_pressed(KEY_S)) {
      r->camera().zoom(0.5f);
    }
    if (input.key_pressed(KEY_1)) {
      r->camera().mode = Camera::ProjectionMode::Perspective;
    }
    if (input.key_pressed(KEY_2)) {
      r->camera().mode = Camera::ProjectionMode::Orthographic;
    }
    if (input.key_pressed(KEY_R)) {
      r->camera().position = {20, 15, 20};
      r->camera().target = {0, 0, 0};
    }

    // Toggle render mode
    static bool space_pressed = false;
    if (input.key_pressed(KEY_SPACE)) {
      if (!space_pressed) {
        render_mode = (render_mode + 1) % 3;
        const char *modes[] = {"Grid", "Random", "Both"};
        std::cout << "Render mode: " << modes[render_mode] << std::endl;
        space_pressed = true;
      }
    } else {
      space_pressed = false;
    }

    // Reassign textures on T key
    static bool t_pressed = false;
    if (input.key_pressed('T')) {
      if (!t_pressed) {
        static int assignment_mode = 0;
        assignment_mode = (assignment_mode + 1) % 2;

        if (assignment_mode == 0) {
          std::cout << "Sequential texture assignment" << std::endl;
          RendererInstanced::assign_texture_indices(instances,
                                                    array_info.layers);
        } else {
          std::cout << "Random texture assignment" << std::endl;
          RendererInstanced::assign_random_texture_indices(instances,
                                                           array_info.layers);
        }
        instanced_cubes->set_instances(instances);
        t_pressed = true;
      }
    } else {
      t_pressed = false;
    }

    if (input.key_pressed(KEY_ESCAPE)) {
      break;
    }

    // Mouse orbit
    if (input.mouse_pressed(0)) {
      if (!mouse_was_pressed) {
        mouse_was_pressed = true;
      } else {
        float dx = static_cast<float>(input.mouse_delta_x);
        float dy = static_cast<float>(input.mouse_delta_y);
        r->camera().orbit(dx * 0.5f, dy * 0.5f);
      }
    } else {
      mouse_was_pressed = false;
    }

    // Fixed timestep update
    while (acc >= dt) {
      time_elapsed += static_cast<float>(dt);

      // Animate instances
      for (size_t i = 0; i < instances.size(); ++i) {
        float phase = (i % 20) * 0.3f + (i / 20) * 0.3f;
        instances[i].position.y =
            0.5f + 0.5f * std::sin(time_elapsed * 2.0f + phase);
        instances[i].rotation.y = time_elapsed * 30.0f;
      }
      instanced_cubes->set_instances(instances);

      // Animate random instances
      for (size_t i = 0; i < random_instances.size(); ++i) {
        random_instances[i].rotation.y += 20.0f * dt;
        random_instances[i].rotation.x += 15.0f * dt;
      }
      random_cubes->set_instances(random_instances);

      acc -= dt;
    }

    // Rendering
    r->begin_frame(Color(0.1f, 0.1f, 0.15f, 1.0f));

    // Draw floor
    r->draw_mesh(*floor_mesh, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, floor_mat);

    // Draw instanced meshes with texture array
    if (render_mode == 0 || render_mode == 2) {
      RendererInstanced::draw_instanced(*r, *instanced_cubes, textured_mat);
    }

    if (render_mode == 1 || render_mode == 2) {
      RendererInstanced::draw_instanced(*r, *random_cubes, textured_mat);
    }

    r->end_frame();
  }

  std::cout << "\nShutting down..." << std::endl;
  return 0;
}
