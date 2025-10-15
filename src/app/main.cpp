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
  ws.title = "Pixel-Life - Instanced Rendering Demo";

  auto r = Renderer::create(ws);

  // Create base mesh (will be instanced)
  auto cube_mesh = r->create_cube(1.0f);
  auto floor_mesh = r->create_plane(40.0f, 40.0f, 10);

  // Setup camera
  r->camera().position = {20, 15, 20};
  r->camera().target = {0, 0, 0};
  r->camera().mode = Camera::ProjectionMode::Perspective;

  // Create instanced mesh for cubes
  // We'll create a grid of 20x20 = 400 cubes efficiently
  auto instanced_cubes =
      RendererInstanced::create_instanced_mesh(*cube_mesh, 500);

  // Generate instance data - 20x20 grid
  auto instances = RendererInstanced::create_grid(20, 20, 2.5f, 0.5f);
  instanced_cubes->set_instances(instances);

  std::cout << "Created " << instances.size() << " instanced cubes"
            << std::endl;

  // Create another instanced mesh for a circle of rotating cubes
  auto circle_cubes = RendererInstanced::create_instanced_mesh(*cube_mesh, 100);
  auto circle_instances = RendererInstanced::create_circle(50, 15.0f, 5.0f);
  circle_cubes->set_instances(circle_instances);

  // Create random scattered cubes
  auto random_cubes = RendererInstanced::create_instanced_mesh(*cube_mesh, 200);
  auto random_instances = RendererInstanced::create_random(
      100, Vec3{-30, 10, -30}, Vec3{30, 30, 30});
  random_cubes->set_instances(random_instances);

  // Floor entity (not instanced, just one)
  Material floor_mat;
  floor_mat.diffuse = Color(0.3f, 0.3f, 0.35f, 1.0f);

  const double dt = 1.0 / 60.0;
  double acc = 0.0, t0 = pixel::core::now_sec();
  float time_elapsed = 0.0f;

  bool mouse_was_pressed = false;
  int render_mode = 0; // 0 = grid, 1 = circle, 2 = random, 3 = all

  std::cout << "\nControls:" << std::endl;
  std::cout << "  Mouse drag: Orbit camera" << std::endl;
  std::cout << "  W/S: Zoom in/out" << std::endl;
  std::cout << "  1/2: Switch projection mode" << std::endl;
  std::cout << "  Space: Cycle render modes" << std::endl;
  std::cout << "  R: Reset camera" << std::endl;
  std::cout << "  ESC: Exit" << std::endl;

  // Main loop
  while (r->process_events()) {
    const double t1 = pixel::core::now_sec();
    acc += (t1 - t0);
    t0 = t1;

    // Handle input
    const auto &input = r->input();

    // Camera controls
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
    if (input.key_pressed(KEY_SPACE)) {
      static bool space_was_pressed = false;
      if (!space_was_pressed) {
        render_mode = (render_mode + 1) % 4;
        const char *modes[] = {"Grid", "Circle", "Random", "All"};
        std::cout << "Render mode: " << modes[render_mode] << std::endl;
        space_was_pressed = true;
      }
    } else {
      static bool space_was_pressed = false;
      space_was_pressed = false;
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

      // Animate grid instances - make them bob up and down with wave pattern
      for (size_t i = 0; i < instances.size(); ++i) {
        float phase = (i % 20) * 0.3f + (i / 20) * 0.3f;
        instances[i].position.y =
            0.5f + 0.5f * std::sin(time_elapsed * 2.0f + phase);
        instances[i].rotation.y = time_elapsed * 30.0f;
      }
      instanced_cubes->set_instances(instances);

      // Animate circle instances - rotate around center
      for (size_t i = 0; i < circle_instances.size(); ++i) {
        float angle =
            (float)i / circle_instances.size() * 6.28318530718f + time_elapsed;
        circle_instances[i].position.x = 15.0f * std::cos(angle);
        circle_instances[i].position.z = 15.0f * std::sin(angle);
        circle_instances[i].position.y =
            5.0f + 3.0f * std::sin(time_elapsed * 2.0f + i * 0.1f);
        circle_instances[i].rotation.y = angle * 57.2957795131f;
        circle_instances[i].rotation.x = time_elapsed * 50.0f;
      }
      circle_cubes->set_instances(circle_instances);

      // Animate random instances - gentle floating motion
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

    // Draw instanced meshes based on mode
    Material cube_mat;

    if (render_mode == 0 || render_mode == 3) {
      // Draw grid
      RendererInstanced::draw_instanced(*r, *instanced_cubes, cube_mat);
    }

    if (render_mode == 1 || render_mode == 3) {
      // Draw circle
      RendererInstanced::draw_instanced(*r, *circle_cubes, cube_mat);
    }

    if (render_mode == 2 || render_mode == 3) {
      // Draw random
      RendererInstanced::draw_instanced(*r, *random_cubes, cube_mat);
    }

    r->end_frame();
  }

  std::cout << "\nShutting down..." << std::endl;
  return 0;
}
