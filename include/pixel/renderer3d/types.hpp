#pragma once

#include "pixel/math/math.hpp"

namespace pixel::renderer3d {

using Vec2 = pixel::math::Vec2;
using Vec3 = pixel::math::Vec3;
using Vec4 = pixel::math::Vec4;
using Color = pixel::math::Color;

struct Vertex {
  Vec3 position;
  Vec3 normal;
  Vec2 texcoord;
  Color color;
};

} // namespace pixel::renderer3d
