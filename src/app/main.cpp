#include "pixel/platform/platform.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using pixel::renderer3d::Color;
using pixel::renderer3d::InstanceData;
using pixel::renderer3d::Material;
using pixel::renderer3d::Renderer;
using pixel::renderer3d::RendererInstanced;
using pixel::renderer3d::Vec3;

namespace {
struct DemoDiagnostics {
  bool renderer_initialized = false;
  bool instanced_shader_loaded = false;
  bool instanced_mesh_created = false;
  std::string backend_name;

  size_t instance_count = 0;
  size_t frames_rendered = 0;
  size_t instance_updates = 0;

  double last_log_time = 0.0;
  bool reported_instance_mismatch = false;

  std::vector<std::string> failures;

  void log_success(const std::string &message) const {
    std::cout << "[ok] " << message << std::endl;
  }

  void log_failure(const std::string &message) {
    std::cerr << "[failure] " << message << std::endl;
    failures.push_back(message);
  }

  void log_status(double time_seconds) {
    last_log_time = time_seconds;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[status] t=" << time_seconds << "s"
              << " | frames=" << frames_rendered
              << " | instance_updates=" << instance_updates
              << " | active_instances=" << instance_count << std::endl;

    if (!failures.empty()) {
      std::cout << "         Issues detected: " << failures.size() << std::endl;
      for (const auto &failure : failures) {
        std::cout << "           - " << failure << std::endl;
      }
    }
  }

  void log_final() const {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Demo completed on backend: "
              << (backend_name.empty() ? "unknown" : backend_name)
              << std::endl;
    std::cout << "Total frames rendered: " << frames_rendered << std::endl;
    std::cout << "Instance buffer updates: " << instance_updates << std::endl;
    std::cout << "Tracked instance count: " << instance_count << std::endl;
    if (failures.empty()) {
      std::cout << "No failures detected." << std::endl;
    } else {
      std::cout << "Failures detected (" << failures.size() << "):" << std::endl;
      for (const auto &failure : failures) {
        std::cout << "  - " << failure << std::endl;
      }
    }
    std::cout << "========================================\n" << std::endl;
  }
};

Color palette_from_index(size_t index) {
  constexpr Color base_colors[] = {
      {0.75f, 0.85f, 1.0f, 1.0f}, {0.85f, 0.75f, 1.0f, 1.0f},
      {0.75f, 1.0f, 0.85f, 1.0f}, {1.0f, 0.85f, 0.75f, 1.0f},
      {0.95f, 0.95f, 0.75f, 1.0f}, {0.85f, 0.95f, 1.0f, 1.0f}};
  return base_colors[index % std::size(base_colors)];
}

std::vector<InstanceData> create_instance_cloud(size_t grid, size_t layers,
                                                float spacing) {
  std::vector<InstanceData> instances;
  instances.reserve(grid * grid * layers);

  float half = static_cast<float>(grid - 1) * spacing * 0.5f;

  for (size_t layer = 0; layer < layers; ++layer) {
    for (size_t x = 0; x < grid; ++x) {
      for (size_t z = 0; z < grid; ++z) {
        InstanceData data;
        float world_x = static_cast<float>(x) * spacing - half;
        float world_z = static_cast<float>(z) * spacing - half;
        float world_y = static_cast<float>(layer) * spacing * 0.5f;

        data.position = Vec3(world_x, world_y, world_z);
        data.scale = Vec3(0.8f, 0.8f + 0.1f * static_cast<float>(layer), 0.8f);
        data.rotation = Vec3(0.0f, 0.0f, 0.0f);
        data.color = palette_from_index(layer + x + z);
        data.culling_radius = spacing * 0.9f;
        data.texture_index = 0.0f;
        data.lod_transition_alpha = 1.0f;

        instances.push_back(data);
      }
    }
  }

  return instances;
}

} // namespace

int main() {
  DemoDiagnostics diagnostics;

  try {
    pixel::platform::WindowSpec spec;
    spec.w = 1280;
    spec.h = 720;
    spec.title = "Pixel Life - Instanced Rendering Diagnostics";

    auto renderer = Renderer::create(spec);
    if (!renderer) {
      diagnostics.log_failure("Renderer creation returned null");
      diagnostics.log_final();
      return EXIT_FAILURE;
    }

    diagnostics.renderer_initialized = true;
    diagnostics.backend_name = renderer->backend_name();
    diagnostics.log_success("Renderer created successfully");

    auto &camera = renderer->camera();
    camera.position = Vec3(0.0f, 25.0f, 35.0f);
    camera.target = Vec3(0.0f, 0.0f, 0.0f);
    camera.up = Vec3(0.0f, 1.0f, 0.0f);
    camera.near_clip = 0.1f;
    camera.far_clip = 500.0f;

    auto base_mesh = renderer->create_cube(1.0f);
    if (!base_mesh) {
      diagnostics.log_failure("Failed to create base cube mesh");
      diagnostics.log_final();
      return EXIT_FAILURE;
    }
    diagnostics.log_success("Base mesh created (cube primitive)");

    constexpr size_t kGridDimension = 20;
    constexpr size_t kLayerCount = 3;
    constexpr float kSpacing = 2.5f;
    std::vector<InstanceData> initial_instances =
        create_instance_cloud(kGridDimension, kLayerCount, kSpacing);

    diagnostics.instance_count = initial_instances.size();
    diagnostics.log_success("Prepared " +
                            std::to_string(diagnostics.instance_count) +
                            " instances for instanced rendering");

    auto *device = renderer->device();
    if (!device) {
      diagnostics.log_failure("Renderer returned a null device pointer");
      diagnostics.log_final();
      return EXIT_FAILURE;
    }

    auto instanced_mesh =
        RendererInstanced::create_instanced_mesh(device, *base_mesh,
                                                 diagnostics.instance_count);
    if (!instanced_mesh) {
      diagnostics.log_failure("Failed to allocate instanced mesh resources");
      diagnostics.log_final();
      return EXIT_FAILURE;
    }
    diagnostics.instanced_mesh_created = true;
    diagnostics.log_success("Instanced mesh allocated on GPU");

    instanced_mesh->set_instances(initial_instances);

    auto *instanced_shader =
        renderer->get_shader(renderer->instanced_shader());
    diagnostics.instanced_shader_loaded = instanced_shader != nullptr;
    if (!diagnostics.instanced_shader_loaded) {
      diagnostics.log_failure("Instanced shader handle is invalid");
      diagnostics.log_final();
      return EXIT_FAILURE;
    }

    Material base_material;
    base_material.blend_mode = Material::BlendMode::Opaque;
    base_material.color = Color(0.9f, 0.95f, 1.0f, 1.0f);
    base_material.depth_test = true;
    base_material.depth_write = true;

    const std::vector<InstanceData> baseline_instances = initial_instances;

    diagnostics.log_status(0.0);

    double last_time = renderer->time();
    const size_t animated_instance_count =
        std::min<size_t>(64, baseline_instances.size());

    while (renderer->process_events()) {
      double now = renderer->time();
      double dt = now - last_time;
      last_time = now;

      if (instanced_mesh->instance_count() != diagnostics.instance_count &&
          !diagnostics.reported_instance_mismatch) {
        diagnostics.reported_instance_mismatch = true;
        diagnostics.log_failure("Instance count mismatch detected: expected " +
                                std::to_string(diagnostics.instance_count) +
                                ", got " +
                                std::to_string(instanced_mesh->instance_count()));
      }

      for (size_t i = 0; i < animated_instance_count; ++i) {
        InstanceData animated = baseline_instances[i];
        float angle = static_cast<float>(now * 0.5 + static_cast<double>(i) * 0.1);
        animated.rotation = Vec3(0.25f * std::sin(angle), angle, 0.0f);
        animated.position.y = baseline_instances[i].position.y +
                              0.5f * std::sin(static_cast<float>(now) +
                                              static_cast<float>(i) * 0.25f);
        animated.color = Color(0.6f + 0.4f * std::sin(angle),
                               0.6f + 0.4f * std::sin(angle * 0.7f + 1.3f),
                               0.8f + 0.2f * std::cos(angle * 0.5f), 1.0f);
        animated.lod_transition_alpha =
            0.5f + 0.5f * std::sin(static_cast<float>(now) * 0.8f +
                                   static_cast<float>(i) * 0.2f);

        instanced_mesh->update_instance(i, animated);
        diagnostics.instance_updates++;
      }

      renderer->begin_frame(Color(0.02f, 0.02f, 0.05f, 1.0f));
      RendererInstanced::draw_instanced(*renderer, *instanced_mesh,
                                        base_material);
      renderer->end_frame();

      diagnostics.frames_rendered++;

      if (now - diagnostics.last_log_time >= 2.0) {
        diagnostics.log_status(now);
      }

      (void)dt; // dt is currently unused but kept for potential debugging.
    }

    diagnostics.log_final();
    return diagnostics.failures.empty() ? EXIT_SUCCESS : EXIT_FAILURE;

  } catch (const std::exception &e) {
    diagnostics.log_failure(std::string("Unhandled exception: ") + e.what());
    diagnostics.log_final();
    return EXIT_FAILURE;
  }
}
