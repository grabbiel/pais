#include "pixel/math/vec2.hpp"
#include <cmath>

namespace pixel::math {

float Vec2::length() const {
  return std::sqrt(x * x + y * y);
}

Vec2 Vec2::normalized() const {
  float len = length();
  if (len > 0) {
    return {x / len, y / len};
  }
  return {0, 0};
}

} // namespace pixel::math
