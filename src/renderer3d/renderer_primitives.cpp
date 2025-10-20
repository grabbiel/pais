// src/renderer3d/renderer_primitives.cpp
#include "pixel/renderer3d/renderer.hpp"
#include <cmath>

namespace pixel::renderer3d::primitives {

std::vector<Vertex> create_quad_vertices(float size) {
  float h = size / 2.0f;

  return {{{-h, -h, 0}, {0, 0, 1}, {0, 0}, Color::White()},
          {{h, -h, 0}, {0, 0, 1}, {1, 0}, Color::White()},
          {{h, h, 0}, {0, 0, 1}, {1, 1}, Color::White()},
          {{-h, h, 0}, {0, 0, 1}, {0, 1}, Color::White()}};
}

std::vector<Vertex> create_cube_vertices(float size) {
  float h = size / 2.0f;

  std::vector<Vertex> vertices;

  // Front face
  vertices.push_back({{-h, -h, h}, {0, 0, 1}, {0, 0}, Color::White()});
  vertices.push_back({{h, -h, h}, {0, 0, 1}, {1, 0}, Color::White()});
  vertices.push_back({{h, h, h}, {0, 0, 1}, {1, 1}, Color::White()});
  vertices.push_back({{h, h, h}, {0, 0, 1}, {1, 1}, Color::White()});
  vertices.push_back({{-h, h, h}, {0, 0, 1}, {0, 1}, Color::White()});
  vertices.push_back({{-h, -h, h}, {0, 0, 1}, {0, 0}, Color::White()});

  // Back face
  vertices.push_back({{h, -h, -h}, {0, 0, -1}, {0, 0}, Color::White()});
  vertices.push_back({{-h, -h, -h}, {0, 0, -1}, {1, 0}, Color::White()});
  vertices.push_back({{-h, h, -h}, {0, 0, -1}, {1, 1}, Color::White()});
  vertices.push_back({{-h, h, -h}, {0, 0, -1}, {1, 1}, Color::White()});
  vertices.push_back({{h, h, -h}, {0, 0, -1}, {0, 1}, Color::White()});
  vertices.push_back({{h, -h, -h}, {0, 0, -1}, {0, 0}, Color::White()});

  // Left face
  vertices.push_back({{-h, -h, -h}, {-1, 0, 0}, {0, 0}, Color::White()});
  vertices.push_back({{-h, -h, h}, {-1, 0, 0}, {1, 0}, Color::White()});
  vertices.push_back({{-h, h, h}, {-1, 0, 0}, {1, 1}, Color::White()});
  vertices.push_back({{-h, h, h}, {-1, 0, 0}, {1, 1}, Color::White()});
  vertices.push_back({{-h, h, -h}, {-1, 0, 0}, {0, 1}, Color::White()});
  vertices.push_back({{-h, -h, -h}, {-1, 0, 0}, {0, 0}, Color::White()});

  // Right face
  vertices.push_back({{h, -h, h}, {1, 0, 0}, {0, 0}, Color::White()});
  vertices.push_back({{h, -h, -h}, {1, 0, 0}, {1, 0}, Color::White()});
  vertices.push_back({{h, h, -h}, {1, 0, 0}, {1, 1}, Color::White()});
  vertices.push_back({{h, h, -h}, {1, 0, 0}, {1, 1}, Color::White()});
  vertices.push_back({{h, h, h}, {1, 0, 0}, {0, 1}, Color::White()});
  vertices.push_back({{h, -h, h}, {1, 0, 0}, {0, 0}, Color::White()});

  // Top face
  vertices.push_back({{-h, h, h}, {0, 1, 0}, {0, 0}, Color::White()});
  vertices.push_back({{h, h, h}, {0, 1, 0}, {1, 0}, Color::White()});
  vertices.push_back({{h, h, -h}, {0, 1, 0}, {1, 1}, Color::White()});
  vertices.push_back({{h, h, -h}, {0, 1, 0}, {1, 1}, Color::White()});
  vertices.push_back({{-h, h, -h}, {0, 1, 0}, {0, 1}, Color::White()});
  vertices.push_back({{-h, h, h}, {0, 1, 0}, {0, 0}, Color::White()});

  // Bottom face
  vertices.push_back({{-h, -h, -h}, {0, -1, 0}, {0, 0}, Color::White()});
  vertices.push_back({{h, -h, -h}, {0, -1, 0}, {1, 0}, Color::White()});
  vertices.push_back({{h, -h, h}, {0, -1, 0}, {1, 1}, Color::White()});
  vertices.push_back({{h, -h, h}, {0, -1, 0}, {1, 1}, Color::White()});
  vertices.push_back({{-h, -h, h}, {0, -1, 0}, {0, 1}, Color::White()});
  vertices.push_back({{-h, -h, -h}, {0, -1, 0}, {0, 0}, Color::White()});

  return vertices;
}

std::vector<Vertex> create_plane_vertices(float width, float depth,
                                          int segments) {
  std::vector<Vertex> vertices;

  float hw = width / 2.0f;
  float hd = depth / 2.0f;

  for (int z = 0; z <= segments; ++z) {
    for (int x = 0; x <= segments; ++x) {
      float px = -hw + (x / float(segments)) * width;
      float pz = -hd + (z / float(segments)) * depth;
      float u = x / float(segments);
      float v = z / float(segments);

      vertices.push_back({{px, 0, pz}, {0, 1, 0}, {u, v}, Color::White()});
    }
  }

  // Generate indices for triangles
  std::vector<Vertex> indexed_verts;
  for (int z = 0; z < segments; ++z) {
    for (int x = 0; x < segments; ++x) {
      int i0 = z * (segments + 1) + x;
      int i1 = i0 + 1;
      int i2 = (z + 1) * (segments + 1) + x;
      int i3 = i2 + 1;

      // First triangle
      indexed_verts.push_back(vertices[i0]);
      indexed_verts.push_back(vertices[i2]);
      indexed_verts.push_back(vertices[i1]);

      // Second triangle
      indexed_verts.push_back(vertices[i1]);
      indexed_verts.push_back(vertices[i2]);
      indexed_verts.push_back(vertices[i3]);
    }
  }

  return indexed_verts;
}

} // namespace pixel::renderer3d::primitives

namespace pixel::renderer3d {

std::unique_ptr<Mesh> Renderer::create_quad(float size) {
  std::vector<Vertex> verts = primitives::create_quad_vertices(size);
  std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
  return Mesh::create(device_, verts, indices);
}

std::unique_ptr<Mesh> Renderer::create_sprite_quad() {
  return create_quad(1.0f);
}

std::unique_ptr<Mesh> Renderer::create_cube(float size) {
  std::vector<Vertex> verts = primitives::create_cube_vertices(size);
  std::vector<uint32_t> indices;
  for (uint32_t i = 0; i < verts.size(); ++i)
    indices.push_back(i);
  return Mesh::create(device_, verts, indices);
}

std::unique_ptr<Mesh> Renderer::create_plane(float width, float depth,
                                             int segments) {
  std::vector<Vertex> verts =
      primitives::create_plane_vertices(width, depth, segments);
  std::vector<uint32_t> indices;
  for (uint32_t i = 0; i < verts.size(); ++i)
    indices.push_back(i);
  return Mesh::create(device_, verts, indices);
}

void Renderer::draw_mesh(const Mesh &mesh, const Vec3 &position,
                         const Vec3 &rotation, const Vec3 &scale,
                         const Material &material) {
  Shader *shader = get_shader(default_shader_);
  if (!shader)
    return;

  auto *cmd = device_->getImmediate();
  cmd->setPipeline(shader->pipeline());
  cmd->setVertexBuffer(mesh.vertex_buffer());
  cmd->setIndexBuffer(mesh.index_buffer());

  // TODO: Set uniforms via uniform buffers when RHI supports it
  // For now, drawing will work but without proper transformations

  cmd->drawIndexed(mesh.index_count(), 0, 1);
}

void Renderer::draw_sprite(rhi::TextureHandle texture, const Vec3 &position,
                           const Vec2 &size, const Color &tint) {
  Shader *shader = get_shader(sprite_shader_);
  if (!shader)
    return;

  auto *cmd = device_->getImmediate();
  cmd->setPipeline(shader->pipeline());

  if (sprite_mesh_) {
    cmd->setVertexBuffer(sprite_mesh_->vertex_buffer());
    cmd->setIndexBuffer(sprite_mesh_->index_buffer());
    cmd->drawIndexed(sprite_mesh_->index_count(), 0, 1);
  }
}

} // namespace pixel::renderer3d
