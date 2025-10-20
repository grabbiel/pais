#include "pixel/renderer3d/renderer.hpp"
#include <cstring>

namespace pixel::renderer3d {

std::unique_ptr<Mesh> Mesh::create(rhi::Device *device,
                                   const std::vector<Vertex> &vertices,
                                   const std::vector<uint32_t> &indices) {
  auto mesh = std::unique_ptr<Mesh>(new Mesh());
  mesh->vertex_count_ = vertices.size();
  mesh->index_count_ = indices.size();
  mesh->vertices_ = vertices;
  mesh->indices_ = indices;

  // Create vertex buffer
  rhi::BufferDesc vb_desc;
  vb_desc.size = vertices.size() * sizeof(Vertex);
  vb_desc.usage = rhi::BufferUsage::Vertex;
  vb_desc.hostVisible = true;
  mesh->vertex_buffer_ = device->createBuffer(vb_desc);

  // Upload vertex data
  auto *cmd = device->getImmediate();
  cmd->begin();
  cmd->copyToBuffer(mesh->vertex_buffer_, 0,
                    std::span<const std::byte>(
                        reinterpret_cast<const std::byte *>(vertices.data()),
                        vertices.size() * sizeof(Vertex)));
  cmd->end();

  // Create index buffer
  rhi::BufferDesc ib_desc;
  ib_desc.size = indices.size() * sizeof(uint32_t);
  ib_desc.usage = rhi::BufferUsage::Index;
  ib_desc.hostVisible = true;
  mesh->index_buffer_ = device->createBuffer(ib_desc);

  // Upload index data
  cmd->begin();
  cmd->copyToBuffer(mesh->index_buffer_, 0,
                    std::span<const std::byte>(
                        reinterpret_cast<const std::byte *>(indices.data()),
                        indices.size() * sizeof(uint32_t)));
  cmd->end();

  return mesh;
}

Mesh::~Mesh() {
  // RHI device will handle cleanup through handle management
}

} // namespace pixel::renderer3d
