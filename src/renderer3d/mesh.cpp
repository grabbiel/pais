#include "pixel/renderer3d/mesh.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace pixel::renderer3d {

std::unique_ptr<Mesh> Mesh::create(rhi::Device *device,
                                   const std::vector<Vertex> &vertices,
                                   const std::vector<uint32_t> &indices) {
  auto mesh = std::unique_ptr<Mesh>(new Mesh());
  mesh->vertex_count_ = vertices.size();
  mesh->index_count_ = indices.size();
  mesh->vertices_ = vertices;
  mesh->indices_ = indices;

  if (!vertices.empty()) {
    Vec3 min_pos = vertices.front().position;
    Vec3 max_pos = vertices.front().position;
    for (const auto &v : vertices) {
      min_pos.x = std::min(min_pos.x, v.position.x);
      min_pos.y = std::min(min_pos.y, v.position.y);
      min_pos.z = std::min(min_pos.z, v.position.z);
      max_pos.x = std::max(max_pos.x, v.position.x);
      max_pos.y = std::max(max_pos.y, v.position.y);
      max_pos.z = std::max(max_pos.z, v.position.z);
    }

    std::cout << "Mesh::create()" << std::endl;
    std::cout << "  vertex_count: " << mesh->vertex_count_ << std::endl;
    std::cout << "  index_count:  " << mesh->index_count_ << std::endl;
    std::cout << "  position bounds: min(" << min_pos.x << ", " << min_pos.y
              << ", " << min_pos.z << ") max(" << max_pos.x << ", "
              << max_pos.y << ", " << max_pos.z << ")" << std::endl;
    const auto &first = vertices.front();
    std::cout << "  first vertex: pos(" << first.position.x << ", "
              << first.position.y << ", " << first.position.z << ") normal(" <<
        first.normal.x << ", " << first.normal.y << ", " << first.normal.z
              << ")" << std::endl;
  } else {
    std::cerr << "Mesh::create() received empty vertex array" << std::endl;
  }

  // Create vertex buffer
  rhi::BufferDesc vb_desc;
  vb_desc.size = vertices.size() * sizeof(Vertex);
  vb_desc.usage = rhi::BufferUsage::Vertex;
  vb_desc.hostVisible = true;
  mesh->vertex_buffer_ = device->createBuffer(vb_desc);
  if (mesh->vertex_buffer_.id == 0) {
    std::cerr << "  ERROR: Failed to allocate vertex buffer" << std::endl;
  } else {
    std::cout << "  Vertex buffer handle: " << mesh->vertex_buffer_.id
              << " size=" << vb_desc.size << std::endl;
  }

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
  if (mesh->index_buffer_.id == 0) {
    std::cerr << "  ERROR: Failed to allocate index buffer" << std::endl;
  } else {
    std::cout << "  Index buffer handle: " << mesh->index_buffer_.id
              << " size=" << ib_desc.size << std::endl;
  }

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
