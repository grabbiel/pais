#pragma once

#include "pixel/math/math.hpp"

namespace pixel::platform {
  class Window;
}

namespace pixel::input {

// ============================================================================
// Input State
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

  bool key_pressed(int key) const {
    return key >= 0 && key < 512 && keys[key] && !prev_keys[key];
  }
  bool key_down(int key) const { return key >= 0 && key < 512 && keys[key]; }
  bool key_released(int key) const {
    return key >= 0 && key < 512 && !keys[key] && prev_keys[key];
  }

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
};

// ============================================================================
// Input Manager
// ============================================================================

class InputManager {
public:
  explicit InputManager(platform::Window* window);

  // Call once per frame to update input state
  void update();

  // Keyboard
  bool key_pressed(int key) const;
  bool key_down(int key) const;
  bool key_released(int key) const;

  // Mouse buttons
  bool mouse_pressed(int button) const;
  bool mouse_down(int button) const;
  bool mouse_released(int button) const;

  // Mouse position/delta
  math::Vec2 mouse_position() const;
  math::Vec2 mouse_delta() const;
  float scroll_delta() const;

  // Direct access to input state (for backward compatibility)
  const InputState& state() const { return state_; }

private:
  platform::Window* window_;
  InputState state_;
  double last_mouse_x_ = 0.0;
  double last_mouse_y_ = 0.0;
};

} // namespace pixel::input
