#include "device_vk.hpp"

#include <stdexcept>
#include <string>

namespace pixel::rhi {

VulkanCmdList::VulkanCmdList(VulkanDevice &device) : device_(device) {}

void VulkanCmdList::begin() {
  device_.beginFrameIfNeeded();
  VkCommandBuffer cmd = device_.currentCommandBuffer();
  if (cmd == VK_NULL_HANDLE) {
    throw std::runtime_error(
        "Vulkan command list cannot begin without a valid command buffer");
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to begin Vulkan command buffer");
  }

  activeCommandBuffer_ = cmd;
}

void VulkanCmdList::beginRender(const RenderPassDesc &) {
  notImplemented("beginRender");
}

void VulkanCmdList::setPipeline(PipelineHandle) { notImplemented("setPipeline"); }

void VulkanCmdList::setVertexBuffer(BufferHandle, size_t) {
  notImplemented("setVertexBuffer");
}

void VulkanCmdList::setIndexBuffer(BufferHandle, size_t) {
  notImplemented("setIndexBuffer");
}

void VulkanCmdList::setInstanceBuffer(BufferHandle, size_t, size_t) {
  notImplemented("setInstanceBuffer");
}

void VulkanCmdList::setDepthStencilState(const DepthStencilState &) {
  notImplemented("setDepthStencilState");
}

void VulkanCmdList::setDepthBias(const DepthBiasState &) {
  notImplemented("setDepthBias");
}

void VulkanCmdList::setUniformMat4(const char *, const float *) {
  notImplemented("setUniformMat4");
}

void VulkanCmdList::setUniformVec3(const char *, const float *) {
  notImplemented("setUniformVec3");
}

void VulkanCmdList::setUniformVec4(const char *, const float *) {
  notImplemented("setUniformVec4");
}

void VulkanCmdList::setUniformInt(const char *, int) {
  notImplemented("setUniformInt");
}

void VulkanCmdList::setUniformFloat(const char *, float) {
  notImplemented("setUniformFloat");
}

void VulkanCmdList::setUniformBuffer(uint32_t, BufferHandle, size_t, size_t) {
  notImplemented("setUniformBuffer");
}

void VulkanCmdList::setTexture(const char *, TextureHandle, uint32_t,
                               SamplerHandle) {
  notImplemented("setTexture");
}

void VulkanCmdList::copyToTexture(TextureHandle, uint32_t,
                                  std::span<const std::byte>) {
  notImplemented("copyToTexture");
}

void VulkanCmdList::copyToTextureLayer(TextureHandle, uint32_t, uint32_t,
                                       std::span<const std::byte>) {
  notImplemented("copyToTextureLayer");
}

void VulkanCmdList::setComputePipeline(PipelineHandle) {
  notImplemented("setComputePipeline");
}

void VulkanCmdList::setStorageBuffer(uint32_t, BufferHandle, size_t, size_t) {
  notImplemented("setStorageBuffer");
}

void VulkanCmdList::dispatch(uint32_t, uint32_t, uint32_t) {
  notImplemented("dispatch");
}

void VulkanCmdList::memoryBarrier() { notImplemented("memoryBarrier"); }

void VulkanCmdList::resourceBarrier(
    std::span<const ResourceBarrierDesc>) {
  notImplemented("resourceBarrier");
}

void VulkanCmdList::beginQuery(QueryHandle, QueryType) {
  notImplemented("beginQuery");
}

void VulkanCmdList::endQuery(QueryHandle, QueryType) {
  notImplemented("endQuery");
}

void VulkanCmdList::signalFence(FenceHandle) {
  notImplemented("signalFence");
}

void VulkanCmdList::drawIndexed(uint32_t, uint32_t, uint32_t) {
  notImplemented("drawIndexed");
}

void VulkanCmdList::endRender() { notImplemented("endRender"); }

void VulkanCmdList::copyToBuffer(BufferHandle, size_t,
                                 std::span<const std::byte>) {
  notImplemented("copyToBuffer");
}

void VulkanCmdList::end() {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    return;
  }

  if (vkEndCommandBuffer(activeCommandBuffer_) != VK_SUCCESS) {
    throw std::runtime_error("Failed to end Vulkan command buffer");
  }

  activeCommandBuffer_ = VK_NULL_HANDLE;
}

[[noreturn]] void VulkanCmdList::notImplemented(const char *feature) const {
  throw std::runtime_error(std::string("Vulkan backend: ") + feature +
                           " not implemented yet");
}

} // namespace pixel::rhi

