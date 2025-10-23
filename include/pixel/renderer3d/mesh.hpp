#pragma once

#include "pixel/renderer3d/types.hpp"
#include "pixel/rhi/rhi.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace pixel::renderer3d {

class Mesh {
public:
  static std::unique_ptr<Mesh> create(rhi::Device *device,
                                      const std::vector<Vertex> &vertices,
                                      const std::vector<uint32_t> &indices);
  ~Mesh();

  rhi::BufferHandle vertex_buffer() const { return vertex_buffer_; }
  rhi::BufferHandle index_buffer() const { return index_buffer_; }

  size_t vertex_count() const { return vertex_count_; }
  size_t index_count() const { return index_count_; }

  const std::vector<Vertex> &vertices() const { return vertices_; }
  const std::vector<uint32_t> &indices() const { return indices_; }

private:
  Mesh() = default;

  rhi::BufferHandle vertex_buffer_{0};
  rhi::BufferHandle index_buffer_{0};
  size_t vertex_count_ = 0;
  size_t index_count_ = 0;

  std::vector<Vertex> vertices_;
  std::vector<uint32_t> indices_;
};

} // namespace pixel::renderer3d
