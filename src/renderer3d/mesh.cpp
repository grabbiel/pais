
#include "pixel/renderer3d/renderer.hpp"
// ============================================================================
// Mesh Implementation
// ============================================================================

namespace pixel::renderer3d {
std::unique_ptr<Mesh> Mesh::create(const std::vector<Vertex> &vertices,
                                   const std::vector<uint32_t> &indices) {
  auto mesh = std::unique_ptr<Mesh>(new Mesh());
  mesh->vertex_count_ = vertices.size();
  mesh->index_count_ = indices.size();

  mesh->vertices_ = vertices;
  mesh->indices_ = indices;

  glGenVertexArrays(1, &mesh->vao_);
  glGenBuffers(1, &mesh->vbo_);
  glGenBuffers(1, &mesh->ebo_);

  glBindVertexArray(mesh->vao_);

  glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
               indices.data(), GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, position));

  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, normal));

  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, texcoord));

  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, color));

  glBindVertexArray(0);

  return mesh;
}

Mesh::~Mesh() {
  if (vao_)
    glDeleteVertexArrays(1, &vao_);
  if (vbo_)
    glDeleteBuffers(1, &vbo_);
  if (ebo_)
    glDeleteBuffers(1, &ebo_);
}

void Mesh::draw() const {
  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
}
} // namespace pixel::renderer3d
