#pragma once
#include <glm/glm.hpp>

namespace pixel::math {

struct Vec3 {
  float x, y, z;

  Vec3() : x(0), y(0), z(0) {}
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  Vec3(const glm::vec3& v) : x(v.x), y(v.y), z(v.z) {}

  // GLM conversion
  glm::vec3 to_glm() const;
  static Vec3 from_glm(const glm::vec3& v) { return {v.x, v.y, v.z}; }

  // Arithmetic operators
  Vec3 operator-(const Vec3& other) const;
  Vec3 operator+(const Vec3& other) const;
  Vec3 operator*(float scalar) const;
  friend Vec3 operator*(float scalar, const Vec3& v);

  // Utility methods
  float length() const;
  Vec3 normalized() const;
};

} // namespace pixel::math
