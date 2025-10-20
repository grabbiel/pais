#include "pixel/renderer3d/renderer_instanced.hpp"
#include <cstring>
#include <iostream>

namespace pixel::renderer3d {

// ============================================================================
// InstancedMesh Implementation
// ============================================================================

std::unique_ptr<InstancedMesh> InstancedMesh::create(rhi::Device *device,
                                                     const Mesh &mesh,
                                                     size_t max_instances) {
  auto instanced = std::unique_ptr<InstancedMesh>(new InstancedMesh());
  instanced->device_ = device;
  instanced->vertex_buffer_ = mesh.vertex_buffer();
  instanced->index_buffer_ = mesh.index_buffer();
  instanced->vertex_count_ = mesh.vertex_count();
  instanced->index_count_ = mesh.index_count();
  instanced->max_instances_ = max_instances;
  instanced->instance_count_ = 0;

  // Create instance buffer
  rhi::BufferDesc instance_desc;
  instance_desc.size = max_instances * sizeof(InstanceData);
  instance_desc.usage = rhi::BufferUsage::Vertex;
  instance_desc.hostVisible = true;
  instanced->instance_buffer_ = device->createBuffer(instance_desc);

  instanced->instance_data_.reserve(max_instances);

  std::cout << "Created instanced mesh with capacity for " << max_instances
            << " instances" << std::endl;

  return instanced;
}

InstancedMesh::~InstancedMesh() {
  // RHI handles cleanup
}

void InstancedMesh::set_instances(const std::vector<InstanceData> &instances) {
  if (instances.size() > max_instances_) {
    std::cerr << "Warning: Instance count " << instances.size()
              << " exceeds max " << max_instances_ << std::endl;
    instance_count_ = max_instances_;
  } else {
    instance_count_ = instances.size();
  }

  instance_data_ = instances;
  if (instance_data_.size() > max_instances_) {
    instance_data_.resize(max_instances_);
  }

  // Upload instance data to GPU
  if (instance_count_ > 0) {
    auto *cmd = device_->getImmediate();
    cmd->begin();
    cmd->copyToBuffer(
        instance_buffer_, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte *>(instance_data_.data()),
            instance_count_ * sizeof(InstanceData)));
    cmd->end();
  }
}

void InstancedMesh::update_instance(size_t index, const InstanceData &data) {
  if (index >= instance_count_) {
    std::cerr << "Warning: Instance index " << index
              << " out of range (count: " << instance_count_ << ")"
              << std::endl;
    return;
  }

  instance_data_[index] = data;

  // Update single instance on GPU
  auto *cmd = device_->getImmediate();
  cmd->begin();
  cmd->copyToBuffer(
      instance_buffer_, index * sizeof(InstanceData),
      std::span<const std::byte>(reinterpret_cast<const std::byte *>(&data),
                                 sizeof(InstanceData)));
  cmd->end();
}

void InstancedMesh::draw(rhi::CmdList *cmd) const {
  if (instance_count_ == 0)
    return;

  cmd->setVertexBuffer(vertex_buffer_);
  cmd->setIndexBuffer(index_buffer_);
  cmd->setInstanceBuffer(instance_buffer_, sizeof(InstanceData));
  cmd->drawIndexed(index_count_, 0, instance_count_);
}

// ============================================================================
// RendererInstanced Implementation
// ============================================================================

std::unique_ptr<InstancedMesh>
RendererInstanced::create_instanced_mesh(rhi::Device *device, const Mesh &mesh,
                                         size_t max_instances) {
  return InstancedMesh::create(device, mesh, max_instances);
}

void RendererInstanced::draw_instanced(Renderer &renderer,
                                       const InstancedMesh &mesh,
                                       const Material &base_material) {
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

  // Set time uniform for animated dither (if shader supports it)
  cmd->setUniformFloat("uTime", static_cast<float>(renderer.time()));

  // Set dither mode (0 = off, 1 = static, 2 = animated)
  cmd->setUniformInt("uDitherEnabled", 1);

  // Set texture array if available
  if (base_material.texture_array.id != 0) {
    cmd->setTexture("uTextureArray", base_material.texture_array, 0);
    cmd->setUniformInt("useTextureArray", 1);
  } else {
    cmd->setUniformInt("useTextureArray", 0);
  }

  mesh.draw(cmd);
}

} // namespace pixel::renderer3d
