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

struct LODConfig {
  float distance_high = 20.0f;   // Switch to medium after this distance
  float distance_medium = 50.0f; // Switch to low after this distance
  float distance_cull = 100.0f;  // Cull completely after this distance

  // Hysteresis to prevent popping
  float hysteresis = 2.0f; // Distance buffer for LOD transitions
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
  void compute_lod_distribution(const Renderer &renderer);

  // Draw all LOD levels
  void draw_all_lods() const;

  // Get statistics
  struct LODStats {
    uint32_t total_instances = 0;
    uint32_t instances_per_lod[3] = {0, 0, 0};
    uint32_t visible_per_lod[3] = {0, 0, 0};
    uint32_t culled = 0;
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
