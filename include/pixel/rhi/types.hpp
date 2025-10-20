#pragma once
#include <cstdint>
#include <cstddef>
namespace pixel::rhi {

enum class Format {
  Unknown,
  RGBA8,
  BGRA8,
  R8,
  R16F,
  RG16F,
  RGBA16F,
  D24S8,
  D32F
};
struct Extent2D {
  uint32_t w{0}, h{0};
};

enum class BufferUsage : uint32_t {
  Vertex = 1,
  Index = 2,
  Uniform = 4,
  TransferSrc = 8,
  TransferDst = 16
};
inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
  return BufferUsage(uint32_t(a) | uint32_t(b));
}

struct BufferDesc {
  size_t size;
  BufferUsage usage;
  bool hostVisible{false};
};
struct TextureDesc {
  Extent2D size;
  Format format;
  uint32_t mipLevels{1};
  bool renderTarget{false};
};
struct SamplerDesc {
  bool linear{true};
  bool repeat{true};
  bool aniso{false};
};
} // namespace pixel::rhi
