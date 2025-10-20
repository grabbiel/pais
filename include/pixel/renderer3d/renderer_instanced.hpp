#pragma once
#include "renderer.hpp"
#include <array>
#include <memory>
#include <vector>

namespace pixel::renderer3d {

// ============================================================================
// Instance Data
// ============================================================================

struct InstanceData {
  Vec3 position{0, 0, 0};
  Vec3 rotation{0, 0, 0};
  Vec3 scale{1, 1, 1};
  Color color{1, 1, 1, 1};

  float texture_index = 0.0f;
  float culling_radius = 1.0f;
  float lod_transition_alpha = 1.0f;
  float _padding = 0.0f;
};

// ============================================================================
// Instanced Mesh
// ============================================================================

class InstancedMesh {
public:
  static std::unique_ptr<InstancedMesh>
  create(rhi::Device *device, const Mesh &mesh, size_t max_instances);
  ~InstancedMesh();

  void set_instances(const std::vector<InstanceData> &instances);
  void update_instance(size_t index, const InstanceData &data);

  void draw(rhi::CmdList *cmd) const;

  size_t instance_count() const { return instance_count_; }
  size_t max_instances() const { return max_instances_; }

  rhi::BufferHandle vertex_buffer() const { return vertex_buffer_; }
  rhi::BufferHandle index_buffer() const { return index_buffer_; }
  rhi::BufferHandle instance_buffer() const { return instance_buffer_; }

private:
  InstancedMesh() = default;

  rhi::Device *device_ = nullptr;
  rhi::BufferHandle vertex_buffer_{0};
  rhi::BufferHandle index_buffer_{0};
  rhi::BufferHandle instance_buffer_{0};

  size_t vertex_count_ = 0;
  size_t index_count_ = 0;
  size_t instance_count_ = 0;
  size_t max_instances_ = 0;

  std::vector<InstanceData> instance_data_;
};

// ============================================================================
// Renderer Instanced
// ============================================================================

class RendererInstanced {
public:
  static std::unique_ptr<InstancedMesh>
  create_instanced_mesh(rhi::Device *device, const Mesh &mesh,
                        size_t max_instances);

  static void draw_instanced(Renderer &renderer, const InstancedMesh &mesh,
                             const Material &base_material);
};

} // namespace pixel::renderer3d
