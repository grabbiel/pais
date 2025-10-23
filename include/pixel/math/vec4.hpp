#pragma once

namespace pixel::math {

struct Vec4 {
  float x, y, z, w;

  Vec4() : x(0), y(0), z(0), w(1) {}
  Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

  // Arithmetic operators
  Vec4 operator+(const Vec4& other) const {
    return {x + other.x, y + other.y, z + other.z, w + other.w};
  }

  Vec4 operator-(const Vec4& other) const {
    return {x - other.x, y - other.y, z - other.z, w - other.w};
  }

  Vec4 operator*(float scalar) const {
    return {x * scalar, y * scalar, z * scalar, w * scalar};
  }

  friend Vec4 operator*(float scalar, const Vec4& v) {
    return {v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar};
  }
};

} // namespace pixel::math
