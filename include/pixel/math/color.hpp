#pragma once

namespace pixel::math {

struct Color {
  float r, g, b, a;

  Color() : r(1), g(1), b(1), a(1) {}
  Color(float r_, float g_, float b_, float a_ = 1.0f)
      : r(r_), g(g_), b(b_), a(a_) {}

  // Common color constants
  static Color White() { return {1, 1, 1, 1}; }
  static Color Black() { return {0, 0, 0, 1}; }
  static Color Red() { return {1, 0, 0, 1}; }
  static Color Green() { return {0, 1, 0, 1}; }
  static Color Blue() { return {0, 0, 1, 1}; }
  static Color Yellow() { return {1, 1, 0, 1}; }
  static Color Cyan() { return {0, 1, 1, 1}; }
  static Color Magenta() { return {1, 0, 1, 1}; }

  // Utility methods
  Color operator*(float scalar) const {
    return {r * scalar, g * scalar, b * scalar, a};
  }

  Color operator+(const Color& other) const {
    return {r + other.r, g + other.g, b + other.b, a + other.a};
  }
};

} // namespace pixel::math
