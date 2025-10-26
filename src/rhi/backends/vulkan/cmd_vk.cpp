#include "device_vk.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pixel::rhi {

namespace {

VkPipelineStageFlags toVkPipelineStage(PipelineStage stage) {
  switch (stage) {
  case PipelineStage::TopOfPipe:
    return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  case PipelineStage::VertexShader:
    return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
  case PipelineStage::FragmentShader:
    return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  case PipelineStage::ComputeShader:
    return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  case PipelineStage::Transfer:
    return VK_PIPELINE_STAGE_TRANSFER_BIT;
  case PipelineStage::BottomOfPipe:
  default:
    return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  }
}

VkAccessFlags toVkAccess(ResourceState state) {
  switch (state) {
  case ResourceState::General:
    return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  case ResourceState::CopySrc:
    return VK_ACCESS_TRANSFER_READ_BIT;
  case ResourceState::CopyDst:
    return VK_ACCESS_TRANSFER_WRITE_BIT;
  case ResourceState::ShaderRead:
    return VK_ACCESS_SHADER_READ_BIT;
  case ResourceState::ShaderWrite:
    return VK_ACCESS_SHADER_WRITE_BIT;
  case ResourceState::RenderTarget:
    return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  case ResourceState::DepthStencilRead:
    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  case ResourceState::DepthStencilWrite:
    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  case ResourceState::Present:
    return VK_ACCESS_MEMORY_READ_BIT;
  case ResourceState::Undefined:
  default:
    return 0;
  }
}

VkImageLayout toVkImageLayout(ResourceState state) {
  switch (state) {
  case ResourceState::General:
    return VK_IMAGE_LAYOUT_GENERAL;
  case ResourceState::CopySrc:
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  case ResourceState::CopyDst:
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  case ResourceState::ShaderRead:
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  case ResourceState::ShaderWrite:
    return VK_IMAGE_LAYOUT_GENERAL;
  case ResourceState::RenderTarget:
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  case ResourceState::DepthStencilRead:
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  case ResourceState::DepthStencilWrite:
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  case ResourceState::Present:
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  case ResourceState::Undefined:
  default:
    return VK_IMAGE_LAYOUT_UNDEFINED;
  }
}

VkClearValue makeColorClear(const RenderPassColorAttachment &attachment) {
  VkClearValue clear{};
  clear.color.float32[0] = attachment.clearColor[0];
  clear.color.float32[1] = attachment.clearColor[1];
  clear.color.float32[2] = attachment.clearColor[2];
  clear.color.float32[3] = attachment.clearColor[3];
  return clear;
}

VkClearValue makeDepthClear(const RenderPassDepthAttachment &attachment) {
  VkClearValue clear{};
  clear.depthStencil.depth = attachment.clearDepth;
  clear.depthStencil.stencil = attachment.clearStencil;
  return clear;
}

bool isDepthFormat(Format format) {
  return format == Format::D24S8 || format == Format::D32F;
}

const VulkanDevice::PipelineResource &getPipeline(const VulkanDevice &device,
                                                  PipelineHandle handle) {
  if (handle.id == 0 || handle.id >= device.pipelines_.size()) {
    throw std::runtime_error("Invalid Vulkan pipeline handle");
  }
  return device.pipelines_[handle.id];
}

const VulkanDevice::BufferResource &getBuffer(const VulkanDevice &device,
                                              BufferHandle handle) {
  if (handle.id == 0 || handle.id >= device.buffers_.size()) {
    throw std::runtime_error("Invalid Vulkan buffer handle");
  }
  return device.buffers_[handle.id];
}

const VulkanDevice::TextureResource &getTexture(const VulkanDevice &device,
                                                TextureHandle handle) {
  if (handle.id == 0 || handle.id >= device.textures_.size()) {
    throw std::runtime_error("Invalid Vulkan texture handle");
  }
  return device.textures_[handle.id];
}

VkImageView swapchainViewForCurrentImage(const VulkanDevice &device) {
  if (device.swapchainImageViews_.empty()) {
    throw std::runtime_error("Vulkan swapchain image views not initialized");
  }
  if (device.currentImageIndex_ >= device.swapchainImageViews_.size()) {
    throw std::runtime_error("Vulkan current swapchain index out of range");
  }
  return device.swapchainImageViews_[device.currentImageIndex_];
}

VkImage swapchainImageForCurrentIndex(const VulkanDevice &device) {
  if (device.swapchainImages_.empty()) {
    throw std::runtime_error("Vulkan swapchain images not initialized");
  }
  if (device.currentImageIndex_ >= device.swapchainImages_.size()) {
    throw std::runtime_error("Vulkan current swapchain index out of range");
  }
  return device.swapchainImages_[device.currentImageIndex_];
}

} // namespace

struct VulkanCmdList::PixelUniformBufferData {
  alignas(16) float model[16]{};
  alignas(16) float view[16]{};
  alignas(16) float projection[16]{};
  alignas(16) float normalMatrix[16]{};
  alignas(16) float lightViewProj[16]{};
  alignas(16) float materialColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
  alignas(16) float lightPos[4]{};
  alignas(16) float alphaCutoff[4]{};
  alignas(16) float viewPos[4]{};
  alignas(16) float baseAlpha[4]{1.0f, 0.0f, 0.0f, 0.0f};
  alignas(16) float lightColor[4]{1.0f, 1.0f, 1.0f, 0.0f};
  alignas(16) float shadowBias[4]{};
  alignas(16) float uTime[4]{};
  alignas(16) float ditherScale[4]{1.0f, 0.0f, 0.0f, 0.0f};
  alignas(16) float crossfadeDuration[4]{};
  alignas(16) float padMisc[4]{};
  alignas(16) int32_t useTexture[4]{};
  alignas(16) int32_t useTextureArray[4]{};
  alignas(16) int32_t uDitherEnabled[4]{};
  alignas(16) int32_t shadowsEnabled[4]{};
};

void VulkanCmdList::resetDescriptorState() {
  if (currentDescriptorSet_ != VK_NULL_HANDLE && device_.descriptorPool_ != VK_NULL_HANDLE) {
    vkFreeDescriptorSets(device_.vkDevice(), device_.descriptorPool_, 1,
                         &currentDescriptorSet_);
  }
  currentDescriptorSet_ = VK_NULL_HANDLE;
  descriptorSetPipeline_ = {};
  descriptorSetDirty_ = true;
}

void VulkanCmdList::ensurePixelUniformResources() {
  if (pixelUniformBuffer_.id == 0) {
    BufferDesc desc{};
    desc.size = sizeof(PixelUniformBufferData);
    desc.usage = BufferUsage::Uniform;
    desc.hostVisible = true;
    pixelUniformBuffer_ = device_.createBuffer(desc);
    pixelUniformMapped_ = nullptr;
    pixelUniformsDirty_ = true;
  }

  if (pixelUniformBuffer_.id == 0) {
    return;
  }

  auto &resource = device_.buffers_[pixelUniformBuffer_.id];
  if (!pixelUniformMapped_) {
    if (resource.allocationInfo.pMappedData) {
      pixelUniformMapped_ = resource.allocationInfo.pMappedData;
    } else {
      void *mapped = nullptr;
      if (vmaMapMemory(device_.allocator(), resource.allocation, &mapped) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map Vulkan uniform buffer memory");
      }
      pixelUniformMapped_ = mapped;
    }
  }

  auto [it, inserted] = uniformBuffers_.try_emplace(kPixelUniformBinding);
  auto &bound = it->second;
  bound.handle = pixelUniformBuffer_;
  bound.offset = 0;
  bound.size = sizeof(PixelUniformBufferData);
  bound.bufferInfo.buffer = resource.buffer;
  bound.bufferInfo.offset = 0;
  bound.bufferInfo.range = sizeof(PixelUniformBufferData);
  if (inserted) {
    descriptorSetDirty_ = true;
  }
}

void VulkanCmdList::uploadPixelUniformsIfNeeded() {
  if (!pixelUniformsDirty_ || pixelUniformBuffer_.id == 0 || !pixelUniformMapped_) {
    return;
  }

  std::memcpy(pixelUniformMapped_, &pixelUniforms_, sizeof(PixelUniformBufferData));

  auto &resource = device_.buffers_[pixelUniformBuffer_.id];
  vmaFlushAllocation(device_.allocator(), resource.allocation, 0,
                     sizeof(PixelUniformBufferData));

  pixelUniformsDirty_ = false;
}

void VulkanCmdList::ensureDescriptorSetForCurrentPipeline() {
  if (currentGraphicsPipeline_.id == 0) {
    return;
  }

  const auto &pipeline = getPipeline(device_, currentGraphicsPipeline_);
  if (pipeline.descriptorSetLayouts.empty()) {
    return;
  }

  if (descriptorSetPipeline_.id == currentGraphicsPipeline_.id &&
      currentDescriptorSet_ != VK_NULL_HANDLE) {
    return;
  }

  if (currentDescriptorSet_ != VK_NULL_HANDLE && device_.descriptorPool_ != VK_NULL_HANDLE) {
    vkFreeDescriptorSets(device_.vkDevice(), device_.descriptorPool_, 1,
                         &currentDescriptorSet_);
    currentDescriptorSet_ = VK_NULL_HANDLE;
  }

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = device_.descriptorPool_;
  allocInfo.descriptorSetCount =
      static_cast<uint32_t>(pipeline.descriptorSetLayouts.size());
  allocInfo.pSetLayouts = pipeline.descriptorSetLayouts.data();

  std::vector<VkDescriptorSet> sets(allocInfo.descriptorSetCount);
  if (vkAllocateDescriptorSets(device_.vkDevice(), &allocInfo, sets.data()) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate Vulkan descriptor set");
  }

  currentDescriptorSet_ = sets.front();
  descriptorSetPipeline_ = currentGraphicsPipeline_;
  descriptorSetDirty_ = true;
}

SamplerHandle VulkanCmdList::ensureDefaultSampler() {
  if (defaultSampler_.id != 0) {
    return defaultSampler_;
  }

  SamplerDesc desc{};
  defaultSampler_ = device_.createSampler(desc);
  if (defaultSampler_.id == 0) {
    throw std::runtime_error("Failed to create default Vulkan sampler");
  }
  return defaultSampler_;
}

void VulkanCmdList::bindDescriptorSetIfNeeded() {
  ensureDescriptorSetForCurrentPipeline();

  if (currentDescriptorSet_ == VK_NULL_HANDLE || currentGraphicsPipeline_.id == 0) {
    return;
  }

  ensurePixelUniformResources();
  uploadPixelUniformsIfNeeded();

  if (descriptorSetDirty_) {
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(uniformBuffers_.size() + boundTextures_.size());

    for (auto &[binding, bound] : uniformBuffers_) {
      if (bound.handle.id == 0) {
        continue;
      }
      const auto &resource = getBuffer(device_, bound.handle);
      bound.bufferInfo.buffer = resource.buffer;
      bound.bufferInfo.offset = bound.offset;
      bound.bufferInfo.range = bound.size == 0
                                   ? static_cast<VkDeviceSize>(resource.desc.size - bound.offset)
                                   : static_cast<VkDeviceSize>(bound.size);

      VkWriteDescriptorSet write{};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = currentDescriptorSet_;
      write.dstBinding = binding;
      write.dstArrayElement = 0;
      write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      write.descriptorCount = 1;
      write.pBufferInfo = &bound.bufferInfo;
      writes.push_back(write);
    }

    for (auto &[binding, boundTex] : boundTextures_) {
      if (boundTex.handle.id == 0) {
        continue;
      }

      const auto &tex = getTexture(device_, boundTex.handle);
      VkSampler sampler = VK_NULL_HANDLE;
      if (boundTex.sampler.id != 0 && boundTex.sampler.id < device_.samplers_.size()) {
        sampler = device_.samplers_[boundTex.sampler.id].sampler;
      }
      if (sampler == VK_NULL_HANDLE) {
        SamplerHandle handle = ensureDefaultSampler();
        if (handle.id < device_.samplers_.size()) {
          sampler = device_.samplers_[handle.id].sampler;
        }
      }

      if (sampler == VK_NULL_HANDLE || tex.view == VK_NULL_HANDLE) {
        continue;
      }

      boundTex.imageInfo.sampler = sampler;
      boundTex.imageInfo.imageView = tex.view;
      boundTex.imageInfo.imageLayout =
          isDepthFormat(tex.desc.format)
              ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
              : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      VkWriteDescriptorSet write{};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = currentDescriptorSet_;
      write.dstBinding = binding;
      write.dstArrayElement = 0;
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo = &boundTex.imageInfo;
      writes.push_back(write);
    }

    if (!writes.empty()) {
      vkUpdateDescriptorSets(device_.vkDevice(),
                             static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    descriptorSetDirty_ = false;
  }

  const auto &pipeline = getPipeline(device_, currentGraphicsPipeline_);
  vkCmdBindDescriptorSets(activeCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline.layout, 0, 1, &currentDescriptorSet_, 0, nullptr);
}

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
  pendingRenderPass_ = {};
  renderPassPending_ = false;
  renderPassActive_ = false;
  currentGraphicsPipeline_ = {};
  currentComputePipeline_ = {};

  if (activeFramebuffer_ != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device_.vkDevice(), activeFramebuffer_, nullptr);
    activeFramebuffer_ = VK_NULL_HANDLE;
  }

  resetDescriptorState();
  uniformBuffers_.clear();
  boundTextures_.clear();
  pixelUniformsDirty_ = true;
}

void VulkanCmdList::beginRender(const RenderPassDesc &desc) {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan beginRender called before begin");
  }
  if (renderPassActive_) {
    throw std::runtime_error("Vulkan render pass already active");
  }

  pendingRenderPass_ = desc;
  renderPassPending_ = true;
}

void VulkanCmdList::setPipeline(PipelineHandle handle) {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan setPipeline called before begin");
  }

  PipelineHandle previousGraphics = currentGraphicsPipeline_;
  const auto &pipeline = getPipeline(device_, handle);

  if (pipeline.isCompute) {
    vkCmdBindPipeline(activeCommandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                      pipeline.pipeline);
    currentComputePipeline_ = handle;
    return;
  }

  if (renderPassPending_ && !renderPassActive_) {
    VkExtent2D extent = device_.swapchainExtent_;
    std::vector<VkImageView> attachments;
    std::vector<VkClearValue> clearValues;

    attachments.reserve(pendingRenderPass_.colorAttachmentCount +
                        (pendingRenderPass_.hasDepthAttachment ? 1 : 0));
    clearValues.reserve(attachments.capacity());

    for (uint32_t i = 0; i < pendingRenderPass_.colorAttachmentCount; ++i) {
      const auto &attachment = pendingRenderPass_.colorAttachments[i];
      VkImageView view = VK_NULL_HANDLE;
      if (attachment.texture.id == 0) {
        view = swapchainViewForCurrentImage(device_);
      } else {
        view = getTexture(device_, attachment.texture).view;
      }
      if (view == VK_NULL_HANDLE) {
        throw std::runtime_error("Vulkan color attachment missing image view");
      }
      attachments.push_back(view);
      if (attachment.loadOp == LoadOp::Clear) {
        clearValues.push_back(makeColorClear(attachment));
      } else {
        clearValues.emplace_back();
      }
    }

    if (pendingRenderPass_.hasDepthAttachment) {
      const auto &attachment = pendingRenderPass_.depthAttachment;
      VkImageView view = VK_NULL_HANDLE;
      if (attachment.texture.id == 0) {
        throw std::runtime_error(
            "Vulkan backend does not currently support default depth attachment");
      }
      view = getTexture(device_, attachment.texture).view;
      if (view == VK_NULL_HANDLE) {
        throw std::runtime_error("Vulkan depth attachment missing image view");
      }
      attachments.push_back(view);
      clearValues.push_back(makeDepthClear(attachment));
    }

    if (attachments.empty()) {
      throw std::runtime_error("Vulkan render pass requires at least one attachment");
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = pipeline.renderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = extent.width;
    fbInfo.height = extent.height;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device_.vkDevice(), &fbInfo, nullptr,
                            &activeFramebuffer_) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create Vulkan framebuffer");
    }

    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = pipeline.renderPass;
    beginInfo.framebuffer = activeFramebuffer_;
    beginInfo.renderArea.offset = {0, 0};
    beginInfo.renderArea.extent = extent;
    beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    beginInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(activeCommandBuffer_, &beginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(activeCommandBuffer_, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(activeCommandBuffer_, 0, 1, &scissor);

    renderPassActive_ = true;
    renderPassPending_ = false;
  }

  vkCmdBindPipeline(activeCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.pipeline);
  currentGraphicsPipeline_ = handle;

  if (previousGraphics.id != handle.id) {
    resetDescriptorState();
  }
}

void VulkanCmdList::setVertexBuffer(BufferHandle handle, size_t offset) {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan setVertexBuffer called before begin");
  }

  const auto &buffer = getBuffer(device_, handle);
  VkBuffer buffers[] = {buffer.buffer};
  VkDeviceSize offsets[] = {offset};
  vkCmdBindVertexBuffers(activeCommandBuffer_, 0, 1, buffers, offsets);
}

void VulkanCmdList::setIndexBuffer(BufferHandle handle, size_t offset) {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan setIndexBuffer called before begin");
  }

  const auto &buffer = getBuffer(device_, handle);
  vkCmdBindIndexBuffer(activeCommandBuffer_, buffer.buffer, offset,
                       VK_INDEX_TYPE_UINT32);
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

void VulkanCmdList::setUniformMat4(const char *name, const float *mat4x4) {
  if (!name || !mat4x4) {
    return;
  }

  std::string_view uniformName{name};
  bool updated = false;

  if (uniformName == "model") {
    std::memcpy(pixelUniforms_.model, mat4x4, sizeof(float) * 16);
    updated = true;
  } else if (uniformName == "view") {
    std::memcpy(pixelUniforms_.view, mat4x4, sizeof(float) * 16);
    updated = true;
  } else if (uniformName == "projection") {
    std::memcpy(pixelUniforms_.projection, mat4x4, sizeof(float) * 16);
    updated = true;
  } else if (uniformName == "normalMatrix") {
    std::memcpy(pixelUniforms_.normalMatrix, mat4x4, sizeof(float) * 16);
    updated = true;
  } else if (uniformName == "lightViewProj") {
    std::memcpy(pixelUniforms_.lightViewProj, mat4x4, sizeof(float) * 16);
    updated = true;
  }

  if (updated) {
    ensurePixelUniformResources();
    pixelUniformsDirty_ = true;
  }
}

void VulkanCmdList::setUniformVec3(const char *name, const float *vec3) {
  if (!name || !vec3) {
    return;
  }

  std::string_view uniformName{name};
  bool updated = false;

  if (uniformName == "lightPos") {
    std::memcpy(pixelUniforms_.lightPos, vec3, sizeof(float) * 3);
    pixelUniforms_.lightPos[3] = 0.0f;
    updated = true;
  } else if (uniformName == "viewPos") {
    std::memcpy(pixelUniforms_.viewPos, vec3, sizeof(float) * 3);
    pixelUniforms_.viewPos[3] = 0.0f;
    updated = true;
  } else if (uniformName == "lightColor") {
    std::memcpy(pixelUniforms_.lightColor, vec3, sizeof(float) * 3);
    pixelUniforms_.lightColor[3] = 0.0f;
    updated = true;
  }

  if (updated) {
    ensurePixelUniformResources();
    pixelUniformsDirty_ = true;
  }
}

void VulkanCmdList::setUniformVec4(const char *name, const float *vec4) {
  if (!name || !vec4) {
    return;
  }

  std::string_view uniformName{name};
  if (uniformName == "materialColor") {
    std::memcpy(pixelUniforms_.materialColor, vec4, sizeof(float) * 4);
    ensurePixelUniformResources();
    pixelUniformsDirty_ = true;
  }
}

void VulkanCmdList::setUniformInt(const char *name, int value) {
  if (!name) {
    return;
  }

  std::string_view uniformName{name};
  bool updated = false;

  if (uniformName == "useTexture") {
    pixelUniforms_.useTexture[0] = value;
    updated = true;
  } else if (uniformName == "useTextureArray") {
    pixelUniforms_.useTextureArray[0] = value;
    updated = true;
  } else if (uniformName == "uDitherEnabled" || uniformName == "ditherEnabled") {
    pixelUniforms_.uDitherEnabled[0] = value;
    updated = true;
  } else if (uniformName == "shadowsEnabled") {
    pixelUniforms_.shadowsEnabled[0] = value;
    updated = true;
  }

  if (updated) {
    ensurePixelUniformResources();
    pixelUniformsDirty_ = true;
  }
}

void VulkanCmdList::setUniformFloat(const char *name, float value) {
  if (!name) {
    return;
  }

  std::string_view uniformName{name};
  bool updated = false;

  if (uniformName == "uTime" || uniformName == "time") {
    pixelUniforms_.uTime[0] = value;
    updated = true;
  } else if (uniformName == "ditherScale" || uniformName == "uDitherScale") {
    pixelUniforms_.ditherScale[0] = value;
    updated = true;
  } else if (uniformName == "crossfadeDuration" ||
             uniformName == "uCrossfadeDuration") {
    pixelUniforms_.crossfadeDuration[0] = value;
    updated = true;
  } else if (uniformName == "shadowBias") {
    pixelUniforms_.shadowBias[0] = value;
    updated = true;
  } else if (uniformName == "alphaCutoff") {
    pixelUniforms_.alphaCutoff[0] = value;
    updated = true;
  } else if (uniformName == "baseAlpha") {
    pixelUniforms_.baseAlpha[0] = value;
    updated = true;
  }

  if (updated) {
    ensurePixelUniformResources();
    pixelUniformsDirty_ = true;
  }
}

void VulkanCmdList::setUniformBuffer(uint32_t binding, BufferHandle buffer,
                                     size_t offset, size_t size) {
  if (buffer.id == 0) {
    if (uniformBuffers_.erase(binding) > 0) {
      descriptorSetDirty_ = true;
    }
    return;
  }

  const auto &resource = getBuffer(device_, buffer);
  if (offset >= resource.desc.size) {
    throw std::runtime_error("Uniform buffer offset exceeds buffer size");
  }

  size_t available = resource.desc.size - offset;
  size_t bindSize = size == 0 ? available : size;
  if (bindSize == 0 || bindSize > available) {
    throw std::runtime_error("Invalid uniform buffer binding size");
  }

  auto &bound = uniformBuffers_[binding];
  bound.handle = buffer;
  bound.offset = offset;
  bound.size = bindSize;
  bound.bufferInfo.buffer = resource.buffer;
  bound.bufferInfo.offset = offset;
  bound.bufferInfo.range = bindSize;

  descriptorSetDirty_ = true;
}

void VulkanCmdList::setTexture(const char *, TextureHandle texture, uint32_t slot,
                               SamplerHandle sampler) {
  if (texture.id == 0) {
    if (boundTextures_.erase(slot) > 0) {
      descriptorSetDirty_ = true;
    }
    return;
  }

  if (texture.id >= device_.textures_.size()) {
    throw std::runtime_error("Invalid Vulkan texture handle");
  }

  BoundTexture &bound = boundTextures_[slot];
  bound.handle = texture;
  bound.sampler = sampler;

  descriptorSetDirty_ = true;
}

void VulkanCmdList::copyToTexture(TextureHandle, uint32_t,
                                  std::span<const std::byte>) {
  notImplemented("copyToTexture");
}

void VulkanCmdList::copyToTextureLayer(TextureHandle, uint32_t, uint32_t,
                                       std::span<const std::byte>) {
  notImplemented("copyToTextureLayer");
}

void VulkanCmdList::setComputePipeline(PipelineHandle handle) {
  setPipeline(handle);
}

void VulkanCmdList::setStorageBuffer(uint32_t, BufferHandle, size_t, size_t) {
  notImplemented("setStorageBuffer");
}

void VulkanCmdList::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                             uint32_t groupCountZ) {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan dispatch called before begin");
  }
  if (currentComputePipeline_.id == 0) {
    throw std::runtime_error("Vulkan dispatch requires bound compute pipeline");
  }
  vkCmdDispatch(activeCommandBuffer_, groupCountX, groupCountY, groupCountZ);
}

void VulkanCmdList::memoryBarrier() {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan memoryBarrier called before begin");
  }

  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

  vkCmdPipelineBarrier(activeCommandBuffer_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0,
                       nullptr, 0, nullptr);
}

void VulkanCmdList::resourceBarrier(
    std::span<const ResourceBarrierDesc> barriers) {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan resourceBarrier called before begin");
  }
  if (barriers.empty()) {
    return;
  }

  std::vector<VkBufferMemoryBarrier> bufferBarriers;
  std::vector<VkImageMemoryBarrier> imageBarriers;
  bufferBarriers.reserve(barriers.size());
  imageBarriers.reserve(barriers.size());

  VkPipelineStageFlags srcStages = 0;
  VkPipelineStageFlags dstStages = 0;

  for (const auto &barrier : barriers) {
    VkPipelineStageFlags srcStage = toVkPipelineStage(barrier.srcStage);
    VkPipelineStageFlags dstStage = toVkPipelineStage(barrier.dstStage);
    srcStages |= srcStage;
    dstStages |= dstStage;

    VkAccessFlags srcAccess = toVkAccess(barrier.srcState);
    VkAccessFlags dstAccess = toVkAccess(barrier.dstState);

    if (barrier.type == BarrierType::Buffer) {
      if (barrier.buffer.id == 0 ||
          barrier.buffer.id >= device_.buffers_.size()) {
        throw std::runtime_error("Vulkan buffer barrier references invalid buffer");
      }
      const auto &buffer = device_.buffers_[barrier.buffer.id];

      VkBufferMemoryBarrier bufferBarrier{};
      bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      bufferBarrier.srcAccessMask = srcAccess;
      bufferBarrier.dstAccessMask = dstAccess;
      bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      bufferBarrier.buffer = buffer.buffer;
      bufferBarrier.offset = 0;
      bufferBarrier.size = VK_WHOLE_SIZE;
      bufferBarriers.push_back(bufferBarrier);
      continue;
    }

    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    if (barrier.texture.id == 0) {
      image = swapchainImageForCurrentIndex(device_);
    } else {
      if (barrier.texture.id >= device_.textures_.size()) {
        throw std::runtime_error(
            "Vulkan texture barrier references invalid texture");
      }
      const auto &texture = device_.textures_[barrier.texture.id];
      image = texture.image;
      if (isDepthFormat(texture.desc.format)) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (texture.desc.format == Format::D24S8) {
          aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
      }
    }

    if (image == VK_NULL_HANDLE) {
      throw std::runtime_error("Vulkan image barrier missing image");
    }

    VkImageMemoryBarrier imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarrier.srcAccessMask = srcAccess;
    imageBarrier.dstAccessMask = dstAccess;
    imageBarrier.oldLayout = toVkImageLayout(barrier.srcState);
    imageBarrier.newLayout = toVkImageLayout(barrier.dstState);
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = image;
    imageBarrier.subresourceRange.aspectMask = aspectMask;
    imageBarrier.subresourceRange.baseMipLevel = barrier.baseMipLevel;
    imageBarrier.subresourceRange.levelCount = barrier.levelCount == 0
                                                  ? VK_REMAINING_MIP_LEVELS
                                                  : barrier.levelCount;
    imageBarrier.subresourceRange.baseArrayLayer = barrier.baseArrayLayer;
    imageBarrier.subresourceRange.layerCount = barrier.layerCount == 0
                                                   ? VK_REMAINING_ARRAY_LAYERS
                                                   : barrier.layerCount;

    imageBarriers.push_back(imageBarrier);
  }

  vkCmdPipelineBarrier(activeCommandBuffer_, srcStages, dstStages, 0, 0, nullptr,
                       static_cast<uint32_t>(bufferBarriers.size()),
                       bufferBarriers.data(),
                       static_cast<uint32_t>(imageBarriers.size()),
                       imageBarriers.data());
}

void VulkanCmdList::beginQuery(QueryHandle, QueryType) {
  notImplemented("beginQuery");
}

void VulkanCmdList::endQuery(QueryHandle, QueryType) {
  notImplemented("endQuery");
}

void VulkanCmdList::signalFence(FenceHandle handle) {
  if (handle.id == 0) {
    pendingFence_.reset();
    return;
  }
  pendingFence_ = handle;
}

void VulkanCmdList::drawIndexed(uint32_t indexCount, uint32_t firstIndex,
                                uint32_t instanceCount) {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan drawIndexed called before begin");
  }
  if (!renderPassActive_) {
    throw std::runtime_error("Vulkan drawIndexed requires active render pass");
  }
  if (currentGraphicsPipeline_.id == 0) {
    throw std::runtime_error("Vulkan drawIndexed requires bound graphics pipeline");
  }

  bindDescriptorSetIfNeeded();

  vkCmdDrawIndexed(activeCommandBuffer_, indexCount, instanceCount, firstIndex, 0,
                   0);
}

void VulkanCmdList::endRender() {
  if (!renderPassActive_) {
    return;
  }

  vkCmdEndRenderPass(activeCommandBuffer_);
  renderPassActive_ = false;
  renderPassPending_ = false;

  if (activeFramebuffer_ != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device_.vkDevice(), activeFramebuffer_, nullptr);
    activeFramebuffer_ = VK_NULL_HANDLE;
  }
}

void VulkanCmdList::copyToBuffer(BufferHandle, size_t,
                                 std::span<const std::byte>) {
  notImplemented("copyToBuffer");
}

void VulkanCmdList::end() {
  if (activeCommandBuffer_ == VK_NULL_HANDLE) {
    return;
  }

  if (renderPassActive_) {
    endRender();
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

std::optional<FenceHandle> VulkanCmdList::takePendingFence() {
  if (!pendingFence_.has_value()) {
    return std::nullopt;
  }
  auto result = pendingFence_;
  pendingFence_.reset();
  return result;
}

} // namespace pixel::rhi
