// src/rhi/backends/metal/device_metal.hpp
#pragma once

#ifdef __APPLE__

#include "pixel/rhi/rhi.hpp"
#include <memory>
#include <unordered_map>

// Forward declare Objective-C types
#ifdef __OBJC__
@class CAMetalLayer;
@protocol MTLDevice;
@protocol MTLCommandQueue;
@protocol MTLTexture;
@protocol MTLBuffer;
@protocol MTLDepthStencilState;
@protocol MTLLibrary;
#else
typedef void CAMetalLayer;
typedef void MTLDevice;
typedef void MTLCommandQueue;
typedef void MTLTexture;
typedef void MTLBuffer;
typedef void MTLDepthStencilState;
typedef void MTLLibrary;
#endif

namespace pixel::rhi {

// Forward declarations
struct MTLBufferResource;
struct MTLTextureResource;
struct MTLSamplerResource;
struct MTLShaderResource;
struct MTLPipelineResource;
class MetalCmdList;

// ============================================================================
// Metal Device
// ============================================================================
class MetalDevice : public Device {
public:
  MetalDevice(void *device, void *layer, void *depth_texture);
  ~MetalDevice() override;

  const Caps &caps() const override;

  BufferHandle createBuffer(const BufferDesc &desc) override;
  TextureHandle createTexture(const TextureDesc &desc) override;
  SamplerHandle createSampler(const SamplerDesc &desc) override;
  ShaderHandle createShader(std::string_view stage,
                            std::span<const uint8_t> bytes) override;
  PipelineHandle createPipeline(const PipelineDesc &desc) override;
  FramebufferHandle createFramebuffer(const FramebufferDesc &desc) override;
  QueryHandle createQuery(QueryType type) override;
  void destroyQuery(QueryHandle handle) override;
  bool getQueryResult(QueryHandle handle, uint64_t &result,
                      bool wait) override;
  FenceHandle createFence(bool signaled = false) override;
  void destroyFence(FenceHandle handle) override;
  void waitFence(FenceHandle handle, uint64_t timeout_ns = ~0ull) override;
  void resetFence(FenceHandle handle) override;

  CmdList *getImmediate() override;
  void present() override;
  void readBuffer(BufferHandle handle, void *dst, size_t size,
                  size_t offset = 0) override;

private:
  friend class MetalCmdList;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Metal Command List
// ============================================================================
class MetalCmdList : public CmdList {
public:
  MetalCmdList(MetalDevice::Impl *device_impl);
  ~MetalCmdList() override;

  void begin() override;
  void beginRender(const RenderPassDesc &desc) override;
  void setPipeline(PipelineHandle handle) override;
  void setVertexBuffer(BufferHandle handle, size_t offset = 0) override;
  void setIndexBuffer(BufferHandle handle, size_t offset = 0) override;
  void setInstanceBuffer(BufferHandle handle, size_t stride,
                         size_t offset = 0) override;

  void setUniformMat4(const char *name, const float *mat4x4) override;
  void setUniformVec3(const char *name, const float *vec3) override;
  void setUniformVec4(const char *name, const float *vec4) override;
  void setUniformInt(const char *name, int value) override;
  void setUniformFloat(const char *name, float value) override;

  void setUniformBuffer(uint32_t binding, BufferHandle buffer,
                        size_t offset = 0, size_t size = 0) override;
  void setTexture(const char *name, TextureHandle texture,
                  uint32_t slot = 0) override;
  void copyToTexture(TextureHandle texture, uint32_t mipLevel,
                     std::span<const std::byte> data) override;
  void copyToTextureLayer(TextureHandle texture, uint32_t layer,
                          uint32_t mipLevel,
                          std::span<const std::byte> data) override;

  void setComputePipeline(PipelineHandle handle) override;
  void setStorageBuffer(uint32_t binding, BufferHandle buffer,
                        size_t offset = 0, size_t size = 0) override;
  void dispatch(uint32_t groupCountX, uint32_t groupCountY = 1,
                uint32_t groupCountZ = 1) override;
  void memoryBarrier() override;
  void resourceBarrier(std::span<const ResourceBarrierDesc> barriers) override;
  void beginQuery(QueryHandle handle, QueryType type) override;
  void endQuery(QueryHandle handle, QueryType type) override;
  void signalFence(FenceHandle handle) override;

  void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0,
                   uint32_t instanceCount = 1) override;
  void endRender() override;
  void copyToBuffer(BufferHandle handle, size_t dstOff,
                    std::span<const std::byte> src) override;
  void end() override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pixel::rhi

#endif // __APPLE__
