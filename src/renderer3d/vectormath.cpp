#include "pixel/renderer3d/renderer.hpp"

// ============================================================================
// Vector Arithmetic
// ============================================================================

namespace pixel::renderer3d {

glm::vec3 Vec3::to_glm() const { return glm::vec3(x, y, z); }

Vec3 Vec3::operator-(const Vec3 &other) const {
  return {x - other.x, y - other.y, z - other.z};
}

Vec3 Vec3::operator+(const Vec3 &other) const {
  return {x + other.x, y + other.y, z + other.z};
}

Vec3 Vec3::normalized() const {
  float length = std::sqrt(x * x + y * y + z * z);
  if (length > 0) {
    return {x / length, y / length, z / length};
  }
  return {0, 0, 0};
}

Vec3 Vec3::operator*(float scalar) const {
  return {x * scalar, y * scalar, z * scalar};
}

// Scalar multiplication (float * Vec3)
Vec3 operator*(float scalar, const Vec3 &v) {
  return {v.x * scalar, v.y * scalar, v.z * scalar};
}

float Vec3::length() const { return std::sqrt(x * x + y * y + z * z); }

} // namespace pixel::renderer3d
