#pragma once
#include <cstdint>
namespace pixel::rhi {
struct BufferHandle {
  uint32_t id{0};
};
struct TextureHandle {
  uint32_t id{0};
};
struct SamplerHandle {
  uint32_t id{0};
};
struct ShaderHandle {
  uint32_t id{0};
};
struct PipelineHandle {
  uint32_t id{0};
};
struct FramebufferHandle {
  uint32_t id{0};
};
} // namespace pixel::rhi
