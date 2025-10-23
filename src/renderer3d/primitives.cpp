#include "pixel/renderer3d/primitives.hpp"

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
