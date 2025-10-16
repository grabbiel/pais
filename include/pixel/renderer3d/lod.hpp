#pragma once
#include "pixel/renderer3d/renderer_instanced.hpp"
#include <array>
#include <memory>

namespace pixel::renderer3d {

// ============================================================================
// LOD Configuration
// ============================================================================

enum class LODLevel {
  High = 0,   // Full detail
  Medium = 1, // Reduced detail
  Low = 2,    // Minimal detail
  COUNT = 3
};

enum class LODMode {
  Distance,    // Traditional distance-based LOD
  ScreenSpace, // Screen-space coverage based LOD
  Hybrid       // Combined distance + screen-space
};

struct LODConfig {
  LODMode mode = LODMode::Hybrid;

  // Distance-based thresholds (in world units)
  float distance_high = 20.0f;   // Switch to medium after this distance
  float distance_medium = 50.0f; // Switch to low after this distance
  float distance_cull = 100.0f;  // Cull completely after this distance

  // Screen-space thresholds (in pixels)
  // Calculated as: screen_height * threshold_percentage
  float screenspace_high = 0.15f;   // > 15% of screen height = high detail
  float screenspace_medium = 0.05f; // > 5% of screen height = medium detail
  float screenspace_low = 0.01f;    // > 1% of screen height = low detail
  // Below screenspace_low = culled

  // Hybrid mode weights (0.0 = pure distance, 1.0 = pure screen-space)
  float hybrid_screenspace_weight = 0.5f;

  // Hysteresis to prevent popping
  float hysteresis = 2.0f; // Distance/size buffer for LOD transitions

  // Object reference size for screen-space calculations
  // This represents the "typical" object size in your scene
  // Used to normalize screen-space calculations
  float reference_object_size = 1.0f;
};

// ============================================================================
// LOD Mesh Management
// ============================================================================

class LODMesh {
public:
  static std::unique_ptr<LODMesh> create(const Mesh &high_detail,
                                         const Mesh &medium_detail,
                                         const Mesh &low_detail,
                                         size_t max_instances_per_lod = 10000,
                                         const LODConfig &config = LODConfig());

  ~LODMesh();

  // Set all instances (LOD assignment computed on GPU)
  void set_instances(const std::vector<InstanceData> &instances);

  // Update instance data
  void update_instance(size_t index, const InstanceData &data);

  // Compute LOD levels and distribute instances to LOD buffers
  // Pass viewport dimensions for screen-space calculations
  void compute_lod_distribution(const Renderer &renderer);

  // Draw all LOD levels
  void draw_all_lods() const;

  // Get statistics
  struct LODStats {
    uint32_t total_instances = 0;
    uint32_t instances_per_lod[3] = {0, 0, 0};
    uint32_t visible_per_lod[3] = {0, 0, 0};
    uint32_t culled = 0;

    // Additional metrics for screen-space LOD
    float avg_screen_size_per_lod[3] = {0, 0, 0};
    float min_screen_size = 0.0f;
    float max_screen_size = 0.0f;
  };

  LODStats get_stats() const;

  const LODConfig &config() const { return config_; }
  LODConfig &config() { return config_; }

  bool using_gpu_lod() const { return gpu_lod_enabled_; }

private:
  LODMesh() = default;

  void setup_lod_compute_shader();
  void setup_lod_instance_buffers();

  // LOD meshes
  std::array<std::unique_ptr<InstancedMesh>, 3> lod_meshes_;

  // Source instance data
  std::vector<InstanceData> source_instances_;
  size_t total_instance_count_ = 0;
  size_t max_instances_per_lod_ = 0;

  // GPU LOD computation
  bool gpu_lod_enabled_ = false;
  uint32_t lod_compute_shader_ = 0;

  // SSBOs for LOD computation
  uint32_t source_instances_ssbo_ = 0;     // Input: all instances
  uint32_t lod_assignments_ssbo_ = 0;      // Output: LOD level per instance
  uint32_t lod_counters_ssbo_ = 0;         // Output: count per LOD level
  uint32_t lod_instance_indices_ssbo_ = 0; // Output: instance indices per LOD

  LODConfig config_;
  mutable LODStats last_stats_;
};

// ============================================================================
// Screen-Space Utilities
// ============================================================================

namespace screen_space {

// Calculate approximate screen-space size of a sphere
// Returns size as fraction of screen height (0.0 - 1.0)
inline float calculate_sphere_screen_size(const Vec3 &world_pos,
                                          float world_radius,
                                          const float *view_matrix,
                                          const float *projection_matrix,
                                          int viewport_height) {

  // Transform position to view space
  glm::mat4 view, proj;
  memcpy(&view[0][0], view_matrix, 16 * sizeof(float));
  memcpy(&proj[0][0], projection_matrix, 16 * sizeof(float));

  glm::vec4 view_pos =
      view * glm::vec4(world_pos.x, world_pos.y, world_pos.z, 1.0f);
  float distance = std::abs(view_pos.z);

  if (distance < 0.001f)
    return 1.0f; // Very close = max size

  // Get FOV from projection matrix
  float fov_y_rad = 2.0f * std::atan(1.0f / proj[1][1]);

  // Calculate screen-space size
  // size_pixels = (world_radius / distance) * viewport_height / tan(fov/2)
  float size_pixels =
      (world_radius / distance) * viewport_height / std::tan(fov_y_rad * 0.5f);

  // Return as fraction of screen height
  return size_pixels / viewport_height;
}

// Calculate LOD level based on screen-space size
inline LODLevel determine_lod_screenspace(float screen_size_fraction,
                                          const LODConfig &config) {

  if (screen_size_fraction < config.screenspace_low) {
    return LODLevel::COUNT; // Cull
  } else if (screen_size_fraction < config.screenspace_medium) {
    return LODLevel::Low;
  } else if (screen_size_fraction < config.screenspace_high) {
    return LODLevel::Medium;
  } else {
    return LODLevel::High;
  }
}

// Hybrid LOD calculation
inline LODLevel determine_lod_hybrid(float distance, float screen_size_fraction,
                                     const LODConfig &config) {

  // Calculate distance-based LOD score (0 = high, 1 = medium, 2 = low, 3 =
  // cull)
  float distance_score;
  if (distance < config.distance_high) {
    distance_score = 0.0f;
  } else if (distance < config.distance_medium) {
    distance_score = 1.0f + (distance - config.distance_high) /
                                (config.distance_medium - config.distance_high);
  } else if (distance < config.distance_cull) {
    distance_score = 2.0f + (distance - config.distance_medium) /
                                (config.distance_cull - config.distance_medium);
  } else {
    distance_score = 3.0f;
  }

  // Calculate screen-space LOD score
  float screenspace_score;
  if (screen_size_fraction >= config.screenspace_high) {
    screenspace_score = 0.0f;
  } else if (screen_size_fraction >= config.screenspace_medium) {
    screenspace_score =
        1.0f + (config.screenspace_high - screen_size_fraction) /
                   (config.screenspace_high - config.screenspace_medium);
  } else if (screen_size_fraction >= config.screenspace_low) {
    screenspace_score =
        2.0f + (config.screenspace_medium - screen_size_fraction) /
                   (config.screenspace_medium - config.screenspace_low);
  } else {
    screenspace_score = 3.0f;
  }

  // Blend scores
  float weight = config.hybrid_screenspace_weight;
  float final_score =
      distance_score * (1.0f - weight) + screenspace_score * weight;

  // Map back to LOD level
  if (final_score < 0.5f)
    return LODLevel::High;
  else if (final_score < 1.5f)
    return LODLevel::Medium;
  else if (final_score < 2.5f)
    return LODLevel::Low;
  else
    return LODLevel::COUNT; // Cull
}

} // namespace screen_space

// ============================================================================
// Renderer Extensions for LOD
// ============================================================================

class RendererLOD {
public:
  // Create LOD mesh from multiple detail levels
  static std::unique_ptr<LODMesh>
  create_lod_mesh(const Mesh &high_detail, const Mesh &medium_detail,
                  const Mesh &low_detail, size_t max_instances_per_lod = 10000,
                  const LODConfig &config = LODConfig());

  // Generate meshes at different LOD levels
  static std::unique_ptr<Mesh> create_cube_high_detail(Renderer &r,
                                                       float size = 1.0f);
  static std::unique_ptr<Mesh> create_cube_medium_detail(Renderer &r,
                                                         float size = 1.0f);
  static std::unique_ptr<Mesh> create_cube_low_detail(Renderer &r,
                                                      float size = 1.0f);

  static std::unique_ptr<Mesh> create_sphere_high_detail(Renderer &r,
                                                         float radius = 0.5f);
  static std::unique_ptr<Mesh> create_sphere_medium_detail(Renderer &r,
                                                           float radius = 0.5f);
  static std::unique_ptr<Mesh> create_sphere_low_detail(Renderer &r,
                                                        float radius = 0.5f);

  // Draw LOD mesh with automatic LOD selection and culling
  static void draw_lod(Renderer &renderer, LODMesh &mesh,
                       const Material &base_material = Material());
};

} // namespace pixel::renderer3d
