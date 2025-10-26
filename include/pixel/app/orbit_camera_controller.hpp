#pragma once

#include "pixel/input/input_manager.hpp"
#include "pixel/renderer3d/renderer.hpp"

namespace pixel::app {

class OrbitCameraController {
public:
  OrbitCameraController(renderer3d::Camera &camera,
                        input::InputManager &input_manager);

  void set_camera(renderer3d::Camera *camera);
  renderer3d::Camera *camera() const { return camera_; }

  void set_enabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }

  void set_zoom_limits(float min_distance, float max_distance);

  void update(float delta_time);

private:
  renderer3d::Camera *camera_;
  input::InputManager &input_manager_;
  bool enabled_{true};
  float orbit_sensitivity_{0.25f};
  float pan_speed_{5.0f};
  float zoom_speed_{8.0f};
  float min_distance_{1.0f};
  float max_distance_{75.0f};
};

} // namespace pixel::app
