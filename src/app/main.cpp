#include "pixel/platform/platform.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using pixel::renderer3d::Color;
using pixel::renderer3d::InstanceData;
using pixel::renderer3d::Material;
using pixel::renderer3d::Mesh;
using pixel::renderer3d::Renderer;
using pixel::renderer3d::RendererInstanced;
using pixel::renderer3d::Vec3;

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct DemoLogger {
  void info(const std::string &message) const {
    std::cout << "[demo] " << message << std::endl;
  }

  void error(const std::string &message) {
    std::cerr << "[demo:error] " << message << std::endl;
    failures.push_back(message);
  }

  void status(double time_seconds, size_t frames, size_t updates,
              size_t instances) {
    last_status_time = time_seconds;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[demo:status] t=" << time_seconds << "s"
              << " | frames=" << frames << " | instance_updates=" << updates
              << " | active_instances=" << instances << std::endl;

    if (!failures.empty()) {
      std::cout << "               Issues detected: " << failures.size()
                << std::endl;
      for (const auto &failure : failures) {
        std::cout << "                 - " << failure << std::endl;
      }
    }
  }

  void final_report(size_t frames, size_t updates, size_t instances,
                    const std::string &backend) const {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Instanced sphere cloud demo finished" << std::endl;
    std::cout << "Renderer backend: "
              << (backend.empty() ? std::string{"unknown"} : backend)
              << std::endl;
    std::cout << "Frames rendered: " << frames << std::endl;
    std::cout << "Instance updates: " << updates << std::endl;
    std::cout << "Instances tracked: " << instances << std::endl;
    if (failures.empty()) {
      std::cout << "Status: success" << std::endl;
    } else {
      std::cout << "Status: issues detected (" << failures.size()
                << ")" << std::endl;
      for (const auto &failure : failures) {
        std::cout << "  - " << failure << std::endl;
      }
    }
    std::cout << "========================================\n" << std::endl;
  }

  double last_status_time = 0.0;
  std::vector<std::string> failures;
};

struct FloatMotion {
  Vec3 base_position{0.0f, 0.0f, 0.0f};
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float phase = 0.0f;
  float rotation_speed = 0.25f;
};

std::unique_ptr<Mesh> create_uv_sphere(Renderer &renderer, float radius,
                                       int latitude_segments,
                                       int longitude_segments) {
  auto *device = renderer.device();
  if (!device) {
    return nullptr;
  }

  const int lat_segments = std::max(3, latitude_segments);
  const int lon_segments = std::max(3, longitude_segments);

  std::vector<pixel::renderer3d::Vertex> vertices;
  vertices.reserve(static_cast<size_t>((lat_segments + 1) * (lon_segments + 1)));

  for (int y = 0; y <= lat_segments; ++y) {
    float v = static_cast<float>(y) / static_cast<float>(lat_segments);
    float theta = v * kPi;

    float sin_theta = std::sin(theta);
    float cos_theta = std::cos(theta);

    for (int x = 0; x <= lon_segments; ++x) {
      float u = static_cast<float>(x) / static_cast<float>(lon_segments);
      float phi = u * 2.0f * kPi;

      float sin_phi = std::sin(phi);
      float cos_phi = std::cos(phi);

      Vec3 normal{cos_phi * sin_theta, cos_theta, sin_phi * sin_theta};
      Vec3 position = normal * radius;
      pixel::renderer3d::Vec2 texcoord{u, 1.0f - v};

      vertices.push_back({position, normal, texcoord, Color(1.0f, 1.0f, 1.0f, 1.0f)});
    }
  }

  std::vector<uint32_t> indices;
  indices.reserve(static_cast<size_t>(lat_segments * lon_segments) * 6);

  const int stride = lon_segments + 1;
  for (int y = 0; y < lat_segments; ++y) {
    for (int x = 0; x < lon_segments; ++x) {
      uint32_t i0 = static_cast<uint32_t>(y * stride + x);
      uint32_t i1 = static_cast<uint32_t>(i0 + 1);
      uint32_t i2 = static_cast<uint32_t>((y + 1) * stride + x);
      uint32_t i3 = static_cast<uint32_t>(i2 + 1);

      if (y != 0) {
        indices.push_back(i0);
        indices.push_back(i2);
        indices.push_back(i1);
      }

      if (y != lat_segments - 1) {
        indices.push_back(i1);
        indices.push_back(i2);
        indices.push_back(i3);
      }
    }
  }

  return Mesh::create(device, vertices, indices);
}

} // namespace

int main() {
  DemoLogger logger;
  size_t frames_rendered = 0;
  size_t instance_updates = 0;
  std::string backend_name;

  try {
    logger.info("Starting instanced sphere cloud demo");

    pixel::platform::WindowSpec spec;
    spec.w = 1280;
    spec.h = 720;
    spec.title = "Pixel Life - Floating Spheres";

    auto renderer = Renderer::create(spec);
    if (!renderer) {
      logger.error("Renderer creation failed: returned null");
      logger.final_report(frames_rendered, instance_updates, 0, backend_name);
      return EXIT_FAILURE;
    }

    backend_name = renderer->backend_name();
    logger.info("Renderer created using backend: " + backend_name);

    auto &camera = renderer->camera();
    camera.position = Vec3(0.0f, 35.0f, 65.0f);
    camera.target = Vec3(0.0f, 10.0f, 0.0f);
    camera.up = Vec3(0.0f, 1.0f, 0.0f);
    camera.near_clip = 0.1f;
    camera.far_clip = 500.0f;

    logger.info("Creating sphere mesh");
    auto sphere_mesh = create_uv_sphere(*renderer, 0.75f, 32, 32);
    if (!sphere_mesh) {
      logger.error("Failed to create UV sphere mesh");
      logger.final_report(frames_rendered, instance_updates, 0, backend_name);
      return EXIT_FAILURE;
    }

    constexpr size_t kSphereCount = 4096;
    logger.info("Preparing " + std::to_string(kSphereCount) +
                " instanced sphere transforms");

    std::vector<InstanceData> initial_instances;
    initial_instances.reserve(kSphereCount);

    std::vector<FloatMotion> motions;
    motions.reserve(kSphereCount);

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> spread_dist(-45.0f, 45.0f);
    std::uniform_real_distribution<float> height_dist(4.0f, 18.0f);
    std::uniform_real_distribution<float> amplitude_dist(1.5f, 6.0f);
    std::uniform_real_distribution<float> frequency_dist(0.15f, 0.6f);
    std::uniform_real_distribution<float> phase_dist(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> rotation_speed_dist(0.05f, 0.25f);

    for (size_t i = 0; i < kSphereCount; ++i) {
      FloatMotion motion;
      motion.base_position =
          Vec3(spread_dist(rng), height_dist(rng), spread_dist(rng));
      motion.amplitude = amplitude_dist(rng);
      motion.frequency = frequency_dist(rng);
      motion.phase = phase_dist(rng);
      motion.rotation_speed = rotation_speed_dist(rng);

      InstanceData data;
      data.position = motion.base_position;
      data.rotation = Vec3(0.0f, 0.0f, 0.0f);
      data.scale = Vec3(1.0f, 1.0f, 1.0f);
      data.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
      data.culling_radius = 1.0f;
      data.texture_index = 0.0f;
      data.lod_transition_alpha = 1.0f;

      initial_instances.push_back(data);
      motions.push_back(motion);
    }

    auto *device = renderer->device();
    if (!device) {
      logger.error("Renderer device pointer is null");
      logger.final_report(frames_rendered, instance_updates, 0, backend_name);
      return EXIT_FAILURE;
    }

    auto instanced_mesh = RendererInstanced::create_instanced_mesh(
        device, *sphere_mesh, kSphereCount);
    if (!instanced_mesh) {
      logger.error("Failed to allocate instanced mesh");
      logger.final_report(frames_rendered, instance_updates, 0, backend_name);
      return EXIT_FAILURE;
    }

    instanced_mesh->set_instances(initial_instances);
    logger.info("Instanced mesh uploaded to GPU");

    Material base_material;
    base_material.blend_mode = Material::BlendMode::Opaque;
    base_material.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
    base_material.depth_test = true;
    base_material.depth_write = true;

    logger.status(0.0, frames_rendered, instance_updates,
                  instanced_mesh->instance_count());

    while (renderer->process_events()) {
      double now = renderer->time();

      float t = static_cast<float>(now);
      for (size_t i = 0; i < kSphereCount; ++i) {
        InstanceData updated = initial_instances[i];
        const FloatMotion &motion = motions[i];

        float wave = std::sin(t * motion.frequency + motion.phase);
        updated.position = motion.base_position;
        updated.position.y = motion.base_position.y + motion.amplitude * wave;
        updated.rotation = Vec3(0.0f, t * motion.rotation_speed, 0.0f);

        instanced_mesh->update_instance(i, updated);
        ++instance_updates;
      }

      renderer->begin_frame(Color(0.01f, 0.015f, 0.03f, 1.0f));
      RendererInstanced::draw_instanced(*renderer, *instanced_mesh,
                                        base_material);
      renderer->end_frame();

      ++frames_rendered;

      if (now - logger.last_status_time >= 2.5) {
        logger.status(now, frames_rendered, instance_updates,
                      instanced_mesh->instance_count());
      }
    }

    logger.final_report(frames_rendered, instance_updates,
                        instanced_mesh->instance_count(), backend_name);
    return logger.failures.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (const std::exception &e) {
    logger.error(std::string{"Unhandled exception: "}.append(e.what()));
    logger.final_report(frames_rendered, instance_updates, 0, backend_name);
    return EXIT_FAILURE;
  }
}
