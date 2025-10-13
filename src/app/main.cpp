#include "pixel/core/clock.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include <cmath>
#include <vector>

using namespace pixel::renderer3d;

struct Entity {
  Vec3 position;
  Vec3 rotation{0, 0, 0};
  Vec3 scale{1, 1, 1};
  bool is_floor = false;
};

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  pixel::platform::WindowSpec ws;
  ws.w = 1280;
  ws.h = 720;
  ws.title = "Pixel-Life (Pure OpenGL 2.5D)";

  auto r = Renderer::create(ws);

  // Create meshes
  auto cube_mesh = r->create_cube(1.0f);
  auto floor_mesh = r->create_plane(20.0f, 20.0f, 10);

  // Setup camera
  r->camera().position = {10, 8, 10};
  r->camera().target = {0, 0, 0};
  r->camera().mode = Camera::ProjectionMode::Perspective;

  // Create entities
  std::vector<Entity> entities;

  // Floor
  Entity floor;
  floor.position = {0, 0, 0};
  floor.scale = {20, 1, 20};
  floor.is_floor = true;
  entities.push_back(floor);

  // Cubes
  for (int i = 0; i < 5; ++i) {
    Entity cube;
    cube.position = {(float)(i - 2) * 3.0f, 0.5f, (float)((i % 2) * 4 - 2)};
    entities.push_back(cube);
  }

  const double dt = 1.0 / 60.0;
  double acc = 0.0, t0 = pixel::core::now_sec();
  float time_elapsed = 0.0f;

  bool mouse_was_pressed = false;

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
      r->camera().position = {10, 8, 10};
      r->camera().target = {0, 0, 0};
    }
    if (input.key_pressed(KEY_ESCAPE)) {
      break;
    }

    // Mouse orbit
    if (input.mouse_pressed(0)) { // Left mouse button
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

      // Rotate cubes
      for (size_t i = 1; i < entities.size(); ++i) {
        entities[i].rotation.y += 30.0f * dt;
      }

      acc -= dt;
    }

    // Rendering
    r->begin_frame(Color(0.1f, 0.1f, 0.15f, 1.0f));

    // Draw all entities
    for (size_t i = 0; i < entities.size(); ++i) {
      const auto &ent = entities[i];
      Material mat;

      if (ent.is_floor) {
        mat.diffuse = Color(0.3f, 0.3f, 0.35f, 1.0f);
        r->draw_mesh(*floor_mesh, ent.position, ent.rotation, ent.scale, mat);
      } else {
        float hue = i * 0.3f;
        mat.diffuse = Color(0.5f + 0.5f * std::sin(hue),
                            0.5f + 0.5f * std::sin(hue + 2.0f),
                            0.5f + 0.5f * std::sin(hue + 4.0f), 1.0f);
        r->draw_mesh(*cube_mesh, ent.position, ent.rotation, ent.scale, mat);
      }
    }

    r->end_frame();
  }

  return 0;
}
