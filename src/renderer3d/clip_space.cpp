#include "pixel/renderer3d/clip_space.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace pixel::renderer3d {

glm::mat4 clip_space_correction_matrix(const rhi::Caps &caps) {
  glm::mat4 correction(1.0f);
  if (caps.clipSpaceYDown) {
    correction[1][1] = -1.0f;
    correction[2][2] = 0.5f;
    correction[3][2] = 0.5f;
  }
  return correction;
}

glm::mat4 apply_clip_space_correction(const glm::mat4 &matrix,
                                       const rhi::Caps &caps) {
  if (!caps.clipSpaceYDown) {
    return matrix;
  }
  return clip_space_correction_matrix(caps) * matrix;
}

} // namespace pixel::renderer3d
