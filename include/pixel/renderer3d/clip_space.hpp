#pragma once

#include "pixel/rhi/rhi.hpp"
#include <glm/glm.hpp>

namespace pixel::renderer3d {

/**
 * @brief Returns a matrix that converts OpenGL-style clip coordinates to the
 *        coordinate system expected by the active graphics backend.
 *
 * Metal (used on arm64 macOS) expects clip space with Y-down and depth in the
 * [0, 1] range, whereas the renderer produces matrices using OpenGL
 * conventions. The returned matrix performs the required Y flip and depth
 * remapping when the backend exposes Caps::clipSpaceYDown.
 */
glm::mat4 clip_space_correction_matrix(const rhi::Caps &caps);

/**
 * @brief Applies clip-space correction to the given matrix when required by
 *        the active graphics backend.
 */
glm::mat4 apply_clip_space_correction(const glm::mat4 &matrix,
                                       const rhi::Caps &caps);

} // namespace pixel::renderer3d
