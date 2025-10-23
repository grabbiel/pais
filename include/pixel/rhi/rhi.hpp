#pragma once
#include "handles.hpp"
#include "types.hpp"
#include <array>
#include <span>
#include <string_view>
#include <functional>

struct GLFWwindow;

namespace pixel::rhi {

struct Caps {
  bool instancing{true};
  bool samplerAniso{false};
  float maxSamplerAnisotropy{1.0f};
  bool samplerCompare{false};
  bool uniformBuffers{true};
  bool clipSpaceYDown{false}; // GL vs Metal differences if needed
};

struct SwapchainDesc {
  void *nativeWindow{nullptr};
  Extent2D size;
};

struct PipelineDesc {
  ShaderHandle vs, fs, cs; // cs = compute shader
  uint32_t colorAttachmentCount{0};
  std::array<ColorAttachmentDesc, kMaxColorAttachments> colorAttachments{};
};

struct FramebufferAttachmentDesc {
  TextureHandle texture{};
  uint32_t mipLevel{0};
  uint32_t arraySlice{0};
};

struct FramebufferDepthAttachmentDesc {
  TextureHandle texture{};
  uint32_t mipLevel{0};
  uint32_t arraySlice{0};
  bool hasStencil{false};
};

struct FramebufferDesc {
  std::array<FramebufferAttachmentDesc, kMaxColorAttachments> colorAttachments{};
  uint32_t colorAttachmentCount{0};
  bool hasDepthAttachment{false};
  FramebufferDepthAttachmentDesc depthAttachment{};
};

struct RenderPassColorAttachment {
  TextureHandle texture{};
  LoadOp loadOp{LoadOp::Clear};
  StoreOp storeOp{StoreOp::Store};
  float clearColor[4]{0.0f, 0.0f, 0.0f, 0.0f};
  uint32_t mipLevel{0};
  uint32_t arraySlice{0};
};

struct RenderPassDepthAttachment {
  TextureHandle texture{};
  LoadOp depthLoadOp{LoadOp::Clear};
  StoreOp depthStoreOp{StoreOp::DontCare};
  LoadOp stencilLoadOp{LoadOp::DontCare};
  StoreOp stencilStoreOp{StoreOp::DontCare};
  float clearDepth{1.0f};
  uint32_t clearStencil{0};
  bool hasStencil{false};
};

struct RenderPassDesc {
  FramebufferHandle framebuffer{};
  std::array<RenderPassColorAttachment, kMaxColorAttachments> colorAttachments{};
  uint32_t colorAttachmentCount{0};
  bool hasDepthAttachment{false};
  RenderPassDepthAttachment depthAttachment{};
};

struct CmdList {
  virtual void begin() = 0;
  virtual void beginRender(const RenderPassDesc &desc) = 0;
  virtual void setPipeline(PipelineHandle) = 0;
  virtual void setVertexBuffer(BufferHandle, size_t offset = 0) = 0;
  virtual void setIndexBuffer(BufferHandle, size_t offset = 0) = 0;
  virtual void setInstanceBuffer(BufferHandle, size_t stride,
                                 size_t offset = 0) = 0;

  virtual void setUniformMat4(const char *name, const float *mat4x4) = 0;
  virtual void setUniformVec3(const char *name, const float *vec3) = 0;
  virtual void setUniformVec4(const char *name,
                              const float *vec4) = 0; // ADD THIS
  virtual void setUniformInt(const char *name, int value) = 0;
  virtual void setUniformFloat(const char *name, float value) = 0;

  // Uniform buffer binding for instanced rendering
  virtual void setUniformBuffer(uint32_t binding, BufferHandle buffer,
                                size_t offset = 0, size_t size = 0) = 0;

  virtual void setTexture(const char *name, TextureHandle texture,
                          uint32_t slot = 0) = 0;
  virtual void copyToTexture(TextureHandle texture, uint32_t mipLevel,
                             std::span<const std::byte> data) = 0;
  virtual void copyToTextureLayer(TextureHandle texture, uint32_t layer,
                                  uint32_t mipLevel,
                                  std::span<const std::byte> data) = 0;

  // Compute shader support
  virtual void setComputePipeline(PipelineHandle) = 0;
  virtual void setStorageBuffer(uint32_t binding, BufferHandle buffer,
                                size_t offset = 0, size_t size = 0) = 0;
  virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY = 1,
                        uint32_t groupCountZ = 1) = 0;
  virtual void memoryBarrier() = 0;
  virtual void resourceBarrier(std::span<const ResourceBarrierDesc> barriers) = 0;

  virtual void beginQuery(QueryHandle handle, QueryType type) = 0;
  virtual void endQuery(QueryHandle handle, QueryType type) = 0;
  virtual void signalFence(FenceHandle handle) = 0;

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
  virtual FramebufferHandle createFramebuffer(const FramebufferDesc &) = 0;
  virtual QueryHandle createQuery(QueryType type) = 0;
  virtual void destroyQuery(QueryHandle handle) = 0;
  virtual bool getQueryResult(QueryHandle handle, uint64_t &result,
                              bool wait) = 0;
  virtual FenceHandle createFence(bool signaled = false) = 0;
  virtual void destroyFence(FenceHandle handle) = 0;
  virtual void waitFence(FenceHandle handle, uint64_t timeout_ns = ~0ull) = 0;
  virtual void resetFence(FenceHandle handle) = 0;

  virtual CmdList *getImmediate() = 0;
  virtual void present() = 0;

  virtual void readBuffer(BufferHandle handle, void *dst, size_t size,
                          size_t offset = 0) = 0;
};

Device *create_gl_device(GLFWwindow *window);
#ifdef __APPLE__
// Metal backend (macOS/iOS only)
Device *create_metal_device(void *window);
#endif
} // namespace pixel::rhi
