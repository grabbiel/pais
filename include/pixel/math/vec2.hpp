#pragma once

namespace pixel::math {

struct Vec2 {
  float x, y;

  Vec2() : x(0), y(0) {}
  Vec2(float x_, float y_) : x(x_), y(y_) {}

  // Arithmetic operators
  Vec2 operator+(const Vec2& other) const {
    return {x + other.x, y + other.y};
  }

  Vec2 operator-(const Vec2& other) const {
    return {x - other.x, y - other.y};
  }

  Vec2 operator*(float scalar) const {
    return {x * scalar, y * scalar};
  }

  friend Vec2 operator*(float scalar, const Vec2& v) {
    return {v.x * scalar, v.y * scalar};
  }

  // Utility methods
  float length() const;
  Vec2 normalized() const;
};

} // namespace pixel::math
