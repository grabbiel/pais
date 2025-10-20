// include/pixel/renderer3d/renderer_fwd.hpp
#pragma once

// Forward declaration for GLFW to avoid struct/class mismatch
struct GLFWwindow;

namespace pixel::rhi {
class Device;
struct Caps;
struct SwapchainDesc;
struct PipelineDesc;
class CmdList;

// Forward declare factory functions
Device *create_gl_device(GLFWwindow *window);

#ifdef __APPLE__
Device *create_metal_device(GLFWwindow *window);
#endif

} // namespace pixel::rhi
