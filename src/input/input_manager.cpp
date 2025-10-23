#include "pixel/input/input_manager.hpp"
#include "pixel/platform/window.hpp"
#include <GLFW/glfw3.h>
#include <cstring>

namespace pixel::input {

InputManager::InputManager(platform::Window* window)
    : window_(window) {
  // Initialize mouse position
  if (window_) {
    GLFWwindow* glfw_window = window_->native_handle();
    glfwGetCursorPos(glfw_window, &last_mouse_x_, &last_mouse_y_);
    state_.mouse_x = last_mouse_x_;
    state_.mouse_y = last_mouse_y_;
    state_.prev_mouse_x = last_mouse_x_;
    state_.prev_mouse_y = last_mouse_y_;
  }
}

void InputManager::update() {
  if (!window_) {
    return;
  }

  GLFWwindow* glfw_window = window_->native_handle();

  // Copy current state to previous state
  std::memcpy(state_.prev_keys, state_.keys, sizeof(state_.keys));
  std::memcpy(state_.prev_mouse_buttons, state_.mouse_buttons,
              sizeof(state_.mouse_buttons));
  state_.prev_mouse_x = state_.mouse_x;
  state_.prev_mouse_y = state_.mouse_y;

  // Read current keyboard state
  for (int key = 32; key <= 348; ++key) {
    state_.keys[key] = (glfwGetKey(glfw_window, key) == GLFW_PRESS);
  }

  // Read current mouse button state
  for (int btn = 0; btn < 8; ++btn) {
    state_.mouse_buttons[btn] =
        (glfwGetMouseButton(glfw_window, btn) == GLFW_PRESS);
  }

  // Read current mouse position and calculate delta
  double x, y;
  glfwGetCursorPos(glfw_window, &x, &y);
  state_.mouse_delta_x = x - last_mouse_x_;
  state_.mouse_delta_y = y - last_mouse_y_;
  state_.mouse_x = x;
  state_.mouse_y = y;
  last_mouse_x_ = x;
  last_mouse_y_ = y;

  // Reset scroll delta (should be set by scroll callback if needed)
  state_.scroll_delta = 0.0;
}

bool InputManager::key_pressed(int key) const {
  return state_.key_pressed(key);
}

bool InputManager::key_down(int key) const {
  return state_.key_down(key);
}

bool InputManager::key_released(int key) const {
  return state_.key_released(key);
}

bool InputManager::mouse_pressed(int button) const {
  return state_.mouse_pressed(button);
}

bool InputManager::mouse_down(int button) const {
  return state_.mouse_down(button);
}

bool InputManager::mouse_released(int button) const {
  return state_.mouse_released(button);
}

math::Vec2 InputManager::mouse_position() const {
  return math::Vec2(static_cast<float>(state_.mouse_x),
                    static_cast<float>(state_.mouse_y));
}

math::Vec2 InputManager::mouse_delta() const {
  return math::Vec2(static_cast<float>(state_.mouse_delta_x),
                    static_cast<float>(state_.mouse_delta_y));
}

float InputManager::scroll_delta() const {
  return static_cast<float>(state_.scroll_delta);
}

} // namespace pixel::input
