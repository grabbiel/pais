#pragma once
#include "renderer.hpp"
#include <vector>

namespace pixel::renderer3d {

// ============================================================================
// Instanced Rendering Structures
// ============================================================================

struct InstanceData {
  Vec3 position{0, 0, 0};
  Vec3 rotation{0, 0, 0};
  Vec3 scale{1, 1, 1};
  Color color = Color::White();
  float texture_index = 0.0f;
  float culling_radius = 1.0f; // Bounding sphere radius for culling
  float _padding[2] = {0, 0};  // Alignment padding
};

// GPU-side draw command structure (matches glDrawElementsIndirect)
struct DrawCommand {
  uint32_t index_count;
  uint32_t instance_count;
  uint32_t first_index;
  uint32_t base_vertex;
  uint32_t base_instance;
};

class InstancedMesh {
public:
  static std::unique_ptr<InstancedMesh> create(const Mesh &base_mesh,
                                               size_t max_instances = 1000);
  ~InstancedMesh();

  void set_instances(const std::vector<InstanceData> &instances);
  void update_instance(size_t index, const InstanceData &data);

  size_t instance_count() const { return instance_count_; }
  size_t max_instances() const { return max_instances_; }

  void draw() const;

  uint32_t vao() const { return vao_; }
  size_t index_count() const { return index_count_; }

  bool using_persistent_mapping() const { return persistent_mapped_; }
  bool using_gpu_culling() const { return gpu_culling_enabled_; }

  // GPU culling - updates visibility buffer on GPU
  void compute_gpu_culling(const Renderer &renderer);

  // Get visible instance count (after GPU culling)
  uint32_t get_visible_count() const;

private:
  InstancedMesh() = default;
  void setup_instance_buffer();
  void setup_persistent_buffer();
  void setup_gpu_culling_buffers();
  void update_instance_buffer();
  void update_persistent_buffer();
  void wait_for_previous_frame();

  uint32_t vao_ = 0;
  uint32_t vbo_ = 0;
  uint32_t ebo_ = 0;
  uint32_t instance_vbo_ = 0;

  size_t index_count_ = 0;
  size_t instance_count_ = 0;
  size_t max_instances_ = 0;

  std::vector<InstanceData> instance_data_;
  bool needs_update_ = false;

  bool persistent_mapped_ = false;
  void *mapped_buffer_ = nullptr;
  uint64_t sync_fence_ = 0;

  // GPU culling resources
  bool gpu_culling_enabled_ = false;
  uint32_t culling_compute_shader_ = 0;
  uint32_t visible_instances_ssbo_ = 0; // Visible instance indices
  uint32_t draw_command_buffer_ = 0;    // Indirect draw command
  uint32_t instance_input_ssbo_ = 0;    // Input instance data for compute
  uint32_t culling_vao_ = 0;            // VAO for culled rendering
};

// ============================================================================
// Renderer Extensions for Instancing
// ============================================================================

class RendererInstanced {
public:
  static std::unique_ptr<InstancedMesh>
  create_instanced_mesh(const Mesh &mesh, size_t max_instances = 1000);

  static void draw_instanced(Renderer &renderer, const InstancedMesh &mesh,
                             const Material &base_material = Material());

  // Helper functions for instance generation
  static std::vector<InstanceData> create_grid(int width, int depth,
                                               float spacing = 2.0f,
                                               float y_offset = 0.0f);

  static std::vector<InstanceData> create_circle(int count, float radius,
                                                 float y_offset = 0.0f);

  static std::vector<InstanceData>
  create_random(int count, const Vec3 &min_bounds, const Vec3 &max_bounds);

  static void assign_texture_indices(std::vector<InstanceData> &instances,
                                     int num_textures);

  static void
  assign_random_texture_indices(std::vector<InstanceData> &instances,
                                int num_textures);
};

} // namespace pixel::renderer3d
