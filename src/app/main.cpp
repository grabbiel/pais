// main.cpp — Minimal demo: red sphere on brown plane, blue background
#include "pixel/platform/platform.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include <vector>
#include <cmath>
#include <iostream>

using namespace pixel::renderer3d;

// --- Build a UV sphere that matches your Vertex layout (pos, normal, uv,
// color) ---
static std::unique_ptr<Mesh> make_uv_sphere(Renderer &r, float radius = 1.0f,
                                            int slices = 32, int stacks = 16) {
  std::vector<Vertex> verts;
  std::vector<uint32_t> idx;
  verts.reserve((stacks + 1) * (slices + 1));
  idx.reserve(stacks * slices * 6);

  for (int i = 0; i <= stacks; ++i) {
    float v = float(i) / float(stacks);
    float phi = v * 3.1415926535f; // [0..pi]
    float y = std::cos(phi);
    float rxy = std::sin(phi);

    for (int j = 0; j <= slices; ++j) {
      float u = float(j) / float(slices);
      float theta = u * 2.0f * 3.1415926535f; // [0..2pi]
      float x = rxy * std::cos(theta);
      float z = rxy * std::sin(theta);

      glm::vec3 n = glm::normalize(glm::vec3{x, y, z});
      glm::vec3 p = n * radius;
      verts.push_back({p, n, {u, v}, Color(1, 1, 1, 1)});
    }
  }

  auto idxAt = [&](int i, int j) { return uint32_t(i * (slices + 1) + j); };
  for (int i = 0; i < stacks; ++i) {
    for (int j = 0; j < slices; ++j) {
      uint32_t a = idxAt(i, j);
      uint32_t b = idxAt(i + 1, j);
      uint32_t c = idxAt(i, j + 1);
      uint32_t d = idxAt(i + 1, j + 1);
      // two tris per quad (counter-clockwise)
      idx.push_back(a);
      idx.push_back(b);
      idx.push_back(c);
      idx.push_back(b);
      idx.push_back(d);
      idx.push_back(c);
    }
  }

  return Mesh::create(r.device(), verts, idx);
}

int main(int, char **) {
  // Window & renderer
  pixel::platform::WindowSpec ws;
  ws.w = 1280;
  ws.h = 720;
  ws.title = "Textured Sphere Demo";
  auto r = Renderer::create(ws);

  // Load textures
  auto brick_tex = r->load_texture("assets/textures/brick.png");
  auto grass_tex = r->load_texture("assets/textures/grass.png");
  auto stone_tex = r->load_texture("assets/textures/stone.png");

  // Geometry
  auto sphere =
      make_uv_sphere(*r, /*radius*/ 1.0f, /*slices*/ 36, /*stacks*/ 18);
  auto cube = r->create_cube(1.5f);

  // If your renderer has a plane helper, use it; otherwise, keep your existing
  // ground creation
  auto ground = r->create_plane(40.0f, 40.0f, 1);

  // Materials
  Material brick{};
  brick.texture = brick_tex;
  brick.color = Color(1.0f, 1.0f, 1.0f, 1.0f); // white tint
  brick.roughness = 0.8f;
  brick.metallic = 0.0f;

  Material grass{};
  grass.texture = grass_tex;
  grass.color = Color(1.0f, 1.0f, 1.0f, 1.0f); // white tint
  grass.roughness = 0.9f;
  grass.metallic = 0.0f;

  Material stone{};
  stone.texture = stone_tex;
  stone.color = Color(1.0f, 1.0f, 1.0f, 1.0f); // white tint
  stone.roughness = 0.7f;
  stone.metallic = 0.0f;

  // Camera setup
  r->camera().mode = Camera::ProjectionMode::Perspective;
  r->camera().position = {0.0f, 2.5f, 6.0f};
  r->camera().target = {0.0f, 1.0f, 0.0f}; // look at sphere center height
  r->camera().fov = 60.0f;
  r->camera().far_clip = 200.0f;

  std::cout << "Textured Demo - Controls: LMB drag = orbit, Mouse wheel = zoom, "
               "A/D = pan, ESC = quit\n";
  std::cout << "Scene: Brick sphere, stone cube, grass ground\n";

  // Main loop
  while (r->process_events()) {
    const auto &in = r->input();

    // Quit
    if (in.keys[256])
      break; // ESC

    // Camera controls (use your built-in helpers)
    if (in.mouse_buttons[0]) {
      float dx = float(in.mouse_x - in.prev_mouse_x);
      float dy = float(in.mouse_y - in.prev_mouse_y);
      r->camera().orbit(dx, dy);
    }
    if (in.scroll_delta != 0.0) {
      r->camera().zoom(static_cast<float>(in.scroll_delta) * 0.5f);
    }
    if (in.keys['A'])
      r->camera().pan(-0.05f, 0.0f);
    if (in.keys['D'])
      r->camera().pan(+0.05f, 0.0f);

    // Frame
    r->begin_frame(Color(0.50f, 0.70f, 0.90f, 1.0f)); // sky blue background

    // Draw grass ground at y=0
    r->draw_mesh(*ground, {0.0f, 0.0f, 0.0f}, {0, 0, 0}, {1, 1, 1}, grass);

    // Draw brick sphere centered at (0,1,0) so it rests on the plane
    r->draw_mesh(*sphere, {0.0f, 1.0f, 0.0f}, {0, 0, 0}, {1, 1, 1}, brick);

    // Draw stone cube to the right at (3, 0.75, 0)
    r->draw_mesh(*cube, {3.0f, 0.75f, 0.0f}, {0, 0, 0}, {1, 1, 1}, stone);

    r->end_frame();
  }

  return 0;
}
