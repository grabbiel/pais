#pragma once
#include "pixel/math/vec2.hpp"

struct GLFWwindow;

namespace pixel::input {

// ============================================================================
// InputState - Stores current and previous frame input state
// ============================================================================

struct InputState {
  bool keys[512] = {false};
  bool prev_keys[512] = {false};
  bool mouse_buttons[8] = {false};
  bool prev_mouse_buttons[8] = {false};
  double mouse_x = 0.0;
  double mouse_y = 0.0;
  double prev_mouse_x = 0.0;
  double prev_mouse_y = 0.0;
  double mouse_delta_x = 0.0;
  double mouse_delta_y = 0.0;
  double scroll_delta = 0.0;

  // Keyboard queries
  bool key_pressed(int key) const {
    return key >= 0 && key < 512 && keys[key] && !prev_keys[key];
  }
  bool key_down(int key) const {
    return key >= 0 && key < 512 && keys[key];
  }
  bool key_released(int key) const {
    return key >= 0 && key < 512 && !keys[key] && prev_keys[key];
  }

  // Mouse button queries
  bool mouse_pressed(int button) const {
    return button >= 0 && button < 8 && mouse_buttons[button] &&
           !prev_mouse_buttons[button];
  }
  bool mouse_down(int button) const {
    return button >= 0 && button < 8 && mouse_buttons[button];
  }
  bool mouse_released(int button) const {
    return button >= 0 && button < 8 && !mouse_buttons[button] &&
           prev_mouse_buttons[button];
  }

  // Convenience accessors
  math::Vec2 mouse_position() const {
    return {static_cast<float>(mouse_x), static_cast<float>(mouse_y)};
  }
  math::Vec2 mouse_delta() const {
    return {static_cast<float>(mouse_delta_x), static_cast<float>(mouse_delta_y)};
  }
};

// ============================================================================
// InputManager - Manages input state and GLFW polling
// ============================================================================

class InputManager {
public:
  explicit InputManager(GLFWwindow* window);
  ~InputManager() = default;

  // Update input state - call once per frame before processing input
  void update();

  // Access current input state
  const InputState& state() const { return state_; }

  // Convenience accessors (delegates to InputState)
  bool key_pressed(int key) const { return state_.key_pressed(key); }
  bool key_down(int key) const { return state_.key_down(key); }
  bool key_released(int key) const { return state_.key_released(key); }

  bool mouse_pressed(int button) const { return state_.mouse_pressed(button); }
  bool mouse_down(int button) const { return state_.mouse_down(button); }
  bool mouse_released(int button) const { return state_.mouse_released(button); }

  math::Vec2 mouse_position() const { return state_.mouse_position(); }
  math::Vec2 mouse_delta() const { return state_.mouse_delta(); }
  double scroll_delta() const { return state_.scroll_delta; }

private:
  GLFWwindow* window_;
  InputState state_;
  double last_mouse_x_ = 0.0;
  double last_mouse_y_ = 0.0;
};

} // namespace pixel::input
