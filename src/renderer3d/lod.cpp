#include "pixel/renderer3d/lod.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// OpenGL 4.3+ constants
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif

// ============================================================================
// Compute Shader Support Detection
// ============================================================================

typedef void(APIENTRYP PFNGLDISPATCHCOMPUTEPROC)(GLuint, GLuint, GLuint);
typedef void(APIENTRYP PFNGLMEMORYBARRIERPROC)(GLbitfield);

static bool g_compute_shaders_supported = false;
static PFNGLDISPATCHCOMPUTEPROC glDispatchCompute = nullptr;
static PFNGLMEMORYBARRIERPROC glMemoryBarrier = nullptr;

static bool check_opengl_version(int major, int minor) {
  const char *version_str = (const char *)glGetString(GL_VERSION);
  if (!version_str)
    return false;

  int gl_major = 0, gl_minor = 0;
  sscanf(version_str, "%d.%d", &gl_major, &gl_minor);

  if (gl_major > major)
    return true;
  if (gl_major == major && gl_minor >= minor)
    return true;
  return false;
}

static void load_compute_functions() {
  static bool loaded = false;
  if (loaded)
    return;
  loaded = true;

  const char *version_str = (const char *)glGetString(GL_VERSION);

  if (!check_opengl_version(4, 3)) {
    g_compute_shaders_supported = false;
    return;
  }

  glDispatchCompute =
      (PFNGLDISPATCHCOMPUTEPROC)glfwGetProcAddress("glDispatchCompute");
  glMemoryBarrier =
      (PFNGLMEMORYBARRIERPROC)glfwGetProcAddress("glMemoryBarrier");

  g_compute_shaders_supported =
      (glDispatchCompute != nullptr && glMemoryBarrier != nullptr);
}

// ============================================================================
// Screen-Space LOD Compute Shader
// ============================================================================

static const char *lod_compute_shader_src = R"(
#version 430 core

layout(local_size_x = 256) in;

// Input: all instances
struct InstanceData {
  vec3 position;
  vec3 rotation;
  vec3 scale;
  vec4 color;
  float texture_index;
  float culling_radius;
  vec2 _padding;
};

layout(std430, binding = 0) readonly buffer SourceInstancesBuffer {
  InstanceData source_instances[];
};

// Output: LOD level assignment (0=high, 1=medium, 2=low, 3=culled)
layout(std430, binding = 1) writeonly buffer LODAssignmentsBuffer {
  uint lod_assignments[];
};

// Output: Counter for each LOD level [high, medium, low, culled]
layout(std430, binding = 2) buffer LODCountersBuffer {
  uint lod_counters[4];
};

// Output: Instance indices organized by LOD level
layout(std430, binding = 3) writeonly buffer LODInstanceIndicesBuffer {
  uint lod_instance_indices[];
};

// Uniforms
uniform vec3 camera_position;
uniform mat4 view_matrix;
uniform mat4 projection_matrix;
uniform uint total_instances;
uniform int viewport_height;

// LOD mode: 0=distance, 1=screenspace, 2=hybrid
uniform int lod_mode;

// Distance-based thresholds
uniform float distance_high;
uniform float distance_medium;
uniform float distance_cull;

// Screen-space thresholds (as fraction of screen height)
uniform float screenspace_high;
uniform float screenspace_medium;
uniform float screenspace_low;

// Hybrid mode weight (0.0-1.0, how much to weight screen-space)
uniform float hybrid_weight;

// Frustum culling
uniform vec4 frustum_planes[6];

// Check if sphere intersects frustum
bool is_visible(vec3 center, float radius) {
  for (int i = 0; i < 6; i++) {
    vec4 plane = frustum_planes[i];
    float distance = dot(plane.xyz, center) + plane.w;
    if (distance < -radius) {
      return false;
    }
  }
  return true;
}

// Calculate screen-space size of a sphere
// Returns size as fraction of screen height
float calculate_screen_size(vec3 world_pos, float world_radius) {
  // Transform to view space
  vec4 view_pos = view_matrix * vec4(world_pos, 1.0);
  float distance = abs(view_pos.z);
  
  if (distance < 0.001) return 1.0; // Very close = max size
  
  // Get FOV from projection matrix
  float fov_y_rad = 2.0 * atan(1.0 / projection_matrix[1][1]);
  
  // Calculate screen-space size
  // size_fraction = (world_radius / distance) / tan(fov/2)
  float size_fraction = (world_radius / distance) / tan(fov_y_rad * 0.5);
  
  return size_fraction;
}

// Determine LOD based on distance
uint get_lod_distance(float distance) {
  if (distance < distance_high) {
    return 0; // High
  } else if (distance < distance_medium) {
    return 1; // Medium
  } else if (distance < distance_cull) {
    return 2; // Low
  } else {
    return 3; // Culled
  }
}

// Determine LOD based on screen-space size
uint get_lod_screenspace(float screen_size) {
  if (screen_size >= screenspace_high) {
    return 0; // High
  } else if (screen_size >= screenspace_medium) {
    return 1; // Medium
  } else if (screen_size >= screenspace_low) {
    return 2; // Low
  } else {
    return 3; // Culled
  }
}

// Hybrid LOD determination
uint get_lod_hybrid(float distance, float screen_size) {
  // Calculate distance-based score (0-3 continuous)
  float distance_score;
  if (distance < distance_high) {
    distance_score = 0.0;
  } else if (distance < distance_medium) {
    distance_score = 1.0 + (distance - distance_high) / (distance_medium - distance_high);
  } else if (distance < distance_cull) {
    distance_score = 2.0 + (distance - distance_medium) / (distance_cull - distance_medium);
  } else {
    distance_score = 3.0;
  }
  
  // Calculate screen-space score (0-3 continuous)
  float screenspace_score;
  if (screen_size >= screenspace_high) {
    screenspace_score = 0.0;
  } else if (screen_size >= screenspace_medium) {
    screenspace_score = 1.0 + (screenspace_high - screen_size) / 
                        (screenspace_high - screenspace_medium);
  } else if (screen_size >= screenspace_low) {
    screenspace_score = 2.0 + (screenspace_medium - screen_size) / 
                        (screenspace_medium - screenspace_low);
  } else {
    screenspace_score = 3.0;
  }
  
  // Blend scores
  float final_score = distance_score * (1.0 - hybrid_weight) + screenspace_score * hybrid_weight;
  
  // Map to discrete LOD level
  if (final_score < 0.5) return 0;
  else if (final_score < 1.5) return 1;
  else if (final_score < 2.5) return 2;
  else return 3;
}

void main() {
  uint index = gl_GlobalInvocationID.x;
  
  if (index >= total_instances) {
    return;
  }
  
  InstanceData inst = source_instances[index];
  vec3 world_pos = inst.position;
  
  // Calculate effective radius considering scale
  float max_scale = max(max(inst.scale.x, inst.scale.y), inst.scale.z);
  float effective_radius = inst.culling_radius * max_scale;
  
  // Frustum culling first
  if (!is_visible(world_pos, effective_radius)) {
    lod_assignments[index] = 3; // Culled
    atomicAdd(lod_counters[3], 1);
    return;
  }
  
  // Calculate distance to camera
  float distance = length(world_pos - camera_position);
  
  // Calculate screen-space size
  float screen_size = calculate_screen_size(world_pos, effective_radius);
  
  // Determine LOD level based on mode
  uint lod_level;
  if (lod_mode == 0) {
    // Distance-based
    lod_level = get_lod_distance(distance);
  } else if (lod_mode == 1) {
    // Screen-space based
    lod_level = get_lod_screenspace(screen_size);
  } else {
    // Hybrid
    lod_level = get_lod_hybrid(distance, screen_size);
  }
  
  lod_assignments[index] = lod_level;
  
  if (lod_level < 3) {
    // Atomically get position in the LOD-specific buffer
    uint lod_local_index = atomicAdd(lod_counters[lod_level], 1);
    
    // Calculate base offset for this LOD level
    uint base_offset = lod_level * total_instances;
    lod_instance_indices[base_offset + lod_local_index] = index;
  } else {
    atomicAdd(lod_counters[3], 1);
  }
}
)";

namespace pixel::renderer3d {

// ============================================================================
// LODMesh Implementation
// ============================================================================

std::unique_ptr<LODMesh>
LODMesh::create(const Mesh &high_detail, const Mesh &medium_detail,
                const Mesh &low_detail, size_t max_instances_per_lod,
                const LODConfig &config, const HLODTree *hlod_tree) {
  auto lod_mesh = std::unique_ptr<LODMesh>(new LODMesh());

  lod_mesh->max_instances_per_lod_ = max_instances_per_lod;
  lod_mesh->config_ = config;

  lod_mesh->lod_meshes_[0] =
      InstancedMesh::create(high_detail, max_instances_per_lod);
  lod_mesh->lod_meshes_[1] =
      InstancedMesh::create(medium_detail, max_instances_per_lod);
  lod_mesh->lod_meshes_[2] =
      InstancedMesh::create(low_detail, max_instances_per_lod);

  if (hlod_tree) {
    lod_mesh->set_hlod_tree(*hlod_tree);
  }

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
    std::cout << "Hybrid (weight=" << config.hybrid_screenspace_weight << ")";
    break;
  }
  std::cout << std::endl;
  std::cout << "  High detail: " << high_detail.vertex_count() << " verts"
            << std::endl;
  std::cout << "  Medium detail: " << medium_detail.vertex_count() << " verts"
            << std::endl;
  std::cout << "  Low detail: " << low_detail.vertex_count() << " verts"
            << std::endl;

  lod_mesh->setup_lod_compute_shader();

  return lod_mesh;
}

LODMesh::~LODMesh() {
  if (lod_compute_shader_)
    glDeleteProgram(lod_compute_shader_);
  if (source_instances_ssbo_)
    glDeleteBuffers(1, &source_instances_ssbo_);
  if (lod_assignments_ssbo_)
    glDeleteBuffers(1, &lod_assignments_ssbo_);
  if (lod_counters_ssbo_)
    glDeleteBuffers(1, &lod_counters_ssbo_);
  if (lod_instance_indices_ssbo_)
    glDeleteBuffers(1, &lod_instance_indices_ssbo_);
}

void LODMesh::set_hlod_tree(const HLODTree &tree) {
  hlod_tree_ = tree;
  cluster_children_.clear();
  cluster_proxies_.clear();

  for (const auto &cluster : tree.clusters) {
    cluster_children_[cluster.cluster_id] = cluster.children;
    if (cluster.proxy_mesh) {
      cluster_proxies_[cluster.cluster_id] = cluster.proxy_mesh;
    }
  }
}

const std::vector<uint32_t> *
LODMesh::get_cluster_children(uint32_t cluster_id) const {
  auto it = cluster_children_.find(cluster_id);
  if (it == cluster_children_.end())
    return nullptr;
  return &it->second;
}

void LODMesh::set_cluster_proxy(uint32_t cluster_id,
                                const std::shared_ptr<Mesh> &proxy_mesh) {
  if (proxy_mesh) {
    cluster_proxies_[cluster_id] = proxy_mesh;
  } else {
    cluster_proxies_.erase(cluster_id);
  }

  if (hlod_tree_) {
    if (auto *cluster = hlod_tree_->find_cluster(cluster_id)) {
      cluster->proxy_mesh = proxy_mesh;
    }
  }
}

std::shared_ptr<Mesh> LODMesh::get_cluster_proxy(uint32_t cluster_id) const {
  auto it = cluster_proxies_.find(cluster_id);
  if (it == cluster_proxies_.end())
    return nullptr;
  return it->second;
}

void LODMesh::set_cluster_children(uint32_t cluster_id,
                                   const std::vector<uint32_t> &children) {
  cluster_children_[cluster_id] = children;
  if (hlod_tree_) {
    if (auto *cluster = hlod_tree_->find_cluster(cluster_id)) {
      cluster->children = children;
    }
  }
}

void LODMesh::clear_hlod_tree() {
  hlod_tree_.reset();
  cluster_children_.clear();
  cluster_proxies_.clear();
}

void LODMesh::setup_lod_compute_shader() {
  load_compute_functions();

  if (!g_compute_shaders_supported) {
    std::cout << "GPU LOD: DISABLED (compute shaders not available)"
              << std::endl;
    gpu_lod_enabled_ = false;
    return;
  }

  if (!glDispatchCompute || !glMemoryBarrier) {
    gpu_lod_enabled_ = false;
    return;
  }

  // Compile LOD compute shader
  GLuint compute_shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(compute_shader, 1, &lod_compute_shader_src, nullptr);
  glCompileShader(compute_shader);

  GLint success;
  glGetShaderiv(compute_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[512];
    glGetShaderInfoLog(compute_shader, 512, nullptr, log);
    std::cerr << "LOD compute shader compilation failed:\n" << log << std::endl;
    glDeleteShader(compute_shader);
    gpu_lod_enabled_ = false;
    return;
  }

  lod_compute_shader_ = glCreateProgram();
  glAttachShader(lod_compute_shader_, compute_shader);
  glLinkProgram(lod_compute_shader_);

  glGetProgramiv(lod_compute_shader_, GL_LINK_STATUS, &success);
  if (!success) {
    char log[512];
    glGetProgramInfoLog(lod_compute_shader_, 512, nullptr, log);
    std::cerr << "LOD compute shader linking failed:\n" << log << std::endl;
    glDeleteShader(compute_shader);
    glDeleteProgram(lod_compute_shader_);
    gpu_lod_enabled_ = false;
    return;
  }

  glDeleteShader(compute_shader);

  // Create SSBOs
  size_t max_total = max_instances_per_lod_ * 3;

  glGenBuffers(1, &source_instances_ssbo_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, source_instances_ssbo_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, max_total * sizeof(InstanceData),
               nullptr, GL_DYNAMIC_DRAW);

  glGenBuffers(1, &lod_assignments_ssbo_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lod_assignments_ssbo_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, max_total * sizeof(uint32_t), nullptr,
               GL_DYNAMIC_DRAW);

  glGenBuffers(1, &lod_counters_ssbo_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lod_counters_ssbo_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, 4 * sizeof(uint32_t), nullptr,
               GL_DYNAMIC_DRAW);

  glGenBuffers(1, &lod_instance_indices_ssbo_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lod_instance_indices_ssbo_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, max_total * 3 * sizeof(uint32_t),
               nullptr, GL_DYNAMIC_DRAW);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  gpu_lod_enabled_ = true;
  std::cout << "GPU LOD: ENABLED (screen-space support)" << std::endl;
}

void LODMesh::set_instances(const std::vector<InstanceData> &instances) {
  source_instances_ = instances;
  total_instance_count_ = instances.size();
  if (config_.temporal.enabled &&
      instance_lod_states_.size() != instances.size()) {
    instance_lod_states_.resize(instances.size());

    // Initialize all instances to high LOD by default
    for (auto &state : instance_lod_states_) {
      state.current_lod = 0; // Start at high detail
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

  // Compute LOD without hysteresis or temporal smoothing
  // This is used when temporal coherence is disabled

  if (config_.mode == LODMode::Distance) {
    // Distance-based LOD selection
    if (distance < config_.distance_high) {
      return 0; // High detail
    } else if (distance < config_.distance_medium) {
      return 1; // Medium detail
    } else if (distance < config_.distance_cull) {
      return 2; // Low detail
    } else {
      return 3; // Culled
    }
  } else if (config_.mode == LODMode::ScreenSpace) {
    // Screen-space based LOD selection
    if (screen_size >= config_.screenspace_high) {
      return 0; // High detail
    } else if (screen_size >= config_.screenspace_medium) {
      return 1; // Medium detail
    } else if (screen_size >= config_.screenspace_low) {
      return 2; // Low detail
    } else {
      return 3; // Culled
    }
  } else {
    // Hybrid mode - blend distance and screen-space metrics

    // Calculate distance score (0-3: 0=high, 1=medium, 2=low, 3=cull)
    float distance_score;
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

    // Calculate screen-space score
    float screenspace_score;
    if (screen_size >= config_.screenspace_high) {
      screenspace_score = 0.0f;
    } else if (screen_size >= config_.screenspace_medium) {
      screenspace_score =
          1.0f + (config_.screenspace_high - screen_size) /
                     (config_.screenspace_high - config_.screenspace_medium);
    } else if (screen_size >= config_.screenspace_low) {
      screenspace_score =
          2.0f + (config_.screenspace_medium - screen_size) /
                     (config_.screenspace_medium - config_.screenspace_low);
    } else {
      screenspace_score = 3.0f;
    }

    // Blend the two scores
    float weight = config_.hybrid_screenspace_weight;
    float final_score =
        distance_score * (1.0f - weight) + screenspace_score * weight;

    // Map continuous score to discrete LOD level
    if (final_score < 0.5f) {
      return 0; // High detail
    } else if (final_score < 1.5f) {
      return 1; // Medium detail
    } else if (final_score < 2.5f) {
      return 2; // Low detail
    } else {
      return 3; // Culled
    }
  }
}

void LODMesh::compute_lod_distribution(const Renderer &renderer) {
  // Calculate delta time
  double current_time = renderer.time();
  float delta_time = static_cast<float>(current_time - last_update_time_);
  last_update_time_ = current_time;

  // Update temporal states (handles crossfading)
  update_temporal_states(delta_time);

  if (!gpu_lod_enabled_ || total_instance_count_ == 0) {
    // CPU-based LOD distribution with crossfade support
    std::vector<InstanceData> lod_instances[3];

    // Also track crossfading instances (appear in both LODs)
    std::vector<std::pair<InstanceData, float>> crossfade_instances[3];

    Vec3 cam_pos = renderer.camera().position;
    float view[16], proj[16];
    renderer.camera().get_view_matrix(view);
    renderer.camera().get_projection_matrix(proj, renderer.window_width(),
                                            renderer.window_height());
    int viewport_height = renderer.window_height();

    for (size_t i = 0; i < source_instances_.size(); ++i) {
      auto inst = source_instances_[i]; // Copy so we can modify
      auto &state = instance_lod_states_[i];

      // Calculate LOD metrics
      float dx = inst.position.x - cam_pos.x;
      float dy = inst.position.y - cam_pos.y;
      float dz = inst.position.z - cam_pos.z;
      float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

      float max_scale = std::max({inst.scale.x, inst.scale.y, inst.scale.z});
      float effective_radius = inst.culling_radius * max_scale;

      float screen_size = screen_space::calculate_sphere_screen_size(
          inst.position, effective_radius, view, proj, viewport_height);

      uint32_t desired_lod;

      if (config_.temporal.enabled) {
        desired_lod = compute_lod_with_hysteresis(inst, state, dist,
                                                  screen_size, renderer);

        if (desired_lod != state.current_lod) {
          if (desired_lod != state.target_lod) {
            state.target_lod = desired_lod;
            state.transition_time = 0.0f;
          }
        } else {
          state.target_lod = state.current_lod;
          state.transition_time = 0.0f;
        }
      } else {
        desired_lod = compute_lod_direct(inst, dist, screen_size, renderer);
        state.current_lod = desired_lod;
        state.target_lod = desired_lod;
      }

      // Handle crossfading instances
      if (state.is_crossfading && config_.dither.enabled) {
        // Add to previous LOD with inverse alpha
        if (state.previous_lod < 3) {
          auto prev_inst = inst;
          prev_inst.lod_transition_alpha = 1.0f - state.transition_alpha;
          crossfade_instances[state.previous_lod].push_back(
              {prev_inst, 1.0f - state.transition_alpha});
        }

        // Add to current LOD with transition alpha
        if (state.current_lod < 3) {
          inst.lod_transition_alpha = state.transition_alpha;
          lod_instances[state.current_lod].push_back(inst);
        }
      } else {
        // Normal rendering - single LOD
        if (state.current_lod < 3) {
          inst.lod_transition_alpha = 1.0f;
          lod_instances[state.current_lod].push_back(inst);
        }
      }
    }

    // Merge crossfade instances with main instances
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
  }
}

void LODMesh::draw_all_lods() const {
  for (int i = 0; i < 3; i++) {
    if (lod_meshes_[i]) {
      lod_meshes_[i]->draw();
    }
  }
}

LODMesh::LODStats LODMesh::get_stats() const {
  // Update visible counts from individual meshes
  for (int i = 0; i < 3; i++) {
    if (lod_meshes_[i]) {
      last_stats_.visible_per_lod[i] = lod_meshes_[i]->get_visible_count();
    }
  }
  return last_stats_;
}

uint32_t LODMesh::compute_lod_with_hysteresis(const InstanceData &inst,
                                              const InstanceLODState &state,
                                              float distance, float screen_size,
                                              const Renderer &renderer) const {

  LODLevel current = static_cast<LODLevel>(state.current_lod);

  LODLevel new_lod = screen_space::determine_lod_with_hysteresis(
      distance, screen_size, current, config_);

  return static_cast<uint32_t>(new_lod);
}

bool LODMesh::should_transition_lod(const InstanceLODState &state,
                                    uint32_t new_lod, float delta_time) const {

  if (!config_.temporal.enabled) {
    return true; // Always transition immediately if temporal coherence disabled
  }

  if (state.current_lod == new_lod) {
    return false; // Already at target LOD
  }

  // Determine required delay based on upgrade vs downgrade
  float required_delay;
  if (new_lod < state.current_lod) {
    // Upgrading to higher detail - use faster delay
    required_delay = config_.temporal.upgrade_delay;
  } else {
    // Downgrading to lower detail - use slower delay
    required_delay = config_.temporal.downgrade_delay;
  }

  // Check if we've waited long enough
  return state.transition_time >= required_delay;
}

void LODMesh::update_temporal_states(float delta_time) {
  if (!config_.temporal.enabled) {
    return;
  }

  for (auto &state : instance_lod_states_) {
    if (state.is_transitioning()) {
      state.transition_time += delta_time;

      // Handle crossfade transitions
      if (state.is_crossfading && config_.dither.enabled) {
        // Update transition alpha for dithering
        float crossfade_progress = std::min(
            1.0f, state.transition_time / config_.dither.crossfade_duration);
        state.transition_alpha = crossfade_progress;

        // Complete crossfade when done
        if (crossfade_progress >= 1.0f) {
          state.current_lod = state.target_lod;
          state.previous_lod = state.current_lod;
          state.is_crossfading = false;
          state.transition_alpha = 1.0f;
          state.transition_time = 0.0f;
          state.stable_frames = 0;
        }
      } else {
        // Standard transition (no crossfade)
        if (should_transition_lod(state, state.target_lod, delta_time)) {
          // Begin crossfade if dithering enabled
          if (config_.dither.enabled) {
            state.previous_lod = state.current_lod;
            state.current_lod = state.target_lod;
            state.is_crossfading = true;
            state.transition_alpha = 0.0f;
            state.transition_time = 0.0f;
          } else {
            // Instant transition
            state.current_lod = state.target_lod;
            state.transition_time = 0.0f;
            state.stable_frames = 0;
          }
        }
      }
    } else {
      // Stable - increment stability counter
      state.stable_frames =
          std::min(state.stable_frames + 1,
                   config_.temporal.stable_frames_required + 10);
      state.transition_alpha = 1.0f;
    }
  }
}

// ============================================================================
// RendererLOD Implementation
// ============================================================================

std::unique_ptr<LODMesh> RendererLOD::create_lod_mesh(
    const Mesh &high_detail, const Mesh &medium_detail, const Mesh &low_detail,
    size_t max_instances_per_lod, const LODConfig &config,
    const HLODTree *hlod_tree) {
  return LODMesh::create(high_detail, medium_detail, low_detail,
                         max_instances_per_lod, config, hlod_tree);
}

void RendererLOD::draw_lod(Renderer &renderer, LODMesh &mesh,
                           const Material &base_material) {
  // Compute LOD distribution
  mesh.compute_lod_distribution(renderer);

  // Get shader
  Shader *shader = renderer.get_shader(renderer.instanced_shader());
  if (!shader)
    return;

  shader->bind();

  // Set common uniforms
  float view[16], proj[16];
  renderer.camera().get_view_matrix(view);
  renderer.camera().get_projection_matrix(proj, renderer.window_width(),
                                          renderer.window_height());

  shader->set_mat4("view", view);
  shader->set_mat4("projection", proj);
  shader->set_vec3("lightPos", {5, 10, 5});
  shader->set_vec3("viewPos", renderer.camera().position);

  shader->set_float("uTime", static_cast<float>(renderer.time()));
  int dither_mode = mesh.config().dither.enabled
                        ? (mesh.config().dither.temporal_jitter ? 2 : 1)
                        : 0;
  shader->set_int("uDitherEnabled", dither_mode);

  if (base_material.texture_array != INVALID_TEXTURE_ARRAY) {
    renderer.bind_texture_array(base_material.texture_array, 0);
    shader->set_int("uTextureArray", 0);
    shader->set_int("useTextureArray", 1);
  } else {
    shader->set_int("useTextureArray", 0);
  }

  // Draw all LOD levels
  mesh.draw_all_lods();

  shader->unbind();
}

// Mesh generators with different detail levels
std::unique_ptr<Mesh> RendererLOD::create_cube_high_detail(Renderer &r,
                                                           float size) {
  return r.create_cube(size);
}

std::unique_ptr<Mesh> RendererLOD::create_cube_medium_detail(Renderer &r,
                                                             float size) {
  return r.create_cube(size);
}

std::unique_ptr<Mesh> RendererLOD::create_cube_low_detail(Renderer &r,
                                                          float size) {
  float h = size / 2.0f;

  std::vector<Vertex> vertices = {
      {{-h, -h, -h}, {-1, -1, -1}, {0, 0}, Color::White()},
      {{h, -h, -h}, {1, -1, -1}, {1, 0}, Color::White()},
      {{h, h, -h}, {1, 1, -1}, {1, 1}, Color::White()},
      {{-h, h, -h}, {-1, 1, -1}, {0, 1}, Color::White()},
      {{-h, -h, h}, {-1, -1, 1}, {0, 0}, Color::White()},
      {{h, -h, h}, {1, -1, 1}, {1, 0}, Color::White()},
      {{h, h, h}, {1, 1, 1}, {1, 1}, Color::White()},
      {{-h, h, h}, {-1, 1, 1}, {0, 1}, Color::White()},
  };

  std::vector<uint32_t> indices = {
      4, 5, 6, 6, 7, 4, 1, 0, 3, 3, 2, 1, 7, 6, 2, 2, 3, 7,
      0, 1, 5, 5, 4, 0, 5, 1, 2, 2, 6, 5, 0, 4, 7, 7, 3, 0,
  };

  return Mesh::create(vertices, indices);
}

std::unique_ptr<Mesh> RendererLOD::create_sphere_high_detail(Renderer &r,
                                                             float radius) {
  const int segments = 32;
  const int rings = 16;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  for (int ring = 0; ring <= rings; ring++) {
    float theta = ring * M_PI / rings;
    float sin_theta = std::sin(theta);
    float cos_theta = std::cos(theta);

    for (int seg = 0; seg <= segments; seg++) {
      float phi = seg * 2.0f * M_PI / segments;
      float sin_phi = std::sin(phi);
      float cos_phi = std::cos(phi);

      Vec3 pos = {radius * sin_theta * cos_phi, radius * cos_theta,
                  radius * sin_theta * sin_phi};
      Vec3 normal = pos.normalized();
      Vec2 texcoord = {(float)seg / segments, (float)ring / rings};

      vertices.push_back({pos, normal, texcoord, Color::White()});
    }
  }

  for (int ring = 0; ring < rings; ring++) {
    for (int seg = 0; seg < segments; seg++) {
      uint32_t current = ring * (segments + 1) + seg;
      uint32_t next = current + segments + 1;

      indices.push_back(current);
      indices.push_back(next);
      indices.push_back(current + 1);

      indices.push_back(current + 1);
      indices.push_back(next);
      indices.push_back(next + 1);
    }
  }

  return Mesh::create(vertices, indices);
}

std::unique_ptr<Mesh> RendererLOD::create_sphere_medium_detail(Renderer &r,
                                                               float radius) {
  const int segments = 16;
  const int rings = 8;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  for (int ring = 0; ring <= rings; ring++) {
    float theta = ring * M_PI / rings;
    float sin_theta = std::sin(theta);
    float cos_theta = std::cos(theta);

    for (int seg = 0; seg <= segments; seg++) {
      float phi = seg * 2.0f * M_PI / segments;
      float sin_phi = std::sin(phi);
      float cos_phi = std::cos(phi);

      Vec3 pos = {radius * sin_theta * cos_phi, radius * cos_theta,
                  radius * sin_theta * sin_phi};
      Vec3 normal = pos.normalized();
      Vec2 texcoord = {(float)seg / segments, (float)ring / rings};

      vertices.push_back({pos, normal, texcoord, Color::White()});
    }
  }

  for (int ring = 0; ring < rings; ring++) {
    for (int seg = 0; seg < segments; seg++) {
      uint32_t current = ring * (segments + 1) + seg;
      uint32_t next = current + segments + 1;

      indices.push_back(current);
      indices.push_back(next);
      indices.push_back(current + 1);

      indices.push_back(current + 1);
      indices.push_back(next);
      indices.push_back(next + 1);
    }
  }

  return Mesh::create(vertices, indices);
}

std::unique_ptr<Mesh> RendererLOD::create_sphere_low_detail(Renderer &r,
                                                            float radius) {
  const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
  const float scale = radius / std::sqrt(1.0f + t * t);

  std::vector<Vertex> vertices = {
      {{-1 * scale, t * scale, 0}, {-1, t, 0}, {0, 0}, Color::White()},
      {{1 * scale, t * scale, 0}, {1, t, 0}, {1, 0}, Color::White()},
      {{-1 * scale, -t * scale, 0}, {-1, -t, 0}, {0, 1}, Color::White()},
      {{1 * scale, -t * scale, 0}, {1, -t, 0}, {1, 1}, Color::White()},
      {{0, -1 * scale, t * scale}, {0, -1, t}, {0, 0}, Color::White()},
      {{0, 1 * scale, t * scale}, {0, 1, t}, {1, 0}, Color::White()},
      {{0, -1 * scale, -t * scale}, {0, -1, -t}, {0, 1}, Color::White()},
      {{0, 1 * scale, -t * scale}, {0, 1, -t}, {1, 1}, Color::White()},
      {{t * scale, 0, -1 * scale}, {t, 0, -1}, {0, 0}, Color::White()},
      {{t * scale, 0, 1 * scale}, {t, 0, 1}, {1, 0}, Color::White()},
      {{-t * scale, 0, -1 * scale}, {-t, 0, -1}, {0, 1}, Color::White()},
      {{-t * scale, 0, 1 * scale}, {-t, 0, 1}, {1, 1}, Color::White()},
  };

  for (auto &v : vertices) {
    v.normal = v.normal.normalized();
  }

  std::vector<uint32_t> indices = {
      0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11,
      4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3,  9,  4, 3,  4,  2, 3, 2, 6, 3,
      6, 8,  3,  8, 9,  4, 9, 5, 2, 4, 11, 6,  2, 10, 8,  6, 7, 9, 8, 1,
  };

  return Mesh::create(vertices, indices);
}

} // namespace pixel::renderer3d
