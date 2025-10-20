#pragma once
#include "handles.hpp"
#include "types.hpp"
#include <span>
#include <string_view>
#include <functional>

struct GLFWwindow;

namespace pixel::rhi {

struct Caps {
  bool instancing{true};
  bool samplerAniso{false};
  bool uniformBuffers{true};
  bool clipSpaceYDown{false}; // GL vs Metal differences if needed
};

struct SwapchainDesc {
  void *nativeWindow{nullptr};
  Extent2D size;
};

struct PipelineDesc {
  ShaderHandle vs, fs;
  // plus: blend, depth, cull, vertex layouts...
};

struct CmdList {
  virtual void begin() = 0;
  virtual void beginRender(TextureHandle rtColor, TextureHandle rtDepth,
                           float clear[4], float clearDepth,
                           uint8_t clearStencil) = 0;
  virtual void setPipeline(PipelineHandle) = 0;
  virtual void setVertexBuffer(BufferHandle, size_t offset = 0) = 0;
  virtual void setIndexBuffer(BufferHandle, size_t offset = 0) = 0;

  virtual void setUniformMat4(const char *name, const float *mat4x4) = 0;
  virtual void setUniformVec3(const char *name, const float *vec3) = 0;
  virtual void setUniformVec4(const char *name,
                              const float *vec4) = 0; // ADD THIS
  virtual void setUniformInt(const char *name, int value) = 0;
  virtual void setUniformFloat(const char *name, float value) = 0;

  virtual void setTexture(const char *name, TextureHandle texture,
                          uint32_t slot = 0) = 0;
  virtual void copyToTexture(TextureHandle texture, uint32_t mipLevel,
                             std::span<const std::byte> data) = 0;

  virtual void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0,
                           uint32_t instanceCount = 1) = 0;
  virtual void endRender() = 0;
  virtual void copyToBuffer(BufferHandle, size_t dstOff,
                            std::span<const std::byte> src) = 0;
  virtual void end() = 0;
  virtual ~CmdList() = default;
};

struct Device {
  virtual ~Device() = default;
  virtual const Caps &caps() const = 0;

  virtual BufferHandle createBuffer(const BufferDesc &) = 0;
  virtual TextureHandle createTexture(const TextureDesc &) = 0;
  virtual SamplerHandle createSampler(const SamplerDesc &) = 0;
  virtual ShaderHandle createShader(std::string_view stage,
                                    std::span<const uint8_t> bytes) = 0;
  virtual PipelineHandle createPipeline(const PipelineDesc &) = 0;

  virtual CmdList *getImmediate() = 0;
  virtual void present() = 0;
};

/// Factory (implemented in rhi_dispatch.cpp)
Device *create_device_from_config(bool preferMetalIfAvailable);
} // namespace pixel::rhi
