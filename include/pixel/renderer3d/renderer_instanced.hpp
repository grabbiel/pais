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
  float texture_index = 0.0f; // Index into texture array (0-based)
  float _culling_radius = 1.0f;
  bool _is_visible = true;
  float _padding[3] = {0, 0, 0}; // Padding for alignment
};

class InstancedMesh {
public:
  static std::unique_ptr<InstancedMesh> create(const Mesh &base_mesh,
                                               size_t max_instances = 1000);
  ~InstancedMesh();

  // Set instance data
  void set_instances(const std::vector<InstanceData> &instances);
  void update_instance(size_t index, const InstanceData &data);

  // Get instance count
  size_t instance_count() const { return instance_count_; }
  size_t max_instances() const { return max_instances_; }

  // Draw all instances
  void draw() const;

  // Access to base mesh data
  uint32_t vao() const { return vao_; }
  size_t index_count() const { return index_count_; }

  // Check if using persistent mapped buffers
  bool using_persistent_mapping() const { return persistent_mapped_; }

  void compute_culling(const Vec3 &camera_pos, const Vec3 &camera_dir,
                       float fov_radians, float aspect_ratio, float near_plane,
                       float far_plane);

  const std::vector<InstanceData> &get_visible_instances() const {
    return instance_data_;
  }

private:
  InstancedMesh() = default;
  void setup_instance_buffer();
  void setup_persistent_buffer();
  void update_instance_buffer();
  void update_persistent_buffer();
  void wait_for_previous_frame();

  uint32_t vao_ = 0;
  uint32_t vbo_ = 0;          // Base mesh VBO
  uint32_t ebo_ = 0;          // Base mesh EBO
  uint32_t instance_vbo_ = 0; // Instance data VBO

  size_t index_count_ = 0;
  size_t instance_count_ = 0;
  size_t max_instances_ = 0;

  std::vector<InstanceData> instance_data_;
  bool needs_update_ = false;

  // Persistent mapped buffer support
  bool persistent_mapped_ = false;
  void *mapped_buffer_ = nullptr;
  uint64_t sync_fence_ = 0; // GLsync object for synchronization
};

// ============================================================================
// Renderer Extensions for Instancing
// ============================================================================

class RendererInstanced {
public:
  // Create instanced mesh from base mesh
  static std::unique_ptr<InstancedMesh>
  create_instanced_mesh(const Mesh &mesh, size_t max_instances = 1000);

  // Draw instanced mesh with camera and lighting
  static void draw_instanced(Renderer &renderer, const InstancedMesh &mesh,
                             const Material &base_material = Material());

  // Helper: Generate grid of instances
  static std::vector<InstanceData> create_grid(int width, int depth,
                                               float spacing = 2.0f,
                                               float y_offset = 0.0f);

  // Helper: Generate circular pattern of instances
  static std::vector<InstanceData> create_circle(int count, float radius,
                                                 float y_offset = 0.0f);

  // Helper: Generate random instances within bounds
  static std::vector<InstanceData>
  create_random(int count, const Vec3 &min_bounds, const Vec3 &max_bounds);

  // Helper: Assign texture indices to instances (cycling through available
  // textures)
  static void assign_texture_indices(std::vector<InstanceData> &instances,
                                     int num_textures);

  // Helper: Assign random texture indices
  static void
  assign_random_texture_indices(std::vector<InstanceData> &instances,
                                int num_textures);

  static void
  compute_culling_for_instances(std::vector<InstanceData> &instances,
                                const Vec3 &camera_pos, const Vec3 &camera_dir,
                                float fov_radians, float aspect_ratio,
                                float near_plane, float far_plane);
};

} // namespace pixel::renderer3d
