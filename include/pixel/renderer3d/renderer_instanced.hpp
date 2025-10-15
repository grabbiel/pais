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

private:
  InstancedMesh() = default;
  void setup_instance_buffer();
  void update_instance_buffer();

  uint32_t vao_ = 0;
  uint32_t vbo_ = 0;          // Base mesh VBO
  uint32_t ebo_ = 0;          // Base mesh EBO
  uint32_t instance_vbo_ = 0; // Instance data VBO

  size_t index_count_ = 0;
  size_t instance_count_ = 0;
  size_t max_instances_ = 0;

  std::vector<InstanceData> instance_data_;
  bool needs_update_ = false;
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
};

} // namespace pixel::renderer3d
