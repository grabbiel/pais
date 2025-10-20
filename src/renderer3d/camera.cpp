#include "pixel/renderer3d/renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
// ============================================================================
// Camera Implementation
// ============================================================================

namespace pixel::renderer3d {
void Camera::get_view_matrix(float *out_mat4) const {
  glm::vec3 pos(position.x, position.y, position.z);
  glm::vec3 tgt(target.x, target.y, target.z);
  glm::vec3 u(up.x, up.y, up.z);

  glm::mat4 view = glm::lookAt(pos, tgt, u);
  memcpy(out_mat4, glm::value_ptr(view), 16 * sizeof(float));
}

void Camera::get_projection_matrix(float *out_mat4, int w, int h) const {
  float aspect = static_cast<float>(w) / static_cast<float>(h);
  glm::mat4 proj;

  if (mode == ProjectionMode::Perspective) {
    proj = glm::perspective(glm::radians(fov), aspect, near_clip, far_clip);
  } else {
    float half_w = ortho_size * aspect;
    float half_h = ortho_size;
    proj = glm::ortho(-half_w, half_w, -half_h, half_h, near_clip, far_clip);
  }

  memcpy(out_mat4, glm::value_ptr(proj), 16 * sizeof(float));
}

void Camera::orbit(float dx, float dy) {
  glm::vec3 dir = glm::normalize(glm::vec3(
      position.x - target.x, position.y - target.y, position.z - target.z));
  float radius = glm::length(glm::vec3(
      position.x - target.x, position.y - target.y, position.z - target.z));

  float theta = atan2(dir.z, dir.x) + dx * 0.01f;
  float phi = asin(dir.y) + dy * 0.01f;
  phi = glm::clamp(phi, -1.5f, 1.5f);

  position.x = target.x + radius * cos(phi) * cos(theta);
  position.y = target.y + radius * sin(phi);
  position.z = target.z + radius * cos(phi) * sin(theta);
}

void Camera::pan(float dx, float dy) {
  glm::vec3 pos(position.x, position.y, position.z);
  glm::vec3 tgt(target.x, target.y, target.z);
  glm::vec3 u(up.x, up.y, up.z);

  // Calculate camera's right and up vectors
  glm::vec3 forward = glm::normalize(tgt - pos);
  glm::vec3 right = glm::normalize(glm::cross(forward, u));
  glm::vec3 cam_up = glm::normalize(glm::cross(right, forward));

  // Pan both position and target to maintain view direction
  glm::vec3 pan_offset = right * dx + cam_up * dy;

  position.x += pan_offset.x;
  position.y += pan_offset.y;
  position.z += pan_offset.z;

  target.x += pan_offset.x;
  target.y += pan_offset.y;
  target.z += pan_offset.z;
}

void Camera::zoom(float delta) {
  glm::vec3 dir = glm::normalize(glm::vec3(
      target.x - position.x, target.y - position.y, target.z - position.z));
  position.x += dir.x * delta;
  position.y += dir.y * delta;
  position.z += dir.z * delta;
}
} // namespace pixel::renderer3d
