// src/renderer3d/lod.cpp - Enhanced with detailed logging
#include "pixel/renderer3d/lod.hpp"
#include "pixel/platform/shader_loader.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <iomanip>
#include <string>
#include <span>

namespace pixel::renderer3d {

// Global logging flag - set this to true to enable verbose LOD logging
static bool g_verbose_lod_logging = false;
static int g_log_frame_counter = 0;

// ============================================================================
// Screen-Space Utilities
// ============================================================================

namespace screen_space {

float calculate_sphere_screen_size(const Vec3 &world_pos, float world_radius,
                                   const float *view, const float *proj,
                                   int viewport_height) {
  // Transform to view space
  glm::vec4 view_pos = glm::make_mat4(view) *
                       glm::vec4(world_pos.x, world_pos.y, world_pos.z, 1.0f);
  float distance = std::abs(view_pos.z);

  if (distance < 0.001f)
    return 1.0f;

  // Get FOV from projection matrix
  glm::mat4 proj_mat = glm::make_mat4(proj);
  float fov_y_rad = 2.0f * std::atan(1.0f / proj_mat[1][1]);

  // Calculate screen-space size as fraction of screen height
  float size_fraction = (world_radius / distance) / std::tan(fov_y_rad * 0.5f);

  return size_fraction;
}

} // namespace screen_space

// ============================================================================
// LODMesh Implementation
// ============================================================================

std::unique_ptr<LODMesh>
LODMesh::create(rhi::Device *device, const Mesh &high_detail,
                const Mesh &medium_detail, const Mesh &low_detail,
                size_t max_instances_per_lod, const LODConfig &config) {
  auto lod_mesh = std::unique_ptr<LODMesh>(new LODMesh());

  lod_mesh->device_ = device;
  lod_mesh->max_instances_per_lod_ = max_instances_per_lod;
  lod_mesh->config_ = config;

  lod_mesh->lod_meshes_[0] =
      InstancedMesh::create(device, high_detail, max_instances_per_lod);
  lod_mesh->lod_meshes_[1] =
      InstancedMesh::create(device, medium_detail, max_instances_per_lod);
  lod_mesh->lod_meshes_[2] =
      InstancedMesh::create(device, low_detail, max_instances_per_lod);

  lod_mesh->use_gpu_lod_ =
      config.gpu.enabled &&
      lod_mesh->initialize_gpu_resources(max_instances_per_lod);

  std::cout << "Created LOD mesh system:" << std::endl;
  std::cout << "  Mode: ";
  switch (config.mode) {
  case LODMode::Distance:
    std::cout << "Distance-based";
    break;
  case LODMode::ScreenSpace:
    std::cout << "Screen-space";
    break;
  case LODMode::Hybrid:
    std::cout << "Hybrid";
    break;
  }
  std::cout << std::endl;
  std::cout << "  High detail: " << high_detail.vertex_count() << " verts"
            << std::endl;
  std::cout << "  Medium detail: " << medium_detail.vertex_count() << " verts"
            << std::endl;
  std::cout << "  Low detail: " << low_detail.vertex_count() << " verts"
            << std::endl;
  std::cout << "  GPU LOD: "
            << (config.gpu.enabled
                    ? (lod_mesh->use_gpu_lod_ ? "Enabled"
                                               : "Disabled (resource init failed)")
                    : "Disabled (config)")
            << std::endl;

  return lod_mesh;
}

LODMesh::~LODMesh() {}

void LODMesh::set_instances(const std::vector<InstanceData> &instances) {
  source_instances_ = instances;
  if (source_instances_.size() > max_instances_per_lod_) {
    std::cerr << "Warning: LOD instance count " << source_instances_.size()
              << " exceeds capacity " << max_instances_per_lod_ << ", clamping"
              << std::endl;
    source_instances_.resize(max_instances_per_lod_);
  }
  total_instance_count_ = source_instances_.size();

  std::cout << "\n========================================\n";
  std::cout << "LODMesh::set_instances() called\n";
  std::cout << "========================================\n";
  std::cout << "Total instances: " << instances.size() << "\n";

  if (instances.size() > 0) {
    // Calculate position bounds
    Vec3 min_pos = instances[0].position;
    Vec3 max_pos = instances[0].position;
    float min_scale = instances[0].scale.x;
    float max_scale = instances[0].scale.x;

    for (const auto &inst : instances) {
      min_pos.x = std::min(min_pos.x, inst.position.x);
      min_pos.y = std::min(min_pos.y, inst.position.y);
      min_pos.z = std::min(min_pos.z, inst.position.z);
      max_pos.x = std::max(max_pos.x, inst.position.x);
      max_pos.y = std::max(max_pos.y, inst.position.y);
      max_pos.z = std::max(max_pos.z, inst.position.z);
      min_scale = std::min(min_scale, inst.scale.x);
      max_scale = std::max(max_scale, inst.scale.x);
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Position bounds:\n";
    std::cout << "  X: [" << min_pos.x << ", " << max_pos.x << "]\n";
    std::cout << "  Y: [" << min_pos.y << ", " << max_pos.y << "]\n";
    std::cout << "  Z: [" << min_pos.z << ", " << max_pos.z << "]\n";
    std::cout << "Scale range: [" << min_scale << ", " << max_scale << "]\n";

    // Show first few instances
    std::cout << "\nFirst 5 instances:\n";
    for (size_t i = 0; i < std::min(size_t(5), instances.size()); ++i) {
      const auto &inst = instances[i];
      std::cout << "  [" << i << "] pos=(" << inst.position.x << ", "
                << inst.position.y << ", " << inst.position.z
                << "), scale=" << inst.scale.x << "\n";
    }
  }
  std::cout << "========================================\n\n";

  if (config_.temporal.enabled &&
      instance_lod_states_.size() != source_instances_.size()) {
    instance_lod_states_.resize(source_instances_.size());

    for (auto &state : instance_lod_states_) {
      state.current_lod = 0;
      state.target_lod = 0;
      state.transition_time = 0.0f;
      state.stable_frames = 0;
    }
  }

  if (use_gpu_lod_ && gpu_.initialized) {
    std::vector<InstanceGPUData> gpu_data;
    gpu_data.reserve(source_instances_.size());
    for (size_t i = 0; i < source_instances_.size(); ++i) {
      gpu_data.push_back(source_instances_[i].to_gpu_data());
    }

    gpu_lod_assignments_.resize(source_instances_.size());

    if (gpu_.source_instances.id != 0 && !gpu_data.empty()) {
      auto *cmd = device_->getImmediate();
      std::span<const std::byte> bytes(
          reinterpret_cast<const std::byte *>(gpu_data.data()),
          gpu_data.size() * sizeof(InstanceGPUData));
      cmd->copyToBuffer(gpu_.source_instances, 0, bytes);
    }
  }
}

void LODMesh::update_instance(size_t index, const InstanceData &data) {
  if (index < source_instances_.size()) {
    source_instances_[index] = data;
    if (use_gpu_lod_ && gpu_.initialized && gpu_.source_instances.id != 0) {
      InstanceGPUData gpu_data = data.to_gpu_data();
      auto *cmd = device_->getImmediate();
      std::span<const std::byte> bytes(
          reinterpret_cast<const std::byte *>(&gpu_data),
          sizeof(InstanceGPUData));
      cmd->copyToBuffer(gpu_.source_instances,
                        index * sizeof(InstanceGPUData), bytes);
    }
  }
}

uint32_t LODMesh::compute_lod_direct(const InstanceData &inst, float distance,
                                     float screen_size,
                                     const Renderer &renderer) const {
  if (config_.mode == LODMode::Distance) {
    if (distance < config_.distance_high) {
      return 0;
    } else if (distance < config_.distance_medium) {
      return 1;
    } else if (distance < config_.distance_cull) {
      return 2;
    } else {
      return 3;
    }
  } else if (config_.mode == LODMode::ScreenSpace) {
    if (screen_size >= config_.screenspace_high) {
      return 0;
    } else if (screen_size >= config_.screenspace_medium) {
      return 1;
    } else if (screen_size >= config_.screenspace_cull) {
      return 2;
    } else {
      return 3;
    }
  } else {
    // Hybrid mode
    float distance_score, screenspace_score;

    if (distance < config_.distance_high) {
      distance_score = 0.0f;
    } else if (distance < config_.distance_medium) {
      distance_score =
          1.0f + (distance - config_.distance_high) /
                     (config_.distance_medium - config_.distance_high);
    } else if (distance < config_.distance_cull) {
      distance_score =
          2.0f + (distance - config_.distance_medium) /
                     (config_.distance_cull - config_.distance_medium);
    } else {
      distance_score = 3.0f;
    }

    if (screen_size >= config_.screenspace_high) {
      screenspace_score = 0.0f;
    } else if (screen_size >= config_.screenspace_medium) {
      screenspace_score =
          1.0f + (config_.screenspace_high - screen_size) /
                     (config_.screenspace_high - config_.screenspace_medium);
    } else if (screen_size >= config_.screenspace_cull) {
      screenspace_score =
          2.0f + (config_.screenspace_medium - screen_size) /
                     (config_.screenspace_medium - config_.screenspace_cull);
    } else {
      screenspace_score = 3.0f;
    }

    float weight = config_.hybrid_screenspace_weight;
    float final_score =
        distance_score * (1.0f - weight) + screenspace_score * weight;

    if (final_score < 0.5f) {
      return 0;
    } else if (final_score < 1.5f) {
      return 1;
    } else if (final_score < 2.5f) {
      return 2;
    } else {
      return 3;
    }
  }
}

void LODMesh::update_lod_selection(Renderer &renderer, double current_time) {
  float delta_time = 0.0f;
  if (last_update_time_ > 0.0) {
    delta_time = static_cast<float>(current_time - last_update_time_);
  }
  last_update_time_ = current_time;

  bool do_detailed_log = (g_log_frame_counter < 3);
  g_log_frame_counter++;

  if (use_gpu_lod_ && gpu_.initialized) {
    update_lod_selection_gpu(renderer, delta_time, do_detailed_log);
  } else {
    update_lod_selection_cpu(renderer, delta_time, do_detailed_log);
  }
}

void LODMesh::update_lod_selection_cpu(Renderer &renderer, float delta_time,
                                       bool do_detailed_log) {
  if (do_detailed_log) {
    std::cout << "\n========================================\n";
    std::cout << "LOD UPDATE (CPU) - Frame " << g_log_frame_counter << "\n";
    std::cout << "========================================\n";
    std::cout << "Delta time: " << delta_time << "s\n";
    std::cout << "Total source instances: " << source_instances_.size() << "\n";
  }

  if (source_instances_.empty()) {
    apply_lod_results({}, delta_time, do_detailed_log, renderer);
    return;
  }

  Vec3 cam_pos = renderer.camera().position;
  if (do_detailed_log) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Camera position: (" << cam_pos.x << ", " << cam_pos.y << ", "
              << cam_pos.z << ")\n";
  }

  float view[16], proj[16];
  renderer.camera().get_view_matrix(view);
  renderer.camera().get_projection_matrix(proj, renderer.window_width(),
                                          renderer.window_height());
  int viewport_height = renderer.window_height();

  std::vector<uint32_t> desired_lods(source_instances_.size(), 3);

  for (size_t i = 0; i < source_instances_.size(); ++i) {
    const auto &inst = source_instances_[i];

    float dx = inst.position.x - cam_pos.x;
    float dy = inst.position.y - cam_pos.y;
    float dz = inst.position.z - cam_pos.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    float max_scale = std::max({inst.scale.x, inst.scale.y, inst.scale.z});
    float effective_radius = inst.culling_radius * max_scale;

    float screen_size = screen_space::calculate_sphere_screen_size(
        inst.position, effective_radius, view, proj, viewport_height);

    uint32_t desired_lod =
        compute_lod_direct(inst, dist, screen_size, renderer);
    desired_lods[i] = desired_lod;

    if (do_detailed_log && i < 10) {
      std::cout << "\nInstance " << i << ":\n";
      std::cout << "  Position: (" << inst.position.x << ", " << inst.position.y
                << ", " << inst.position.z << ")\n";
      std::cout << "  Distance from camera: " << dist << "\n";
      std::cout << "  Screen size: " << screen_size << " pixels\n";
      std::cout << "  Desired LOD: ";
      switch (desired_lod) {
      case 0:
        std::cout << "HIGH\n";
        break;
      case 1:
        std::cout << "MEDIUM\n";
        break;
      case 2:
        std::cout << "LOW\n";
        break;
      default:
        std::cout << "CULLED\n";
        break;
      }
    }
  }

  apply_lod_results(desired_lods, delta_time, do_detailed_log, renderer);
}

namespace {
struct LODUniformsGPU {
  glm::mat4 viewMatrix{1.0f};
  glm::mat4 projectionMatrix{1.0f};
  glm::vec4 cameraPosition{0.0f};
  glm::ivec4 instanceInfo{0};
  glm::vec4 distanceThresholds{0.0f};
  glm::vec4 screenspaceThresholds{0.0f};
  glm::vec4 frustumPlanes[6]{};
};

glm::vec4 normalize_plane(const glm::vec4 &plane) {
  glm::vec3 normal = glm::vec3(plane);
  float length = glm::length(normal);
  if (length <= 0.0f)
    return plane;
  return plane / length;
}

void extract_frustum_planes(const glm::mat4 &view_proj,
                            glm::vec4 (&planes)[6]) {
  // Column-major layout in GLM
  // Left
  planes[0] = normalize_plane(glm::vec4(view_proj[0][3] + view_proj[0][0],
                                        view_proj[1][3] + view_proj[1][0],
                                        view_proj[2][3] + view_proj[2][0],
                                        view_proj[3][3] + view_proj[3][0]));
  // Right
  planes[1] = normalize_plane(glm::vec4(view_proj[0][3] - view_proj[0][0],
                                        view_proj[1][3] - view_proj[1][0],
                                        view_proj[2][3] - view_proj[2][0],
                                        view_proj[3][3] - view_proj[3][0]));
  // Bottom
  planes[2] = normalize_plane(glm::vec4(view_proj[0][3] + view_proj[0][1],
                                        view_proj[1][3] + view_proj[1][1],
                                        view_proj[2][3] + view_proj[2][1],
                                        view_proj[3][3] + view_proj[3][1]));
  // Top
  planes[3] = normalize_plane(glm::vec4(view_proj[0][3] - view_proj[0][1],
                                        view_proj[1][3] - view_proj[1][1],
                                        view_proj[2][3] - view_proj[2][1],
                                        view_proj[3][3] - view_proj[3][1]));
  // Near
  planes[4] = normalize_plane(glm::vec4(view_proj[0][3] + view_proj[0][2],
                                        view_proj[1][3] + view_proj[1][2],
                                        view_proj[2][3] + view_proj[2][2],
                                        view_proj[3][3] + view_proj[3][2]));
  // Far
  planes[5] = normalize_plane(glm::vec4(view_proj[0][3] - view_proj[0][2],
                                        view_proj[1][3] - view_proj[1][2],
                                        view_proj[2][3] - view_proj[2][2],
                                        view_proj[3][3] - view_proj[3][2]));
}
} // namespace

void LODMesh::update_lod_selection_gpu(Renderer &renderer, float delta_time,
                                       bool do_detailed_log) {
  if (!gpu_.initialized || !use_gpu_lod_) {
    update_lod_selection_cpu(renderer, delta_time, do_detailed_log);
    return;
  }

  if (do_detailed_log) {
    std::cout << "\n========================================\n";
    std::cout << "LOD UPDATE (GPU) - Frame " << g_log_frame_counter << "\n";
    std::cout << "========================================\n";
    std::cout << "Delta time: " << delta_time << "s\n";
    std::cout << "Total source instances: " << source_instances_.size() << "\n";
  }

  if (source_instances_.empty()) {
    apply_lod_results({}, delta_time, do_detailed_log, renderer);
    return;
  }

  auto *cmd = device_->getImmediate();

  // Update uniform buffer
  float view_raw[16];
  float proj_raw[16];
  renderer.camera().get_view_matrix(view_raw);
  renderer.camera().get_projection_matrix(proj_raw, renderer.window_width(),
                                          renderer.window_height());

  glm::mat4 view = glm::make_mat4(view_raw);
  glm::mat4 proj = glm::make_mat4(proj_raw);
  glm::mat4 view_proj = proj * view;

  LODUniformsGPU uniforms;
  uniforms.viewMatrix = view;
  uniforms.projectionMatrix = proj;
  uniforms.cameraPosition = glm::vec4(renderer.camera().position.x,
                                      renderer.camera().position.y,
                                      renderer.camera().position.z, 1.0f);
  uniforms.instanceInfo = glm::ivec4(static_cast<int>(source_instances_.size()),
                                     renderer.window_height(),
                                     static_cast<int>(config_.mode), 0);
  uniforms.distanceThresholds =
      glm::vec4(config_.distance_high, config_.distance_medium,
                config_.distance_cull, 0.0f);
  uniforms.screenspaceThresholds =
      glm::vec4(config_.screenspace_high, config_.screenspace_medium,
                config_.screenspace_cull, config_.hybrid_screenspace_weight);
  extract_frustum_planes(view_proj, uniforms.frustumPlanes);

  std::span<const std::byte> uniform_bytes(
      reinterpret_cast<const std::byte *>(&uniforms), sizeof(LODUniformsGPU));

  renderer.pause_render_pass();

  cmd->copyToBuffer(gpu_.uniform_buffer, 0, uniform_bytes);

  // Reset counters
  std::array<uint32_t, 4> zero_counters{0, 0, 0, 0};
  std::span<const std::byte> counter_bytes(
      reinterpret_cast<const std::byte *>(zero_counters.data()),
      zero_counters.size() * sizeof(uint32_t));
  cmd->copyToBuffer(gpu_.lod_counters, 0, counter_bytes);

  cmd->setComputePipeline(gpu_.compute_pipeline);
  const ShaderReflection &compute_reflection = gpu_.reflection;
  auto resolve_binding = [&](std::initializer_list<std::string_view> names,
                             ShaderBlockType type, uint32_t fallback) {
    for (auto name : names) {
      if (auto binding = compute_reflection.binding_for_block(name, type)) {
        return *binding;
      }
    }
    return fallback;
  };

  uint32_t uniform_binding =
      resolve_binding({"LODUniforms"}, ShaderBlockType::Uniform, 0);
  cmd->setUniformBuffer(uniform_binding, gpu_.uniform_buffer);

  uint32_t source_binding = resolve_binding({"SourceInstancesBuffer",
                                            "sourceInstances"},
                                           ShaderBlockType::Storage, 1);
  uint32_t assignments_binding = resolve_binding({"LODAssignmentsBuffer",
                                                  "lodAssignments"},
                                                 ShaderBlockType::Storage, 2);
  uint32_t counters_binding = resolve_binding({"LODCountersBuffer",
                                               "lodCounters"},
                                              ShaderBlockType::Storage, 3);
  uint32_t indices_binding = resolve_binding({"LODInstanceIndicesBuffer",
                                              "lodInstanceIndices"},
                                             ShaderBlockType::Storage, 4);

  cmd->setStorageBuffer(source_binding, gpu_.source_instances);
  cmd->setStorageBuffer(assignments_binding, gpu_.lod_assignments);
  cmd->setStorageBuffer(counters_binding, gpu_.lod_counters);
  cmd->setStorageBuffer(indices_binding, gpu_.lod_instance_indices);

  uint32_t total_instances = static_cast<uint32_t>(source_instances_.size());
  if (total_instances > 0) {
    uint32_t group_count = (total_instances + 255u) / 256u;

    // Check for GPU dispatch limits (typically 65536 per dimension)
    constexpr uint32_t max_dispatch_groups = 65536;
    if (group_count > max_dispatch_groups) {
      std::cerr << "Warning: Instance count (" << total_instances
                << ") exceeds GPU dispatch limit. Requires " << group_count
                << " workgroups but maximum is " << max_dispatch_groups
                << ". Falling back to CPU LOD calculation." << std::endl;
      renderer.resume_render_pass();
      update_lod_selection_cpu(renderer, delta_time, do_detailed_log);
      return;
    }

    cmd->dispatch(group_count, 1, 1);
    cmd->memoryBarrier();
  }

  renderer.resume_render_pass();

  gpu_lod_assignments_.resize(source_instances_.size());
  device_->readBuffer(gpu_.lod_assignments, gpu_lod_assignments_.data(),
                      gpu_lod_assignments_.size() * sizeof(uint32_t));
  device_->readBuffer(gpu_.lod_counters, gpu_lod_counters_.data(),
                      gpu_lod_counters_.size() * sizeof(uint32_t));

  apply_lod_results(gpu_lod_assignments_, delta_time, do_detailed_log,
                    renderer);
}

void LODMesh::apply_lod_results(const std::vector<uint32_t> &desired_lods,
                                float delta_time, bool do_detailed_log,
                                Renderer &renderer) {
  (void)renderer;
  std::vector<InstanceData> lod_instances[3];
  std::vector<std::pair<InstanceData, float>> crossfade_instances[3];
  int lod_counts[4] = {0, 0, 0, 0};

  if (desired_lods.empty()) {
    for (int lod = 0; lod < 3; ++lod) {
      if (lod_meshes_[lod]) {
        lod_meshes_[lod]->set_instances({});
        last_stats_.instances_per_lod[lod] = 0;
        last_stats_.visible_per_lod[lod] = 0;
      }
    }
    last_stats_.culled = 0;
    last_stats_.total_instances = 0;
    if (do_detailed_log) {
      std::cout << "No instances available for LOD processing.\n";
      std::cout << "========================================\n\n";
    }
    return;
  }

  for (size_t i = 0; i < source_instances_.size(); ++i) {
    uint32_t desired_lod = (i < desired_lods.size()) ? desired_lods[i] : 3u;
    if (desired_lod > 3)
      desired_lod = 3;
    lod_counts[desired_lod]++;

    auto inst = source_instances_[i];

    if (config_.temporal.enabled && i < instance_lod_states_.size()) {
      auto &state = instance_lod_states_[i];

      if (desired_lod != state.current_lod) {
        if (desired_lod != state.target_lod) {
          state.target_lod = desired_lod;
          state.transition_time = 0.0f;
        } else {
          state.transition_time += delta_time;

          float required_delay = (desired_lod < state.current_lod)
                                     ? config_.temporal.upgrade_delay
                                     : config_.temporal.downgrade_delay;

          if (state.transition_time >= required_delay) {
            state.previous_lod = state.current_lod;
            state.current_lod = state.target_lod;
            state.transition_time = 0.0f;
            state.stable_frames = 0;

            if (config_.dither.enabled) {
              state.is_crossfading = true;
            }
          }
        }
      } else {
        state.stable_frames++;
        state.transition_time = 0.0f;

        if (state.is_crossfading &&
            state.stable_frames >= config_.temporal.min_stable_frames) {
          state.is_crossfading = false;
        }
      }

      if (state.is_crossfading) {
        float alpha = std::min(state.stable_frames * delta_time /
                                   std::max(config_.dither.crossfade_duration,
                                            0.0001f),
                               1.0f);
        inst.lod_transition_alpha = alpha;

        if (state.previous_lod < 3) {
          auto fade_inst = inst;
          fade_inst.lod_transition_alpha = 1.0f - alpha;
          crossfade_instances[state.previous_lod].push_back(
              {fade_inst, 1.0f - alpha});
        }

        if (state.current_lod < 3) {
          inst.lod_transition_alpha = alpha;
          lod_instances[state.current_lod].push_back(inst);
        }
      } else {
        if (state.current_lod < 3) {
          inst.lod_transition_alpha = 1.0f;
          lod_instances[state.current_lod].push_back(inst);
        }
      }
    } else {
      if (desired_lod < 3) {
        inst.lod_transition_alpha = 1.0f;
        lod_instances[desired_lod].push_back(inst);
      }
    }
  }

  if (do_detailed_log) {
    std::cout << "\nLOD Distribution (desired):\n";
    std::cout << "  High: " << lod_counts[0] << "\n";
    std::cout << "  Medium: " << lod_counts[1] << "\n";
    std::cout << "  Low: " << lod_counts[2] << "\n";
    std::cout << "  Culled: " << lod_counts[3] << "\n";
  }

  for (int lod = 0; lod < 3; ++lod) {
    for (auto &[inst, alpha] : crossfade_instances[lod]) {
      lod_instances[lod].push_back(inst);
    }
  }

  if (do_detailed_log) {
    std::cout << "\nInstances assigned to LOD buffers:\n";
    std::cout << "  High: " << lod_instances[0].size() << "\n";
    std::cout << "  Medium: " << lod_instances[1].size() << "\n";
    std::cout << "  Low: " << lod_instances[2].size() << "\n";
  }

  for (int lod = 0; lod < 3; ++lod) {
    if (lod_meshes_[lod]) {
      lod_meshes_[lod]->set_instances(lod_instances[lod]);
      if (do_detailed_log) {
        std::cout << "LOD " << lod << " buffer updated with "
                  << lod_instances[lod].size() << " instances\n";
      }
    }
  }

  last_stats_.total_instances = total_instance_count_;
  for (int i = 0; i < 3; ++i) {
    last_stats_.instances_per_lod[i] = lod_instances[i].size();
    if (lod_meshes_[i]) {
      last_stats_.visible_per_lod[i] = lod_meshes_[i]->instance_count();
    }
  }
  last_stats_.culled = lod_counts[3];

  if (do_detailed_log) {
    std::cout << "========================================\n\n";
  }
}

bool LODMesh::initialize_gpu_resources(size_t max_instances) {
  if (!device_ || max_instances == 0)
    return false;

  GPUResources resources{};

  std::string compute_source;
  try {
    compute_source = pixel::platform::load_shader_file("assets/shaders/lod.comp");
  } catch (const std::exception &e) {
    std::cerr << "Failed to load GPU LOD shader: " << e.what() << std::endl;
    return false;
  }

  if (compute_source.empty())
    return false;

  std::span<const uint8_t> shader_bytes(
      reinterpret_cast<const uint8_t *>(compute_source.data()),
      compute_source.size());

  resources.reflection = reflect_glsl(compute_source, ShaderStage::Compute);
  resources.compute_shader = device_->createShader("cs_lod", shader_bytes);
  if (resources.compute_shader.id == 0)
    return false;

  rhi::PipelineDesc pipeline_desc{};
  pipeline_desc.cs = resources.compute_shader;
  resources.compute_pipeline = device_->createPipeline(pipeline_desc);
  if (resources.compute_pipeline.id == 0)
    return false;

  auto create_buffer = [&](size_t size, rhi::BufferUsage usage) {
    if (size == 0)
      return rhi::BufferHandle{0};
    rhi::BufferDesc desc{};
    desc.size = size;
    desc.usage = usage;
    desc.hostVisible = true;
    return device_->createBuffer(desc);
  };

  size_t total_instances = max_instances;

  resources.source_instances =
      create_buffer(total_instances * sizeof(InstanceGPUData),
                    rhi::BufferUsage::Storage);
  resources.lod_assignments =
      create_buffer(total_instances * sizeof(uint32_t),
                    rhi::BufferUsage::Storage);
  resources.lod_counters =
      create_buffer(4 * sizeof(uint32_t), rhi::BufferUsage::Storage);
  resources.lod_instance_indices =
      create_buffer(total_instances * 3 * sizeof(uint32_t),
                    rhi::BufferUsage::Storage);
  resources.uniform_buffer =
      create_buffer(sizeof(LODUniformsGPU), rhi::BufferUsage::Uniform);

  if (resources.source_instances.id == 0 ||
      resources.lod_assignments.id == 0 || resources.lod_counters.id == 0 ||
      resources.lod_instance_indices.id == 0 ||
      resources.uniform_buffer.id == 0) {
    return false;
  }

  resources.initialized = true;
  gpu_ = resources;
  return true;
}

void LODMesh::draw_all_lods(rhi::CmdList *cmd) const {
  for (int i = 0; i < 3; i++) {
    if (lod_meshes_[i]) {
      lod_meshes_[i]->draw(cmd);
    }
  }
}

InstancedMesh *LODMesh::lod_mesh(size_t lod_index) {
  if (lod_index >= lod_meshes_.size())
    return nullptr;
  return lod_meshes_[lod_index].get();
}

const InstancedMesh *LODMesh::lod_mesh(size_t lod_index) const {
  if (lod_index >= lod_meshes_.size())
    return nullptr;
  return lod_meshes_[lod_index].get();
}

LODMesh::LODStats LODMesh::get_stats() const {
  for (int i = 0; i < 3; i++) {
    if (lod_meshes_[i]) {
      last_stats_.visible_per_lod[i] = lod_meshes_[i]->instance_count();
    }
  }
  return last_stats_;
}

// ============================================================================
// RendererLOD Implementation
// ============================================================================

void RendererLOD::draw_lod(Renderer &renderer, LODMesh &mesh,
                           const Material &base_material) {
  mesh.update_lod_selection(renderer, renderer.time());

  renderer.resume_render_pass();

  Shader *shader = renderer.get_shader(renderer.instanced_shader());
  if (!shader)
    return;

  auto *cmd = renderer.device()->getImmediate();
  cmd->setPipeline(shader->pipeline(base_material.shader_variant,
                                    base_material.blend_mode));

  glm::mat4 model = glm::mat4(1.0f);
  const ShaderReflection &reflection =
      shader->reflection(base_material.shader_variant);
  if (reflection.has_uniform("model")) {
    cmd->setUniformMat4("model", glm::value_ptr(model));
  }

  glm::mat3 normalMatrix3x3 = glm::mat3(1.0f);
  glm::mat4 normalMatrix4x4 = glm::mat4(normalMatrix3x3);
  if (reflection.has_uniform("normalMatrix")) {
    cmd->setUniformMat4("normalMatrix", glm::value_ptr(normalMatrix4x4));
  }

  float view[16], projection[16];
  renderer.camera().get_view_matrix(view);
  renderer.camera().get_projection_matrix(projection, renderer.window_width(),
                                          renderer.window_height());
  if (reflection.has_uniform("view")) {
    cmd->setUniformMat4("view", view);
  }
  if (reflection.has_uniform("projection")) {
    cmd->setUniformMat4("projection", projection);
  }

  float light_pos[3] = {10.0f, 10.0f, 10.0f};
  float view_pos[3] = {renderer.camera().position.x,
                       renderer.camera().position.y,
                       renderer.camera().position.z};
  if (reflection.has_uniform("lightPos")) {
    cmd->setUniformVec3("lightPos", light_pos);
  }
  if (reflection.has_uniform("viewPos")) {
    cmd->setUniformVec3("viewPos", view_pos);
  }

  if (reflection.has_uniform("uTime")) {
    cmd->setUniformFloat("uTime", static_cast<float>(renderer.time()));
  }
  if (reflection.has_uniform("uDitherEnabled")) {
    cmd->setUniformInt("uDitherEnabled", 1);
  }

  if (base_material.texture_array.id != 0 &&
      reflection.has_sampler("uTextureArray")) {
    cmd->setTexture("uTextureArray", base_material.texture_array, 1);
    if (reflection.has_uniform("useTextureArray")) {
      cmd->setUniformInt("useTextureArray", 1);
    }
  } else if (reflection.has_uniform("useTextureArray")) {
    cmd->setUniformInt("useTextureArray", 0);
  }

  for (size_t lod = 0; lod < 3; ++lod) {
    const InstancedMesh *instanced_mesh = mesh.lod_mesh(lod);
    if (instanced_mesh && instanced_mesh->instance_count() > 0) {
      instanced_mesh->draw(cmd);
    }
  }
}

} // namespace pixel::renderer3d
