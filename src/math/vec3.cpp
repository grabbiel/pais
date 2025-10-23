#include "pixel/math/vec3.hpp"
#include <cmath>

namespace pixel::math {

glm::vec3 Vec3::to_glm() const {
  return glm::vec3(x, y, z);
}

Vec3 Vec3::operator-(const Vec3& other) const {
  return {x - other.x, y - other.y, z - other.z};
}

Vec3 Vec3::operator+(const Vec3& other) const {
  return {x + other.x, y + other.y, z + other.z};
}

Vec3 Vec3::normalized() const {
  float len = length();
  if (len > 0) {
    return {x / len, y / len, z / len};
  }
  return {0, 0, 0};
}

Vec3 Vec3::operator*(float scalar) const {
  return {x * scalar, y * scalar, z * scalar};
}

Vec3 operator*(float scalar, const Vec3& v) {
  return {v.x * scalar, v.y * scalar, v.z * scalar};
}

float Vec3::length() const {
  return std::sqrt(x * x + y * y + z * z);
}

} // namespace pixel::math
