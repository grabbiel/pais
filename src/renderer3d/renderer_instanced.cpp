#include "pixel/renderer3d/renderer_instanced.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace pixel::renderer3d {

// ============================================================================
// InstanceData Implementation
// ============================================================================

InstanceGPUData InstanceData::to_gpu_data() const {
  InstanceGPUData gpu_data{};

  gpu_data.position[0] = position.x;
  gpu_data.position[1] = position.y;
  gpu_data.position[2] = position.z;

  gpu_data.rotation[0] = rotation.x;
  gpu_data.rotation[1] = rotation.y;
  gpu_data.rotation[2] = rotation.z;

  gpu_data.scale[0] = scale.x;
  gpu_data.scale[1] = scale.y;
  gpu_data.scale[2] = scale.z;

  gpu_data.color[0] = color.r;
  gpu_data.color[1] = color.g;
  gpu_data.color[2] = color.b;
  gpu_data.color[3] = color.a;

  gpu_data.texture_index = texture_index;
  gpu_data.culling_radius = culling_radius;
  gpu_data.lod_transition_alpha = lod_transition_alpha;
  gpu_data._padding = 0.0f;

  return gpu_data;
}

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

  // Create instance buffer (GPU format only)
  rhi::BufferDesc instance_desc;
  instance_desc.size = max_instances * sizeof(InstanceGPUData);
  instance_desc.usage = rhi::BufferUsage::Vertex;
  instance_desc.hostVisible = true;
  instanced->instance_buffer_ = device->createBuffer(instance_desc);

  instanced->instance_data_.reserve(max_instances);

  std::cout << "Created instanced mesh with capacity for " << max_instances
            << " instances (GPU buffer: " << instance_desc.size << " bytes)"
            << std::endl;

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

  // Convert to GPU format and upload
  if (instance_count_ > 0) {
    std::vector<InstanceGPUData> gpu_data;
    gpu_data.reserve(instance_count_);
    for (size_t i = 0; i < instance_count_; ++i) {
      gpu_data.push_back(instance_data_[i].to_gpu_data());
    }

    auto *cmd = device_->getImmediate();
    cmd->begin();
    cmd->copyToBuffer(instance_buffer_, 0,
                      std::span<const std::byte>(
                          reinterpret_cast<const std::byte *>(gpu_data.data()),
                          instance_count_ * sizeof(InstanceGPUData)));
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

  // Convert to GPU format and update single instance
  InstanceGPUData gpu_data = data.to_gpu_data();

  auto *cmd = device_->getImmediate();
  cmd->begin();
  cmd->copyToBuffer(
      instance_buffer_, index * sizeof(InstanceGPUData),
      std::span<const std::byte>(reinterpret_cast<const std::byte *>(&gpu_data),
                                 sizeof(InstanceGPUData)));
  cmd->end();
}

void InstancedMesh::draw(rhi::CmdList *cmd) const {
  if (instance_count_ == 0)
    return;

  cmd->setVertexBuffer(vertex_buffer_);
  cmd->setIndexBuffer(index_buffer_);
  cmd->setInstanceBuffer(instance_buffer_, sizeof(InstanceGPUData));
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
  cmd->setPipeline(shader->pipeline(base_material.blend_mode));

  // Build identity model matrix (instances handle their own transforms)
  glm::mat4 model = glm::mat4(1.0f);

  // Set model matrix
  cmd->setUniformMat4("model", glm::value_ptr(model));

  // Calculate and set normal matrix (identity in this case, but required by
  // shader) For identity matrix, normal matrix is also identity
  glm::mat3 normalMatrix3x3 = glm::mat3(1.0f);
  glm::mat4 normalMatrix4x4 = glm::mat4(normalMatrix3x3);
  cmd->setUniformMat4("normalMatrix", glm::value_ptr(normalMatrix4x4));

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
    cmd->setTexture("uTextureArray", base_material.texture_array, 1);
    cmd->setUniformInt("useTextureArray", 1);
  } else {
    cmd->setUniformInt("useTextureArray", 0);
  }

  mesh.draw(cmd);
}

} // namespace pixel::renderer3d
