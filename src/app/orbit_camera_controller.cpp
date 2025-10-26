#include "pixel/app/orbit_camera_controller.hpp"

#include "pixel/math/vec3.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pixel::app {

namespace {
float distance_to_target(const renderer3d::Camera &camera) {
  const renderer3d::Vec3 delta = camera.target - camera.position;
  return delta.length();
}
}

OrbitCameraController::OrbitCameraController(renderer3d::Camera &camera,
                                             input::InputManager &input_manager)
    : camera_(&camera), input_manager_(input_manager) {}

void OrbitCameraController::set_camera(renderer3d::Camera *camera) {
  camera_ = camera;
}

void OrbitCameraController::set_zoom_limits(float min_distance,
                                            float max_distance) {
  min_distance_ = std::max(0.0f, min_distance);
  max_distance_ = std::max(min_distance_, max_distance);
}

void OrbitCameraController::update(float delta_time) {
  if (!enabled_ || !camera_) {
    return;
  }

  const bool orbit_active = input_manager_.mouse_down(GLFW_MOUSE_BUTTON_LEFT);
  if (orbit_active) {
    const math::Vec2 delta = input_manager_.mouse_delta();
    const float yaw = -delta.x * orbit_sensitivity_;
    const float pitch = -delta.y * orbit_sensitivity_;
    camera_->orbit(yaw, pitch);
  }

  float pan_axis = 0.0f;
  if (input_manager_.key_down(GLFW_KEY_A)) {
    pan_axis -= 1.0f;
  }
  if (input_manager_.key_down(GLFW_KEY_D)) {
    pan_axis += 1.0f;
  }

  if (std::abs(pan_axis) > std::numeric_limits<float>::epsilon()) {
    const float pan_amount = pan_axis * pan_speed_ * delta_time;
    camera_->pan(pan_amount, 0.0f);
  }

  float zoom_axis = 0.0f;
  if (input_manager_.key_down(GLFW_KEY_W)) {
    zoom_axis += 1.0f;
  }
  if (input_manager_.key_down(GLFW_KEY_S)) {
    zoom_axis -= 1.0f;
  }

  if (std::abs(zoom_axis) > std::numeric_limits<float>::epsilon()) {
    const float current_distance = distance_to_target(*camera_);
    const float zoom_delta = zoom_axis * zoom_speed_ * delta_time;

    if (zoom_delta > 0.0f) {
      const float max_in = std::max(0.0f, current_distance - min_distance_);
      camera_->zoom(std::min(zoom_delta, max_in));
    } else {
      const float max_out = std::max(0.0f, max_distance_ - current_distance);
      camera_->zoom(std::max(zoom_delta, -max_out));
    }
  }
}

} // namespace pixel::app
