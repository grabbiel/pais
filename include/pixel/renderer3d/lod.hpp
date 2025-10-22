#pragma once
#include "renderer.hpp"
#include "renderer_instanced.hpp"
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace pixel::renderer3d {
// ============================================================================
// LOD Level Enum
// ============================================================================

enum class LODLevel : uint32_t { High = 0, Medium = 1, Low = 2, Culled = 3 };
// ============================================================================
// LOD Configuration
// ============================================================================

enum class LODMode {
  Distance,    // Distance-based LOD
  ScreenSpace, // Screen-space based LOD (projected size)
  Hybrid       // Combination of both
};

struct LODConfig {
  LODMode mode = LODMode::Hybrid;

  // Distance thresholds (world units)
  float distance_high = 10.0f;
  float distance_medium = 30.0f;
  float distance_cull = 100.0f;

  // Screen-space thresholds (pixels)
  float screenspace_high = 100.0f;
  float screenspace_medium = 30.0f;
  float screenspace_cull = 5.0f;

  // Hybrid mode weight (0.0 = distance only, 1.0 = screenspace only)
  float hybrid_screenspace_weight = 0.5f;

  // Temporal coherence (prevents rapid LOD switching)
  struct TemporalSettings {
    bool enabled = true;
    float hysteresis_factor = 0.2f;
    float min_stable_time = 0.1f;
    int min_stable_frames = 3;
    float upgrade_delay = 0.1f;
    float downgrade_delay = 0.3f;
    float distance_hysteresis = 3.0f;
    float screenspace_hysteresis = 0.015f;
  } temporal;

  // Dithered transitions
  struct DitherSettings {
    bool enabled = true;
    float crossfade_duration = 0.25f;
    float dither_pattern_scale = 1.0f;
    bool temporal_jitter = true;
  } dither;

  struct GPUSettings {
    // When true we attempt to keep per-frame LOD selection on the GPU.  The
    // current implementation requires CPU readbacks which stall rendering, so
    // we default this to false until an async path is implemented.
    bool enabled = false;
  } gpu;
};

// ============================================================================
// LOD State (per instance)
// ============================================================================

struct InstanceLODState {
  uint32_t current_lod = 0;
  uint32_t target_lod = 0;
  uint32_t previous_lod = 0;
  float transition_time = 0.0f;
  float transition_alpha = 1.0f;
  int stable_frames = 0;
  bool is_crossfading = false;
};

// ============================================================================
// HLOD Tree (Hierarchical LOD)
// ============================================================================

struct HLODTree {
  struct Node {
    uint32_t cluster_id;
    std::vector<uint32_t> children;
    std::shared_ptr<Mesh> proxy_mesh;
    Vec3 bounds_center;
    float bounds_radius;
  };

  std::vector<Node> nodes;
  std::unordered_map<uint32_t, size_t> cluster_to_node;
};

// ============================================================================
// LOD Mesh
// ============================================================================

class LODMesh {
public:
  static std::unique_ptr<LODMesh>
  create(rhi::Device *device, const Mesh &high_detail,
         const Mesh &medium_detail, const Mesh &low_detail,
         size_t max_instances_per_lod, const LODConfig &config = LODConfig());

  ~LODMesh();

  void set_instances(const std::vector<InstanceData> &instances);
  void update_instance(size_t index, const InstanceData &data);

  // Update LOD selection based on camera
  void update_lod_selection(Renderer &renderer, double current_time);

  // Draw all LOD levels
  void draw_all_lods(rhi::CmdList *cmd) const;

  InstancedMesh *lod_mesh(size_t lod_index);
  const InstancedMesh *lod_mesh(size_t lod_index) const;

  // Get statistics
  struct LODStats {
    uint32_t total_instances = 0;
    uint32_t instances_per_lod[3] = {0, 0, 0};
    uint32_t visible_per_lod[3] = {0, 0, 0};
    uint32_t culled = 0;

    float avg_screen_size_per_lod[3] = {0, 0, 0};
    float min_screen_size = 0.0f;
    float max_screen_size = 0.0f;
  };

  LODStats get_stats() const;

  const LODConfig &config() const { return config_; }
  LODConfig &config() { return config_; }

private:
  LODMesh() = default;

  struct GPUResources {
    bool initialized = false;
    rhi::ShaderHandle compute_shader{};
    rhi::PipelineHandle compute_pipeline{};
    rhi::BufferHandle source_instances{};
    rhi::BufferHandle lod_assignments{};
    rhi::BufferHandle lod_counters{};
    rhi::BufferHandle lod_instance_indices{};
    rhi::BufferHandle uniform_buffer{};
  };

  void update_lod_selection_cpu(Renderer &renderer, float delta_time,
                                bool detailed_log);
  void update_lod_selection_gpu(Renderer &renderer, float delta_time,
                                bool detailed_log);

  void apply_lod_results(const std::vector<uint32_t> &desired_lods,
                         float delta_time, bool detailed_log,
                         Renderer &renderer);

  uint32_t compute_lod_direct(const InstanceData &inst, float distance,
                              float screen_size,
                              const Renderer &renderer) const;

  bool initialize_gpu_resources(size_t max_instances);

  rhi::Device *device_ = nullptr;

  std::array<std::unique_ptr<InstancedMesh>, 3> lod_meshes_;

  std::vector<InstanceData> source_instances_;
  size_t total_instance_count_ = 0;
  size_t max_instances_per_lod_ = 0;

  LODConfig config_;
  std::vector<InstanceLODState> instance_lod_states_;
  double last_update_time_ = 0.0;

  bool use_gpu_lod_ = false;
  GPUResources gpu_{};
  std::vector<uint32_t> gpu_lod_assignments_;
  std::array<uint32_t, 4> gpu_lod_counters_{0, 0, 0, 0};

  mutable LODStats last_stats_;
};

// ============================================================================
// LOD Renderer
// ============================================================================

class RendererLOD {
public:
  static void draw_lod(Renderer &renderer, LODMesh &lod_mesh,
                       const Material &material);
};

} // namespace pixel::renderer3d
