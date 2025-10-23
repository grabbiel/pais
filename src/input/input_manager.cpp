#include "pixel/input/input_manager.hpp"
#include <GLFW/glfw3.h>
#include <cstring>

namespace pixel::input {

InputManager::InputManager(GLFWwindow* window)
    : window_(window) {
  // Initialize mouse position
  double x, y;
  glfwGetCursorPos(window_, &x, &y);
  last_mouse_x_ = x;
  last_mouse_y_ = y;
  state_.mouse_x = x;
  state_.mouse_y = y;
}

void InputManager::update() {
  // Copy current state to previous state
  std::memcpy(state_.prev_keys, state_.keys, sizeof(state_.keys));
  std::memcpy(state_.prev_mouse_buttons, state_.mouse_buttons,
              sizeof(state_.mouse_buttons));
  state_.prev_mouse_x = state_.mouse_x;
  state_.prev_mouse_y = state_.mouse_y;

  // Poll keyboard state
  // GLFW key codes range from 32 (space) to 348 (last key)
  for (int key = 32; key <= 348; ++key) {
    state_.keys[key] = (glfwGetKey(window_, key) == GLFW_PRESS);
  }

  // Poll mouse button state
  for (int btn = 0; btn < 8; ++btn) {
    state_.mouse_buttons[btn] = (glfwGetMouseButton(window_, btn) == GLFW_PRESS);
  }

  // Update mouse position and delta
  double x, y;
  glfwGetCursorPos(window_, &x, &y);
  state_.mouse_delta_x = x - last_mouse_x_;
  state_.mouse_delta_y = y - last_mouse_y_;
  state_.mouse_x = x;
  state_.mouse_y = y;
  last_mouse_x_ = x;
  last_mouse_y_ = y;

  // Reset scroll delta (must be set by scroll callback)
  state_.scroll_delta = 0.0;
}

} // namespace pixel::input
