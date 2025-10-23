#pragma once

#include "pixel/renderer3d/types.hpp"
#include <vector>

namespace pixel::renderer3d::primitives {

std::vector<Vertex> create_quad_vertices(float size);
std::vector<Vertex> create_cube_vertices(float size);
std::vector<Vertex> create_plane_vertices(float width, float depth,
                                          int segments);

} // namespace pixel::renderer3d::primitives
