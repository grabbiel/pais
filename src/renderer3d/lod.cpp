// src/renderer3d/lod.cpp (Updated for RHI)
#include "pixel/renderer3d/lod.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <iostream>

namespace pixel::renderer3d {

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

LODLevel determine_lod_with_hysteresis(float distance, float screen_size,
                                       LODLevel current_lod,
                                       const LODConfig &config) {
  // Calculate base LOD without hysteresis
  LODLevel target_lod;

  if (config.mode == LODMode::Distance) {
    if (distance < config.distance_high) {
      target_lod = LODLevel::High;
    } else if (distance < config.distance_medium) {
      target_lod = LODLevel::Medium;
    } else if (distance < config.distance_cull) {
      target_lod = LODLevel::Low;
    } else {
      target_lod = LODLevel::Culled;
    }
  } else if (config.mode == LODMode::ScreenSpace) {
    if (screen_size >= config.screenspace_high) {
      target_lod = LODLevel::High;
    } else if (screen_size >= config.screenspace_medium) {
      target_lod = LODLevel::Medium;
    } else if (screen_size >= config.screenspace_cull) {
      target_lod = LODLevel::Low;
    } else {
      target_lod = LODLevel::Culled;
    }
  } else {
    // Hybrid mode - blend both metrics
    float dist_score, screen_score;

    if (distance < config.distance_high) {
      dist_score = 0.0f;
    } else if (distance < config.distance_medium) {
      dist_score = 1.0f + (distance - config.distance_high) /
                              (config.distance_medium - config.distance_high);
    } else if (distance < config.distance_cull) {
      dist_score = 2.0f + (distance - config.distance_medium) /
                              (config.distance_cull - config.distance_medium);
    } else {
      dist_score = 3.0f;
    }

    if (screen_size >= config.screenspace_high) {
      screen_score = 0.0f;
    } else if (screen_size >= config.screenspace_medium) {
      screen_score =
          1.0f + (config.screenspace_high - screen_size) /
                     (config.screenspace_high - config.screenspace_medium);
    } else if (screen_size >= config.screenspace_cull) {
      screen_score =
          2.0f + (config.screenspace_medium - screen_size) /
                     (config.screenspace_medium - config.screenspace_cull);
    } else {
      screen_score = 3.0f;
    }

    float final_score = dist_score * (1.0f - config.hybrid_screenspace_weight) +
                        screen_score * config.hybrid_screenspace_weight;

    if (final_score < 0.5f) {
      target_lod = LODLevel::High;
    } else if (final_score < 1.5f) {
      target_lod = LODLevel::Medium;
    } else if (final_score < 2.5f) {
      target_lod = LODLevel::Low;
    } else {
      target_lod = LODLevel::Culled;
    }
  }

  // Apply hysteresis if enabled
  if (!config.temporal.enabled) {
    return target_lod;
  }

  // If switching to a different LOD, apply hysteresis
  if (target_lod != current_lod) {
    float hysteresis = config.temporal.hysteresis_factor;

    // Check if we're within the hysteresis zone
    if (config.mode == LODMode::Distance || config.mode == LODMode::Hybrid) {
      float threshold_high = config.distance_high * (1.0f + hysteresis);
      float threshold_medium = config.distance_medium * (1.0f + hysteresis);

      // Upgrading (getting closer)
      if (static_cast<int>(target_lod) < static_cast<int>(current_lod)) {
        threshold_high = config.distance_high * (1.0f - hysteresis);
        threshold_medium = config.distance_medium * (1.0f - hysteresis);
      }

      // Stay at current LOD if within hysteresis zone
      if (current_lod == LODLevel::High && distance < threshold_high) {
        return LODLevel::High;
      }
      if (current_lod == LODLevel::Medium && distance < threshold_medium) {
        return LODLevel::Medium;
      }
    }
  }

  return target_lod;
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

  return lod_mesh;
}

LODMesh::~LODMesh() {}

void LODMesh::set_instances(const std::vector<InstanceData> &instances) {
  source_instances_ = instances;
  total_instance_count_ = instances.size();

  if (config_.temporal.enabled &&
      instance_lod_states_.size() != instances.size()) {
    instance_lod_states_.resize(instances.size());

    for (auto &state : instance_lod_states_) {
      state.current_lod = 0;
      state.target_lod = 0;
      state.transition_time = 0.0f;
      state.stable_frames = 0;
    }
  }
}

void LODMesh::update_instance(size_t index, const InstanceData &data) {
  if (index < source_instances_.size()) {
    source_instances_[index] = data;
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

void LODMesh::update_lod_selection(const Renderer &renderer,
                                   double current_time) {
  float delta_time = static_cast<float>(current_time - last_update_time_);
  last_update_time_ = current_time;

  // Distribute instances into LOD buckets
  std::vector<InstanceData> lod_instances[3];
  std::vector<std::pair<InstanceData, float>> crossfade_instances[3];

  Vec3 cam_pos = renderer.camera().position;
  float view[16], proj[16];
  renderer.camera().get_view_matrix(view);
  renderer.camera().get_projection_matrix(proj, renderer.window_width(),
                                          renderer.window_height());
  int viewport_height = renderer.window_height();

  for (size_t i = 0; i < source_instances_.size(); ++i) {
    auto inst = source_instances_[i];
    auto &state = instance_lod_states_[i];

    // Calculate metrics
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

    // Update temporal state
    if (config_.temporal.enabled) {
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
            if (config_.dither.enabled) {
              state.previous_lod = state.current_lod;
              state.current_lod = desired_lod;
              state.is_crossfading = true;
              state.transition_alpha = 0.0f;
              state.transition_time = 0.0f;
            } else {
              state.current_lod = desired_lod;
              state.transition_time = 0.0f;
            }
          }
        }
      } else {
        state.target_lod = state.current_lod;
        state.transition_time = 0.0f;
      }

      // Update crossfade
      if (state.is_crossfading) {
        state.transition_alpha +=
            delta_time / config_.dither.crossfade_duration;
        if (state.transition_alpha >= 1.0f) {
          state.transition_alpha = 1.0f;
          state.is_crossfading = false;
          state.previous_lod = state.current_lod;
        }
      }
    } else {
      state.current_lod = desired_lod;
      state.target_lod = desired_lod;
    }

    // Add to appropriate LOD bucket
    if (state.is_crossfading && config_.dither.enabled) {
      if (state.previous_lod < 3) {
        auto prev_inst = inst;
        prev_inst.lod_transition_alpha = 1.0f - state.transition_alpha;
        crossfade_instances[state.previous_lod].push_back(
            {prev_inst, 1.0f - state.transition_alpha});
      }

      if (state.current_lod < 3) {
        inst.lod_transition_alpha = state.transition_alpha;
        lod_instances[state.current_lod].push_back(inst);
      }
    } else {
      if (state.current_lod < 3) {
        inst.lod_transition_alpha = 1.0f;
        lod_instances[state.current_lod].push_back(inst);
      }
    }
  }

  // Merge crossfade instances
  for (int lod = 0; lod < 3; lod++) {
    for (auto &[inst, alpha] : crossfade_instances[lod]) {
      lod_instances[lod].push_back(inst);
    }
  }

  // Update instance buffers
  for (int lod = 0; lod < 3; lod++) {
    if (lod_meshes_[lod]) {
      lod_meshes_[lod]->set_instances(lod_instances[lod]);
    }
  }

  // Update stats
  last_stats_.total_instances = total_instance_count_;
  for (int i = 0; i < 3; i++) {
    last_stats_.instances_per_lod[i] = lod_instances[i].size();
  }
}

void LODMesh::draw_all_lods(rhi::CmdList *cmd) const {
  for (int i = 0; i < 3; i++) {
    if (lod_meshes_[i]) {
      lod_meshes_[i]->draw(cmd);
    }
  }
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

  Shader *shader = renderer.get_shader(renderer.instanced_shader());
  if (!shader)
    return;

  auto *cmd = renderer.device()->getImmediate();
  cmd->setPipeline(shader->pipeline());

  // Set view and projection matrices
  float view[16], projection[16];
  renderer.camera().get_view_matrix(view);
  renderer.camera().get_projection_matrix(projection, renderer.window_width(),
                                          renderer.window_height());
  cmd->setUniformMat4("view", view);
  cmd->setUniformMat4("projection", projection);

  // Set lighting uniforms
  float light_pos[3] = {10.0f, 10.0f, 10.0f};
  float view_pos[3] = {renderer.camera().position.x,
                       renderer.camera().position.y,
                       renderer.camera().position.z};
  cmd->setUniformVec3("lightPos", light_pos);
  cmd->setUniformVec3("viewPos", view_pos);

  // Set time uniform for animated dither
  cmd->setUniformFloat("uTime", static_cast<float>(renderer.time()));

  // Set dither settings based on LOD config
  const auto &dither = mesh.config().dither;
  cmd->setUniformInt("uDitherEnabled", dither.enabled ? 1 : 0);
  cmd->setUniformFloat("uDitherScale", dither.dither_pattern_scale);
  cmd->setUniformFloat("uCrossfadeDuration", dither.crossfade_duration);

  // Set texture array if available
  if (base_material.texture_array.id != 0) {
    cmd->setTexture("uTextureArray", base_material.texture_array, 0);
    cmd->setUniformInt("useTextureArray", 1);
  } else {
    cmd->setUniformInt("useTextureArray", 0);
  }

  mesh.draw_all_lods(cmd);
}

} // namespace pixel::renderer3d
