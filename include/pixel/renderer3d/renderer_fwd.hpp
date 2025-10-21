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

} // namespace pixel::rhi
