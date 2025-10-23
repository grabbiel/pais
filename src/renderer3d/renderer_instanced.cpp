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
  // === DIAGNOSTIC LOGGING START ===
  std::cerr << "\n" << std::string(70, '=') << std::endl;
  std::cerr << "InstancedMesh::set_instances() CALLED" << std::endl;
  std::cerr << std::string(70, '=') << std::endl;
  std::cerr << "Input:" << std::endl;
  std::cerr << "  instances.size(): " << instances.size() << std::endl;
  std::cerr << "  max_instances_:   " << max_instances_ << std::endl;

  if (instances.empty()) {
    std::cerr << "\n❌ WARNING: Empty instance vector!" << std::endl;
    std::cerr << std::string(70, '=') << "\n" << std::endl;
  }

  if (!instances.empty()) {
    std::cerr << "\nFirst 3 instances:" << std::endl;
    for (size_t i = 0; i < std::min(size_t(3), instances.size()); ++i) {
      const auto &inst = instances[i];
      std::cerr << "  [" << i << "] pos=(" << inst.position.x << ", "
                << inst.position.y << ", " << inst.position.z << "), scale=("
                << inst.scale.x << ", " << inst.scale.y << ", " << inst.scale.z
                << ")" << std::endl;
    }
  }
  // === DIAGNOSTIC LOGGING END ===

  instance_data_ = instances;
  if (instance_data_.size() > max_instances_) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "\n⚠ WARNING: Clamping " << instance_data_.size()
              << " instances to max " << max_instances_ << std::endl;
    // === END ===
    instance_data_.resize(max_instances_);
  }

  instance_count_ = instance_data_.size();

  // Convert to GPU format
  std::vector<InstanceGPUData> gpu_data;
  gpu_data.reserve(instance_count_);
  for (const auto &inst : instance_data_) {
    gpu_data.push_back(inst.to_gpu_data());
  }

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "\nUploading to GPU:" << std::endl;
  std::cerr << "  gpu_data.size():     " << gpu_data.size() << std::endl;
  std::cerr << "  total bytes:         "
            << (gpu_data.size() * sizeof(InstanceGPUData)) << std::endl;
  std::cerr << "  instance_buffer_.id: " << instance_buffer_.id << std::endl;
  // === END ===

  // Upload to GPU
  if (!gpu_data.empty() && instance_buffer_.id != 0) {
    auto *cmd = device_->getImmediate();
    std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte *>(gpu_data.data()),
        gpu_data.size() * sizeof(InstanceGPUData));
    cmd->copyToBuffer(instance_buffer_, 0, bytes);

    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ✓ Instance data uploaded to GPU" << std::endl;
    std::cerr << "\nFinal state:" << std::endl;
    std::cerr << "  instance_count_: " << instance_count_ << std::endl;
    std::cerr << std::string(70, '=') << "\n" << std::endl;
    // === END ===
  } else {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ❌ ERROR: Cannot upload to GPU!" << std::endl;
    if (gpu_data.empty()) {
      std::cerr << "     - gpu_data is empty" << std::endl;
    }
    if (instance_buffer_.id == 0) {
      std::cerr << "     - instance_buffer_ handle is invalid (0)" << std::endl;
    }
    std::cerr << std::string(70, '=') << "\n" << std::endl;
    // === END ===
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
  cmd->copyToBuffer(
      instance_buffer_, index * sizeof(InstanceGPUData),
      std::span<const std::byte>(reinterpret_cast<const std::byte *>(&gpu_data),
                                 sizeof(InstanceGPUData)));
}

void InstancedMesh::draw(rhi::CmdList *cmd) const {
  // === DIAGNOSTIC LOGGING ===
  std::cerr << "\n--- InstancedMesh::draw() ---" << std::endl;
  std::cerr << "  instance_count_: " << instance_count_ << std::endl;
  // === END ===

  if (instance_count_ == 0) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  Skipping draw (no instances)" << std::endl;
    std::cerr << "------------------------------\n" << std::endl;
    // === END ===
    return;
  }

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "  Setting buffers..." << std::endl;
  // === END ===

  cmd->setVertexBuffer(vertex_buffer_);
  cmd->setIndexBuffer(index_buffer_);
  cmd->setInstanceBuffer(instance_buffer_, sizeof(InstanceGPUData));

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "  ✓ Buffers set" << std::endl;
  std::cerr << "  Calling drawIndexed(" << index_count_ << ", 0, "
            << instance_count_ << ")..." << std::endl;
  // === END ===

  cmd->drawIndexed(index_count_, 0, instance_count_);

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "  ✓ drawIndexed() returned" << std::endl;
  std::cerr << "------------------------------\n" << std::endl;
  // === END ===
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
  // === DIAGNOSTIC LOGGING START ===
  std::cerr << "\n" << std::string(70, '#') << std::endl;
  std::cerr << "### RendererInstanced::draw_instanced() CALLED ###"
            << std::endl;
  std::cerr << std::string(70, '#') << std::endl;
  std::cerr << "\nMesh Info:" << std::endl;
  std::cerr << "  instance_count:  " << mesh.instance_count() << std::endl;
  std::cerr << "  index_count:     " << mesh.index_count() << std::endl;
  std::cerr << "  vertex_count:    " << mesh.vertex_count() << std::endl;
  std::cerr << "\nBuffer Handles:" << std::endl;
  std::cerr << "  vertex_buffer:   " << mesh.vertex_buffer().id << std::endl;
  std::cerr << "  index_buffer:    " << mesh.index_buffer().id << std::endl;
  std::cerr << "  instance_buffer: " << mesh.instance_buffer().id << std::endl;

  if (mesh.instance_count() == 0) {
    std::cerr << "\n❌ WARNING: Instance count is ZERO!" << std::endl;
    std::cerr << "   No instances to render." << std::endl;
    std::cerr << std::string(70, '#') << "\n" << std::endl;
    return;
  }
  // === DIAGNOSTIC LOGGING END ===

  Shader *shader = renderer.get_shader(renderer.instanced_shader());
  if (!shader) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "\n❌ FATAL ERROR: Instanced shader is NULL!" << std::endl;
    std::cerr << "   renderer.instanced_shader() returned null" << std::endl;
    std::cerr << std::string(70, '#') << "\n" << std::endl;
    // === END ===
    return;
  }

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "\n✓ Shader retrieved successfully" << std::endl;
  // === END ===

  auto *cmd = renderer.device()->getImmediate();

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "\nMaterial Info:" << std::endl;
  std::cerr << "  blend_mode:     "
            << static_cast<int>(base_material.blend_mode) << std::endl;
  std::cerr << "  depth_test:     " << (base_material.depth_test ? "YES" : "NO")
            << std::endl;
  std::cerr << "  depth_write:    "
            << (base_material.depth_write ? "YES" : "NO") << std::endl;
  // === END ===

  auto pipeline_handle =
      shader->pipeline(base_material.shader_variant, base_material.blend_mode);

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "\nPipeline:" << std::endl;
  std::cerr << "  pipeline.id:    " << pipeline_handle.id << std::endl;
  if (pipeline_handle.id == 0) {
    std::cerr << "  ❌ ERROR: Pipeline ID is 0 (invalid)!" << std::endl;
    std::cerr << std::string(70, '#') << "\n" << std::endl;
    return;
  }
  std::cerr << "  ✓ Valid pipeline handle" << std::endl;
  // === END ===

  cmd->setPipeline(pipeline_handle);

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "  ✓ Pipeline set" << std::endl;
  // === END ===

  renderer.apply_material_state(cmd, base_material);

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "  ✓ Material state applied" << std::endl;
  // === END ===

  // Build identity model matrix (instances handle their own transforms)
  glm::mat4 model = glm::mat4(1.0f);

  // Set model matrix
  const ShaderReflection &reflection =
      shader->reflection(base_material.shader_variant);

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "\nUniforms:" << std::endl;
  std::cerr << "  Has 'model' uniform:       "
            << (reflection.has_uniform("model") ? "YES" : "NO") << std::endl;
  std::cerr << "  Has 'normalMatrix' uniform:"
            << (reflection.has_uniform("normalMatrix") ? "YES" : "NO")
            << std::endl;
  // === END ===

  if (reflection.has_uniform("model")) {
    cmd->setUniformMat4("model", glm::value_ptr(model));
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ✓ Model matrix set (identity)" << std::endl;
    // === END ===
  }

  // Calculate and set normal matrix (identity in this case, but required by
  // shader)
  glm::mat4 normalMatrix = glm::transpose(glm::inverse(model));
  if (reflection.has_uniform("normalMatrix")) {
    cmd->setUniformMat4("normalMatrix", glm::value_ptr(normalMatrix));
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ✓ Normal matrix set (identity)" << std::endl;
    // === END ===
  }

  // Draw the instanced mesh
  // === DIAGNOSTIC LOGGING ===
  std::cerr << "\nCalling mesh.draw()..." << std::endl;
  std::cerr << "  This will call:" << std::endl;
  std::cerr << "    - setVertexBuffer()" << std::endl;
  std::cerr << "    - setIndexBuffer()" << std::endl;
  std::cerr << "    - setInstanceBuffer()" << std::endl;
  std::cerr << "    - drawIndexed()" << std::endl;
  std::cerr << std::string(70, '-') << std::endl;
  // === END ===

  mesh.draw(cmd);

  // === DIAGNOSTIC LOGGING ===
  std::cerr << std::string(70, '-') << std::endl;
  std::cerr << "✓ mesh.draw() returned" << std::endl;
  std::cerr << std::string(70, '#') << "\n" << std::endl;
  // === END ===
}

} // namespace pixel::renderer3d
