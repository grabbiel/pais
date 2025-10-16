#include "pixel/renderer3d/lod.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>

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

std::unique_ptr<LODMesh> LODMesh::create(const Mesh &high_detail,
                                         const Mesh &medium_detail,
                                         const Mesh &low_detail,
                                         size_t max_instances_per_lod,
                                         const LODConfig &config) {
  auto lod_mesh = std::unique_ptr<LODMesh>(new LODMesh());

  lod_mesh->max_instances_per_lod_ = max_instances_per_lod;
  lod_mesh->config_ = config;

  lod_mesh->lod_meshes_[0] =
      InstancedMesh::create(high_detail, max_instances_per_lod);
  lod_mesh->lod_meshes_[1] =
      InstancedMesh::create(medium_detail, max_instances_per_lod);
  lod_mesh->lod_meshes_[2] =
      InstancedMesh::create(low_detail, max_instances_per_lod);

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
}

void LODMesh::update_instance(size_t index, const InstanceData &data) {
  if (index < source_instances_.size()) {
    source_instances_[index] = data;
  }
}

void LODMesh::compute_lod_distribution(const Renderer &renderer) {
  if (!gpu_lod_enabled_ || total_instance_count_ == 0) {
    // CPU fallback with screen-space support
    std::vector<InstanceData> high_lod, medium_lod, low_lod;

    Vec3 cam_pos = renderer.camera().position;

    float view[16], proj[16];
    renderer.camera().get_view_matrix(view);
    renderer.camera().get_projection_matrix(proj, renderer.window_width(),
                                            renderer.window_height());
    int viewport_height = renderer.window_height();

    for (const auto &inst : source_instances_) {
      float dx = inst.position.x - cam_pos.x;
      float dy = inst.position.y - cam_pos.y;
      float dz = inst.position.z - cam_pos.z;
      float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

      float max_scale = std::max({inst.scale.x, inst.scale.y, inst.scale.z});
      float effective_radius = inst.culling_radius * max_scale;

      LODLevel lod;

      if (config_.mode == LODMode::ScreenSpace) {
        float screen_size = screen_space::calculate_sphere_screen_size(
            inst.position, effective_radius, view, proj, viewport_height);
        lod = screen_space::determine_lod_screenspace(screen_size, config_);
      } else if (config_.mode == LODMode::Hybrid) {
        float screen_size = screen_space::calculate_sphere_screen_size(
            inst.position, effective_radius, view, proj, viewport_height);
        lod = screen_space::determine_lod_hybrid(dist, screen_size, config_);
      } else {
        // Distance-based
        if (dist < config_.distance_high) {
          lod = LODLevel::High;
        } else if (dist < config_.distance_medium) {
          lod = LODLevel::Medium;
        } else if (dist < config_.distance_cull) {
          lod = LODLevel::Low;
        } else {
          lod = LODLevel::COUNT; // Cull
        }
      }

      if (lod == LODLevel::High) {
        high_lod.push_back(inst);
      } else if (lod == LODLevel::Medium) {
        medium_lod.push_back(inst);
      } else if (lod == LODLevel::Low) {
        low_lod.push_back(inst);
      }
    }

    lod_meshes_[0]->set_instances(high_lod);
    lod_meshes_[1]->set_instances(medium_lod);
    lod_meshes_[2]->set_instances(low_lod);

    last_stats_.total_instances = total_instance_count_;
    last_stats_.instances_per_lod[0] = high_lod.size();
    last_stats_.instances_per_lod[1] = medium_lod.size();
    last_stats_.instances_per_lod[2] = low_lod.size();
    last_stats_.culled = total_instance_count_ - high_lod.size() -
                         medium_lod.size() - low_lod.size();

    return;
  }

  // GPU path: upload source instances
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, source_instances_ssbo_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  total_instance_count_ * sizeof(InstanceData),
                  source_instances_.data());

  // Reset counters
  uint32_t zero_counters[4] = {0, 0, 0, 0};
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lod_counters_ssbo_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 4 * sizeof(uint32_t),
                  zero_counters);

  // Get camera position and matrices
  Vec3 cam_pos = renderer.camera().position;

  float view[16], proj[16];
  renderer.camera().get_view_matrix(view);
  renderer.camera().get_projection_matrix(proj, renderer.window_width(),
                                          renderer.window_height());

  glm::mat4 view_mat, proj_mat;
  memcpy(&view_mat[0][0], view, 16 * sizeof(float));
  memcpy(&proj_mat[0][0], proj, 16 * sizeof(float));
  glm::mat4 vp = proj_mat * view_mat;

  // Extract frustum planes
  Vec4 frustum_planes[6];
  const float *m = glm::value_ptr(vp);
  frustum_planes[0] = {m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]};
  frustum_planes[1] = {m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]};
  frustum_planes[2] = {m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]};
  frustum_planes[3] = {m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]};
  frustum_planes[4] = {m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]};
  frustum_planes[5] = {m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]};

  // Normalize planes
  for (int i = 0; i < 6; i++) {
    float len = std::sqrt(frustum_planes[i].x * frustum_planes[i].x +
                          frustum_planes[i].y * frustum_planes[i].y +
                          frustum_planes[i].z * frustum_planes[i].z);
    if (len > 0) {
      frustum_planes[i].x /= len;
      frustum_planes[i].y /= len;
      frustum_planes[i].z /= len;
      frustum_planes[i].w /= len;
    }
  }

  // Bind compute shader and set uniforms
  glUseProgram(lod_compute_shader_);

  GLint cam_pos_loc =
      glGetUniformLocation(lod_compute_shader_, "camera_position");
  glUniform3f(cam_pos_loc, cam_pos.x, cam_pos.y, cam_pos.z);

  GLint view_loc = glGetUniformLocation(lod_compute_shader_, "view_matrix");
  glUniformMatrix4fv(view_loc, 1, GL_FALSE, view);

  GLint proj_loc =
      glGetUniformLocation(lod_compute_shader_, "projection_matrix");
  glUniformMatrix4fv(proj_loc, 1, GL_FALSE, proj);

  GLint total_loc =
      glGetUniformLocation(lod_compute_shader_, "total_instances");
  glUniform1ui(total_loc, total_instance_count_);

  GLint viewport_loc =
      glGetUniformLocation(lod_compute_shader_, "viewport_height");
  glUniform1i(viewport_loc, renderer.window_height());

  // LOD mode
  GLint mode_loc = glGetUniformLocation(lod_compute_shader_, "lod_mode");
  glUniform1i(mode_loc, static_cast<int>(config_.mode));

  // Distance thresholds
  GLint dist_high_loc =
      glGetUniformLocation(lod_compute_shader_, "distance_high");
  glUniform1f(dist_high_loc, config_.distance_high);

  GLint dist_med_loc =
      glGetUniformLocation(lod_compute_shader_, "distance_medium");
  glUniform1f(dist_med_loc, config_.distance_medium);

  GLint dist_cull_loc =
      glGetUniformLocation(lod_compute_shader_, "distance_cull");
  glUniform1f(dist_cull_loc, config_.distance_cull);

  // Screen-space thresholds
  GLint ss_high_loc =
      glGetUniformLocation(lod_compute_shader_, "screenspace_high");
  glUniform1f(ss_high_loc, config_.screenspace_high);

  GLint ss_med_loc =
      glGetUniformLocation(lod_compute_shader_, "screenspace_medium");
  glUniform1f(ss_med_loc, config_.screenspace_medium);

  GLint ss_low_loc =
      glGetUniformLocation(lod_compute_shader_, "screenspace_low");
  glUniform1f(ss_low_loc, config_.screenspace_low);

  // Hybrid weight
  GLint hybrid_loc = glGetUniformLocation(lod_compute_shader_, "hybrid_weight");
  glUniform1f(hybrid_loc, config_.hybrid_screenspace_weight);

  // Frustum planes
  GLint planes_loc =
      glGetUniformLocation(lod_compute_shader_, "frustum_planes");
  glUniform4fv(planes_loc, 6, (float *)frustum_planes);

  // Bind SSBOs
  typedef void(APIENTRYP PFNGLBINDBUFFERBASEPROC)(GLenum, GLuint, GLuint);
  PFNGLBINDBUFFERBASEPROC glBindBufferBaseFunc =
      (PFNGLBINDBUFFERBASEPROC)glfwGetProcAddress("glBindBufferBase");

  if (glBindBufferBaseFunc) {
    glBindBufferBaseFunc(GL_SHADER_STORAGE_BUFFER, 0, source_instances_ssbo_);
    glBindBufferBaseFunc(GL_SHADER_STORAGE_BUFFER, 1, lod_assignments_ssbo_);
    glBindBufferBaseFunc(GL_SHADER_STORAGE_BUFFER, 2, lod_counters_ssbo_);
    glBindBufferBaseFunc(GL_SHADER_STORAGE_BUFFER, 3,
                         lod_instance_indices_ssbo_);
  }

  // Dispatch compute shader
  uint32_t work_groups = (total_instance_count_ + 255) / 256;
  glDispatchCompute(work_groups, 1, 1);

  // Memory barrier
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  glUseProgram(0);

  // Read back LOD counters and instance indices
  uint32_t counters[4];
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lod_counters_ssbo_);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 4 * sizeof(uint32_t),
                     counters);

  last_stats_.total_instances = total_instance_count_;
  last_stats_.instances_per_lod[0] = counters[0];
  last_stats_.instances_per_lod[1] = counters[1];
  last_stats_.instances_per_lod[2] = counters[2];
  last_stats_.culled = counters[3];

  // Read back instance indices and populate LOD meshes
  std::vector<uint32_t> indices(total_instance_count_ * 3);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lod_instance_indices_ssbo_);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     total_instance_count_ * 3 * sizeof(uint32_t),
                     indices.data());

  // Build instance lists for each LOD
  for (int lod = 0; lod < 3; lod++) {
    std::vector<InstanceData> lod_instances;
    lod_instances.reserve(counters[lod]);

    uint32_t base_offset = lod * total_instance_count_;
    for (uint32_t i = 0; i < counters[lod]; i++) {
      uint32_t src_index = indices[base_offset + i];
      if (src_index < total_instance_count_) {
        lod_instances.push_back(source_instances_[src_index]);
      }
    }

    lod_meshes_[lod]->set_instances(lod_instances);
  }

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
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

// ============================================================================
// RendererLOD Implementation
// ============================================================================

std::unique_ptr<LODMesh> RendererLOD::create_lod_mesh(
    const Mesh &high_detail, const Mesh &medium_detail, const Mesh &low_detail,
    size_t max_instances_per_lod, const LODConfig &config) {
  return LODMesh::create(high_detail, medium_detail, low_detail,
                         max_instances_per_lod, config);
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
