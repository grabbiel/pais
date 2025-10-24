#include "device_vk.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pixel::rhi {
namespace {

#ifndef NDEBUG
constexpr bool kEnableValidationLayers = true;
#else
constexpr bool kEnableValidationLayers = false;
#endif

const std::array<const char *, 1> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

const std::array<const char *, 1> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData) {
  (void)messageSeverity;
  (void)messageType;
  (void)pUserData;

  std::cerr << "[Vulkan] " << pCallbackData->pMessage << std::endl;
  return VK_FALSE;
}

VkResult createDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) {
  auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  }
  return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroyDebugUtilsMessengerEXT(
    VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks *pAllocator) {
  auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

bool checkValidationLayerSupport() {
  uint32_t layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char *layerName : kValidationLayers) {
    bool layerFound = false;
    for (const auto &layerProperties : availableLayers) {
      if (std::strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }
    if (!layerFound) {
      return false;
    }
  }

  return true;
}

void populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
  createInfo = {};
  createInfo.sType =
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
}

VkBufferUsageFlags toVkBufferUsage(BufferUsage usage, bool hostVisible) {
  VkBufferUsageFlags flags = 0;
  const auto has = [&](BufferUsage bit) {
    return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(bit)) != 0;
  };

  if (has(BufferUsage::Vertex)) {
    flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if (has(BufferUsage::Index)) {
    flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (has(BufferUsage::Uniform)) {
    flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  if (has(BufferUsage::Storage)) {
    flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  if (has(BufferUsage::TransferSrc)) {
    flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }
  if (has(BufferUsage::TransferDst)) {
    flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  if (hostVisible) {
    flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  } else {
    flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  if (flags == 0) {
    flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  return flags;
}

VkFormat toVkFormat(Format format) {
  switch (format) {
  case Format::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case Format::BGRA8:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case Format::R8:
    return VK_FORMAT_R8_UNORM;
  case Format::R16F:
    return VK_FORMAT_R16_SFLOAT;
  case Format::RG16F:
    return VK_FORMAT_R16G16_SFLOAT;
  case Format::RGBA16F:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case Format::D24S8:
    return VK_FORMAT_D24_UNORM_S8_UINT;
  case Format::D32F:
    return VK_FORMAT_D32_SFLOAT;
  case Format::Unknown:
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

VkBlendFactor toVkBlendFactor(BlendFactor factor) {
  switch (factor) {
  case BlendFactor::Zero:
    return VK_BLEND_FACTOR_ZERO;
  case BlendFactor::One:
    return VK_BLEND_FACTOR_ONE;
  case BlendFactor::SrcColor:
    return VK_BLEND_FACTOR_SRC_COLOR;
  case BlendFactor::OneMinusSrcColor:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case BlendFactor::DstColor:
    return VK_BLEND_FACTOR_DST_COLOR;
  case BlendFactor::OneMinusDstColor:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  case BlendFactor::SrcAlpha:
    return VK_BLEND_FACTOR_SRC_ALPHA;
  case BlendFactor::OneMinusSrcAlpha:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case BlendFactor::DstAlpha:
    return VK_BLEND_FACTOR_DST_ALPHA;
  case BlendFactor::OneMinusDstAlpha:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  case BlendFactor::SrcAlphaSaturated:
    return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
  default:
    return VK_BLEND_FACTOR_ONE;
  }
}

VkBlendOp toVkBlendOp(BlendOp op) {
  switch (op) {
  case BlendOp::Add:
    return VK_BLEND_OP_ADD;
  case BlendOp::Subtract:
    return VK_BLEND_OP_SUBTRACT;
  case BlendOp::ReverseSubtract:
    return VK_BLEND_OP_REVERSE_SUBTRACT;
  case BlendOp::Min:
    return VK_BLEND_OP_MIN;
  case BlendOp::Max:
    return VK_BLEND_OP_MAX;
  default:
    return VK_BLEND_OP_ADD;
  }
}

VkShaderStageFlagBits shaderStageFromLabel(std::string_view stage) {
  if (stage.size() >= 2) {
    if (stage[0] == 'v' && stage[1] == 's') {
      return VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (stage[0] == 'f' && stage[1] == 's') {
      return VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (stage[0] == 'c' && stage[1] == 's') {
      return VK_SHADER_STAGE_COMPUTE_BIT;
    }
  }
  return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
}

bool stageUsesInstancing(std::string_view stage) {
  return stage.find("instanced") != std::string_view::npos;
}

VkImageAspectFlags aspectMaskForFormat(Format format) {
  switch (format) {
  case Format::D24S8:
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  case Format::D32F:
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  default:
    return VK_IMAGE_ASPECT_COLOR_BIT;
  }
}

bool isDepthFormat(Format format) {
  return format == Format::D24S8 || format == Format::D32F;
}

VkFilter toVkFilter(FilterMode mode) {
  switch (mode) {
  case FilterMode::Nearest:
    return VK_FILTER_NEAREST;
  case FilterMode::Linear:
  default:
    return VK_FILTER_LINEAR;
  }
}

VkSamplerAddressMode toVkAddressMode(AddressMode mode) {
  switch (mode) {
  case AddressMode::ClampToEdge:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  case AddressMode::ClampToBorder:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  case AddressMode::Repeat:
  default:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  }
}

VkCompareOp toVkCompareOp(CompareOp op) {
  switch (op) {
  case CompareOp::Never:
    return VK_COMPARE_OP_NEVER;
  case CompareOp::Less:
    return VK_COMPARE_OP_LESS;
  case CompareOp::Equal:
    return VK_COMPARE_OP_EQUAL;
  case CompareOp::LessEqual:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case CompareOp::Greater:
    return VK_COMPARE_OP_GREATER;
  case CompareOp::NotEqual:
    return VK_COMPARE_OP_NOT_EQUAL;
  case CompareOp::GreaterEqual:
    return VK_COMPARE_OP_GREATER_OR_EQUAL;
  case CompareOp::Always:
  default:
    return VK_COMPARE_OP_ALWAYS;
  }
}

bool approximately(float a, float b) {
  return std::fabs(a - b) <= 1e-4f;
}

VkBorderColor toVkBorderColor(const SamplerDesc &desc) {
  if (desc.addressU != AddressMode::ClampToBorder &&
      desc.addressV != AddressMode::ClampToBorder &&
      desc.addressW != AddressMode::ClampToBorder) {
    return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  }

  const float *c = desc.borderColor;
  if (approximately(c[0], 0.0f) && approximately(c[1], 0.0f) &&
      approximately(c[2], 0.0f) && approximately(c[3], 0.0f)) {
    return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  }

  if (approximately(c[0], 1.0f) && approximately(c[1], 1.0f) &&
      approximately(c[2], 1.0f) && approximately(c[3], 1.0f)) {
    return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  }

  return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
}

} // namespace

VulkanDevice::VulkanDevice(GLFWwindow *window) {
  if (!window) {
    throw std::invalid_argument("GLFW window handle cannot be null");
  }

  window_ = window;
  caps_.clipSpaceYDown = true;
  caps_.clipSpaceDepthZeroToOne = true;

  createInstance();
  setupDebugMessenger();
  createSurface(window);
  pickPhysicalDevice();
  createLogicalDevice();
  createAllocator();
  createSwapchain();
  createImageViews();
  createCommandPool();
  allocateCommandBuffers();
  createSyncObjects();
  createDescriptorPool();

  immediateCmdList_ = std::make_unique<VulkanCmdList>(*this);
}

VulkanDevice::~VulkanDevice() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }

  if (device_ != VK_NULL_HANDLE) {
    for (size_t i = 1; i < pipelines_.size(); ++i) {
      for (VkDescriptorSetLayout layout : pipelines_[i].descriptorSetLayouts) {
        if (layout != VK_NULL_HANDLE) {
          vkDestroyDescriptorSetLayout(device_, layout, nullptr);
        }
      }
      if (pipelines_[i].pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipelines_[i].pipeline, nullptr);
      }
      if (pipelines_[i].layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelines_[i].layout, nullptr);
      }
      if (pipelines_[i].renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, pipelines_[i].renderPass, nullptr);
      }
    }

    for (size_t i = 1; i < samplers_.size(); ++i) {
      if (samplers_[i].sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device_, samplers_[i].sampler, nullptr);
      }
    }

    for (size_t i = 1; i < textures_.size(); ++i) {
      if (textures_[i].view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, textures_[i].view, nullptr);
      }
    }

    for (size_t i = 1; i < shaders_.size(); ++i) {
      if (shaders_[i].module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, shaders_[i].module, nullptr);
      }
    }
  }

  if (allocator_ != nullptr) {
    for (size_t i = 1; i < textures_.size(); ++i) {
      if (textures_[i].image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, textures_[i].image, textures_[i].allocation);
      }
    }
    for (size_t i = 1; i < buffers_.size(); ++i) {
      if (buffers_[i].buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffers_[i].buffer,
                         buffers_[i].allocation);
      }
    }
  }

  cleanupSwapchain();

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
    }
    if (renderFinishedSemaphores_[i] != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
    }
    if (inFlightFences_[i] != VK_NULL_HANDLE) {
      vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }
  }

  if (descriptorPool_ != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
  }

  if (commandPool_ != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device_, commandPool_, nullptr);
  }

  if (allocator_) {
    vmaDestroyAllocator(allocator_);
    allocator_ = nullptr;
  }

  if (device_ != VK_NULL_HANDLE) {
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }

  if (surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }

  destroyDebugMessenger();

  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

const char *VulkanDevice::backend_name() const { return "Vulkan"; }

const Caps &VulkanDevice::caps() const { return caps_; }

BufferHandle VulkanDevice::createBuffer(const BufferDesc &desc) {
  if (desc.size == 0) {
    throw std::runtime_error("Vulkan buffer size must be greater than zero");
  }
  if (allocator_ == nullptr) {
    throw std::runtime_error("Vulkan allocator is not initialized");
  }

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = desc.size;
  bufferInfo.usage = toVkBufferUsage(desc.usage, desc.hostVisible);
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage =
      desc.hostVisible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
  if (desc.hostVisible) {
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
  }

  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = nullptr;
  VmaAllocationInfo allocationInfo{};
  if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &buffer, &allocation,
                      &allocationInfo) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan buffer");
  }

  BufferResource resource{};
  resource.buffer = buffer;
  resource.allocation = allocation;
  resource.allocationInfo = allocationInfo;
  resource.desc = desc;

  buffers_.push_back(resource);
  return BufferHandle{static_cast<uint32_t>(buffers_.size() - 1)};
}

TextureHandle VulkanDevice::createTexture(const TextureDesc &desc) {
  if (allocator_ == nullptr) {
    throw std::runtime_error("Vulkan allocator is not initialized");
  }

  VkFormat format = toVkFormat(desc.format);
  if (format == VK_FORMAT_UNDEFINED) {
    throw std::runtime_error("Unsupported Vulkan texture format");
  }

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = std::max<uint32_t>(1, desc.size.w);
  imageInfo.extent.height = std::max<uint32_t>(1, desc.size.h);
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = std::max<uint32_t>(1, desc.mipLevels);
  imageInfo.arrayLayers = std::max<uint32_t>(1, desc.layers);
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (imageInfo.mipLevels > 1) {
    imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if (desc.renderTarget) {
    if (isDepthFormat(desc.format)) {
      imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    } else {
      imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
  }

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = nullptr;
  if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation,
                     nullptr) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan image");
  }

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType =
      desc.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectMaskForFormat(desc.format);
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = imageInfo.arrayLayers;

  VkImageView view = VK_NULL_HANDLE;
  if (vkCreateImageView(device_, &viewInfo, nullptr, &view) != VK_SUCCESS) {
    vmaDestroyImage(allocator_, image, allocation);
    throw std::runtime_error("Failed to create Vulkan image view");
  }

  TextureResource resource{};
  resource.image = image;
  resource.view = view;
  resource.allocation = allocation;
  resource.desc = desc;
  resource.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  textures_.push_back(resource);
  return TextureHandle{static_cast<uint32_t>(textures_.size() - 1)};
}

SamplerHandle VulkanDevice::createSampler(const SamplerDesc &desc) {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = toVkFilter(desc.magFilter);
  samplerInfo.minFilter = toVkFilter(desc.minFilter);
  samplerInfo.mipmapMode = desc.minFilter == FilterMode::Nearest
                               ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                               : VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = toVkAddressMode(desc.addressU);
  samplerInfo.addressModeV = toVkAddressMode(desc.addressV);
  samplerInfo.addressModeW = toVkAddressMode(desc.addressW);
  samplerInfo.mipLodBias = desc.mipLodBias;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
  samplerInfo.borderColor = toVkBorderColor(desc);
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  if (desc.aniso && caps_.samplerAniso) {
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy =
        std::min(desc.maxAnisotropy, caps_.maxSamplerAnisotropy);
  } else {
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
  }

  if (desc.compareEnable) {
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = toVkCompareOp(desc.compareOp);
  } else {
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  }

  samplerInfo.maxAnisotropy = std::max(1.0f, samplerInfo.maxAnisotropy);

  VkSampler sampler = VK_NULL_HANDLE;
  if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan sampler");
  }

  SamplerResource resource{};
  resource.sampler = sampler;
  resource.desc = desc;

  samplers_.push_back(resource);
  return SamplerHandle{static_cast<uint32_t>(samplers_.size() - 1)};
}

ShaderHandle VulkanDevice::createShader(std::string_view,
                                        std::span<const uint8_t>) {
  throw std::runtime_error("Vulkan shader creation not implemented yet");
}

ShaderHandle VulkanDevice::createShaderFromBytecode(
    std::string_view stage, std::span<const uint8_t> bytes) {
  if (bytes.empty() || (bytes.size() % sizeof(uint32_t)) != 0) {
    throw std::runtime_error(
        "Vulkan shader bytecode must be non-empty and aligned to 4 bytes");
  }
  if (device_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan device not initialized for shader creation");
  }

  VkShaderStageFlagBits stageFlag = shaderStageFromLabel(stage);
  if (stageFlag == VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM) {
    throw std::runtime_error("Unsupported Vulkan shader stage label: " +
                             std::string(stage));
  }

  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = bytes.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(bytes.data());

  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device_, &createInfo, nullptr, &module) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan shader module");
  }

  ShaderResource resource{};
  resource.module = module;
  resource.stage = stageFlag;
  resource.stageLabel = std::string(stage);
  resource.instanced = stageUsesInstancing(stage);
  resource.isCompute = (stageFlag == VK_SHADER_STAGE_COMPUTE_BIT);

  shaders_.push_back(resource);
  return ShaderHandle{static_cast<uint32_t>(shaders_.size() - 1)};
}

PipelineHandle VulkanDevice::createPipeline(const PipelineDesc &desc) {
  if (device_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Vulkan device not initialized for pipeline creation");
  }

  PipelineResource resource{};

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = 0;
  layoutInfo.pSetLayouts = nullptr;
  layoutInfo.pushConstantRangeCount = 0;
  layoutInfo.pPushConstantRanges = nullptr;

  if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &resource.layout) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan pipeline layout");
  }

  auto getShaderResource = [&](ShaderHandle handle) -> const ShaderResource * {
    if (handle.id == 0 || handle.id >= shaders_.size()) {
      return nullptr;
    }
    return &shaders_[handle.id];
  };

  if (desc.cs.id != 0) {
    const ShaderResource *cs = getShaderResource(desc.cs);
    if (!cs || cs->stage != VK_SHADER_STAGE_COMPUTE_BIT) {
      vkDestroyPipelineLayout(device_, resource.layout, nullptr);
      throw std::runtime_error("Invalid compute shader handle for Vulkan pipeline");
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = cs->module;
    stage.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = resource.layout;

    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                 nullptr, &resource.pipeline) != VK_SUCCESS) {
      vkDestroyPipelineLayout(device_, resource.layout, nullptr);
      throw std::runtime_error("Failed to create Vulkan compute pipeline");
    }

    resource.isCompute = true;
    pipelines_.push_back(resource);
    return PipelineHandle{static_cast<uint32_t>(pipelines_.size() - 1)};
  }

  const ShaderResource *vs = getShaderResource(desc.vs);
  const ShaderResource *fs = getShaderResource(desc.fs);
  if (!vs || vs->stage != VK_SHADER_STAGE_VERTEX_BIT) {
    vkDestroyPipelineLayout(device_, resource.layout, nullptr);
    throw std::runtime_error("Invalid vertex shader handle for Vulkan pipeline");
  }
  if (fs && fs->stage != VK_SHADER_STAGE_FRAGMENT_BIT) {
    fs = nullptr;
  }

  std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
  VkPipelineShaderStageCreateInfo vsStage{};
  vsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vsStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vsStage.module = vs->module;
  vsStage.pName = "main";
  shaderStages.push_back(vsStage);

  VkPipelineShaderStageCreateInfo fsStage{};
  if (fs) {
    fsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fsStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fsStage.module = fs->module;
    fsStage.pName = "main";
    shaderStages.push_back(fsStage);
  }

  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;

  VkVertexInputBindingDescription vertexBinding{};
  vertexBinding.binding = 0;
  vertexBinding.stride = 48; // sizeof(Vertex)
  vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  bindings.push_back(vertexBinding);

  VkVertexInputAttributeDescription attr{};
  attr.binding = 0;
  attr.location = 0;
  attr.format = VK_FORMAT_R32G32B32_SFLOAT;
  attr.offset = 0;
  attributes.push_back(attr);

  attr.location = 1;
  attr.offset = 12;
  attributes.push_back(attr);

  attr.location = 2;
  attr.format = VK_FORMAT_R32G32_SFLOAT;
  attr.offset = 24;
  attributes.push_back(attr);

  attr.location = 3;
  attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attr.offset = 32;
  attributes.push_back(attr);

  if (vs->instanced) {
    VkVertexInputBindingDescription instanceBinding{};
    instanceBinding.binding = 1;
    instanceBinding.stride = 68; // sizeof(InstanceGPUData)
    instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    bindings.push_back(instanceBinding);

    VkVertexInputAttributeDescription instAttr{};
    instAttr.binding = 1;
    instAttr.format = VK_FORMAT_R32G32B32_SFLOAT;

    instAttr.location = 4;
    instAttr.offset = 0;
    attributes.push_back(instAttr);

    instAttr.location = 5;
    instAttr.offset = 12;
    attributes.push_back(instAttr);

    instAttr.location = 6;
    instAttr.offset = 24;
    attributes.push_back(instAttr);

    instAttr.location = 7;
    instAttr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    instAttr.offset = 36;
    attributes.push_back(instAttr);

    instAttr.location = 8;
    instAttr.format = VK_FORMAT_R32_SFLOAT;
    instAttr.offset = 52;
    attributes.push_back(instAttr);

    instAttr.location = 9;
    instAttr.offset = 60;
    attributes.push_back(instAttr);
  }

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount =
      static_cast<uint32_t>(bindings.size());
  vertexInput.pVertexBindingDescriptions = bindings.data();
  vertexInput.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributes.size());
  vertexInput.pVertexAttributeDescriptions = attributes.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(swapchainExtent_.width);
  viewport.height = static_cast<float>(swapchainExtent_.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapchainExtent_;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.sampleShadingEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_FALSE;
  depthStencil.depthWriteEnable = VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  uint32_t colorAttachmentCount = desc.colorAttachmentCount;
  if (colorAttachmentCount == 0) {
    colorAttachmentCount = 1;
  }
  colorAttachmentCount =
      std::min(colorAttachmentCount, static_cast<uint32_t>(kMaxColorAttachments));

  std::vector<VkAttachmentDescription> attachments;
  attachments.reserve(colorAttachmentCount);
  std::vector<VkAttachmentReference> colorAttachmentRefs;
  colorAttachmentRefs.reserve(colorAttachmentCount);
  std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
  colorBlendAttachments.reserve(colorAttachmentCount);

  for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
    Format format = Format::BGRA8;
    BlendState blend = make_alpha_blend_state();
    if (i < desc.colorAttachmentCount) {
      format = desc.colorAttachments[i].format;
      blend = desc.colorAttachments[i].blend;
    }

    VkAttachmentDescription attachment{};
    attachment.format = toVkFormat(format);
    if (attachment.format == VK_FORMAT_UNDEFINED) {
      attachment.format = swapchainImageFormat_;
    }
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments.push_back(attachment);

    VkAttachmentReference reference{};
    reference.attachment = i;
    reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentRefs.push_back(reference);

    VkPipelineColorBlendAttachmentState blendState{};
    blendState.blendEnable = blend.enabled ? VK_TRUE : VK_FALSE;
    blendState.srcColorBlendFactor = toVkBlendFactor(blend.srcColor);
    blendState.dstColorBlendFactor = toVkBlendFactor(blend.dstColor);
    blendState.colorBlendOp = toVkBlendOp(blend.colorOp);
    blendState.srcAlphaBlendFactor = toVkBlendFactor(blend.srcAlpha);
    blendState.dstAlphaBlendFactor = toVkBlendFactor(blend.dstAlpha);
    blendState.alphaBlendOp = toVkBlendOp(blend.alphaOp);
    blendState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments.push_back(blendState);
  }

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount =
      static_cast<uint32_t>(colorAttachmentRefs.size());
  subpass.pColorAttachments = colorAttachmentRefs.data();

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;

  if (vkCreateRenderPass(device_, &renderPassInfo, nullptr,
                         &resource.renderPass) != VK_SUCCESS) {
    vkDestroyPipelineLayout(device_, resource.layout, nullptr);
    throw std::runtime_error("Failed to create Vulkan render pass");
  }

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount =
      static_cast<uint32_t>(colorBlendAttachments.size());
  colorBlending.pAttachments = colorBlendAttachments.data();

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
  pipelineInfo.pStages = shaderStages.data();
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = resource.layout;
  pipelineInfo.renderPass = resource.renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &resource.pipeline) != VK_SUCCESS) {
    vkDestroyRenderPass(device_, resource.renderPass, nullptr);
    vkDestroyPipelineLayout(device_, resource.layout, nullptr);
    throw std::runtime_error("Failed to create Vulkan graphics pipeline");
  }

  pipelines_.push_back(resource);
  return PipelineHandle{static_cast<uint32_t>(pipelines_.size() - 1)};
}

FramebufferHandle VulkanDevice::createFramebuffer(const FramebufferDesc &) {
  throw std::runtime_error("Vulkan framebuffer creation not implemented yet");
}

QueryHandle VulkanDevice::createQuery(QueryType) {
  throw std::runtime_error("Vulkan queries are not implemented yet");
}

void VulkanDevice::destroyQuery(QueryHandle) {}

bool VulkanDevice::getQueryResult(QueryHandle, uint64_t &, bool) { return false; }

FenceHandle VulkanDevice::createFence(bool) {
  throw std::runtime_error("Vulkan fence creation not implemented yet");
}

void VulkanDevice::destroyFence(FenceHandle) {}

void VulkanDevice::waitFence(FenceHandle, uint64_t) {}

void VulkanDevice::resetFence(FenceHandle) {}

CmdList *VulkanDevice::getImmediate() { return immediateCmdList_.get(); }

void VulkanDevice::present() {
  beginFrameIfNeeded();

  VkSemaphore waitSemaphores[] = {
      imageAvailableSemaphores_[currentFrame_],
  };
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  VkCommandBuffer cmd = commandBuffers_[currentFrame_];

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores =
      &renderFinishedSemaphores_[currentFrame_];

  if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo,
                    inFlightFences_[currentFrame_]) != VK_SUCCESS) {
    throw std::runtime_error("Failed to submit draw command buffer");
  }

  VkSwapchainKHR swapchains[] = {swapchain_};
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[currentFrame_];
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapchains;
  presentInfo.pImageIndices = &currentImageIndex_;

  VkResult result = vkQueuePresentKHR(presentQueue_, &presentInfo);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    recreateSwapchain();
  } else if (result != VK_SUCCESS) {
    throw std::runtime_error("Failed to present Vulkan swapchain image");
  }

  finishFrame();
  currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

void VulkanDevice::readBuffer(BufferHandle, void *, size_t, size_t) {
  throw std::runtime_error("Vulkan readBuffer not implemented yet");
}

VkCommandBuffer VulkanDevice::currentCommandBuffer() const {
  if (!frameActive_) {
    return VK_NULL_HANDLE;
  }
  return commandBuffers_[currentFrame_];
}

void VulkanDevice::beginFrameIfNeeded() {
  if (frameActive_) {
    return;
  }

  VkFence fence = inFlightFences_[currentFrame_];
  vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

  uint32_t imageIndex = 0;
  while (true) {
    VkResult result = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE,
        &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      recreateSwapchain();
      continue;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      throw std::runtime_error("Failed to acquire Vulkan swapchain image");
    }
    currentImageIndex_ = imageIndex;
    break;
  }

  if (!imagesInFlight_.empty() &&
      imagesInFlight_[currentImageIndex_] != VK_NULL_HANDLE) {
    vkWaitForFences(device_, 1, &imagesInFlight_[currentImageIndex_], VK_TRUE,
                    UINT64_MAX);
  }
  if (!imagesInFlight_.empty()) {
    imagesInFlight_[currentImageIndex_] = inFlightFences_[currentFrame_];
  }

  vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

  VkCommandBuffer cmd = commandBuffers_[currentFrame_];
  vkResetCommandBuffer(cmd, 0);

  frameActive_ = true;
}

void VulkanDevice::finishFrame() {
  frameActive_ = false;
}

void VulkanDevice::createInstance() {
  if (kEnableValidationLayers && !checkValidationLayerSupport()) {
    throw std::runtime_error("Validation layers requested but unavailable");
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Pixel";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "Pixel";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_1;

  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions =
      glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
  if (!glfwExtensions || glfwExtensionCount == 0) {
    throw std::runtime_error("GLFW did not return Vulkan WSI extensions");
  }

  std::vector<const char *> extensions(glfwExtensions,
                                       glfwExtensions + glfwExtensionCount);
  if (kEnableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (kEnableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(kValidationLayers.size());
    createInfo.ppEnabledLayerNames = kValidationLayers.data();

    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = &debugCreateInfo;
  }

  if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan instance");
  }
}

void VulkanDevice::setupDebugMessenger() {
  if (!kEnableValidationLayers) {
    return;
  }

  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  populateDebugMessengerCreateInfo(createInfo);

  if (createDebugUtilsMessengerEXT(instance_, &createInfo, nullptr,
                                   &debugMessenger_) != VK_SUCCESS) {
    throw std::runtime_error("Failed to set up Vulkan debug messenger");
  }
}

void VulkanDevice::createSurface(GLFWwindow *window) {
  if (glfwCreateWindowSurface(instance_, window, nullptr, &surface_) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan surface");
  }
}

void VulkanDevice::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
  if (deviceCount == 0) {
    throw std::runtime_error("Failed to find GPUs with Vulkan support");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

  for (const auto &device : devices) {
    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete()) {
      continue;
    }

    if (!checkDeviceExtensionSupport(device)) {
      continue;
    }

    SwapchainSupportDetails swapchainSupport = querySwapchainSupport(device);
    if (swapchainSupport.formats.empty() ||
        swapchainSupport.presentModes.empty()) {
      continue;
    }

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(device, &features);

    physicalDevice_ = device;
    graphicsQueueFamilyIndex_ = indices.graphicsFamily.value();
    presentQueueFamilyIndex_ = indices.presentFamily.value();

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    caps_.samplerAniso = features.samplerAnisotropy == VK_TRUE;
    caps_.maxSamplerAnisotropy =
        caps_.samplerAniso ? properties.limits.maxSamplerAnisotropy : 1.0f;
    break;
  }

  if (physicalDevice_ == VK_NULL_HANDLE) {
    throw std::runtime_error("Failed to find a suitable Vulkan GPU");
  }
}

void VulkanDevice::createLogicalDevice() {
  std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamilyIndex_,
                                            presentQueueFamilyIndex_};

  float queuePriority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkPhysicalDeviceFeatures deviceFeatures{};
  if (caps_.samplerAniso) {
    deviceFeatures.samplerAnisotropy = VK_TRUE;
  }

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.pEnabledFeatures = &deviceFeatures;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(
      kDeviceExtensions.size());
  createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

  if (kEnableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(kValidationLayers.size());
    createInfo.ppEnabledLayerNames = kValidationLayers.data();
  }

  if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan logical device");
  }

  vkGetDeviceQueue(device_, graphicsQueueFamilyIndex_, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, presentQueueFamilyIndex_, 0, &presentQueue_);
}

void VulkanDevice::createAllocator() {
  VmaAllocatorCreateInfo info{};
  info.instance = instance_;
  info.physicalDevice = physicalDevice_;
  info.device = device_;

  if (vmaCreateAllocator(&info, &allocator_) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan memory allocator");
  }
}

VulkanDevice::SwapchainSupportDetails
VulkanDevice::querySwapchainSupport(VkPhysicalDevice device) const {
  SwapchainSupportDetails details{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_,
                                            &details.capabilities);

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
  if (formatCount != 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount,
                                         details.formats.data());
  }

  uint32_t presentModeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_,
                                            &presentModeCount, nullptr);
  if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface_, &presentModeCount, details.presentModes.data());
  }

  return details;
}

VulkanDevice::QueueFamilyIndices
VulkanDevice::findQueueFamilies(VkPhysicalDevice device) const {
  QueueFamilyIndices indices{};

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                           queueFamilies.data());

  uint32_t i = 0;
  for (const auto &queueFamily : queueFamilies) {
    if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
        !indices.graphicsFamily.has_value()) {
      indices.graphicsFamily = i;
    }

    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
    if (presentSupport && !indices.presentFamily.has_value()) {
      indices.presentFamily = i;
    }

    if (indices.isComplete()) {
      break;
    }
    ++i;
  }

  return indices;
}

bool VulkanDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) const {
  uint32_t extensionCount = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       nullptr);

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       availableExtensions.data());

  std::set<std::string> requiredExtensions(kDeviceExtensions.begin(),
                                           kDeviceExtensions.end());

  for (const auto &extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}

VkSurfaceFormatKHR VulkanDevice::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> &availableFormats) const {
  for (const auto &availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return availableFormat;
    }
  }

  return availableFormats.front();
}

VkPresentModeKHR VulkanDevice::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR> &availablePresentModes) const {
  for (const auto &availablePresentMode : availablePresentModes) {
    if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return availablePresentMode;
    }
  }

  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanDevice::chooseSwapExtent(
    const VkSurfaceCapabilitiesKHR &capabilities, GLFWwindow *window) const {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);

  VkExtent2D actualExtent = {
      static_cast<uint32_t>(width),
      static_cast<uint32_t>(height),
  };

  actualExtent.width = std::max(capabilities.minImageExtent.width,
                                std::min(capabilities.maxImageExtent.width,
                                         actualExtent.width));
  actualExtent.height = std::max(capabilities.minImageExtent.height,
                                 std::min(capabilities.maxImageExtent.height,
                                          actualExtent.height));

  return actualExtent;
}

void VulkanDevice::createSwapchain() {
  SwapchainSupportDetails swapchainSupport =
      querySwapchainSupport(physicalDevice_);

  VkSurfaceFormatKHR surfaceFormat =
      chooseSwapSurfaceFormat(swapchainSupport.formats);
  VkPresentModeKHR presentMode =
      chooseSwapPresentMode(swapchainSupport.presentModes);
  VkExtent2D extent = chooseSwapExtent(swapchainSupport.capabilities, window_);

  uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
  if (swapchainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapchainSupport.capabilities.maxImageCount) {
    imageCount = swapchainSupport.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface_;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t queueFamilyIndices[] = {graphicsQueueFamilyIndex_,
                                   presentQueueFamilyIndex_};

  if (graphicsQueueFamilyIndex_ != presentQueueFamilyIndex_) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = nullptr;
  }

  createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = swapchain_;

  if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan swapchain");
  }

  vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
  swapchainImages_.resize(imageCount);
  vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount,
                          swapchainImages_.data());

  swapchainImageFormat_ = surfaceFormat.format;
  swapchainExtent_ = extent;
  imagesInFlight_.resize(imageCount, VK_NULL_HANDLE);
}

void VulkanDevice::createImageViews() {
  swapchainImageViews_.resize(swapchainImages_.size());

  for (size_t i = 0; i < swapchainImages_.size(); ++i) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = swapchainImages_[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = swapchainImageFormat_;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &createInfo, nullptr,
                          &swapchainImageViews_[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create Vulkan image view");
    }
  }
}

void VulkanDevice::createCommandPool() {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex_;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan command pool");
  }
}

void VulkanDevice::allocateCommandBuffers() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool_;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(kMaxFramesInFlight);

  if (vkAllocateCommandBuffers(device_, &allocInfo,
                               commandBuffers_.data()) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate Vulkan command buffers");
  }
}

void VulkanDevice::createSyncObjects() {
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                          &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                          &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
        vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) !=
            VK_SUCCESS) {
      throw std::runtime_error(
          "Failed to create Vulkan synchronization primitives");
    }
  }
}

void VulkanDevice::createDescriptorPool() {
  std::array<VkDescriptorPoolSize, 3> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = 32;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = 32;
  poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSizes[2].descriptorCount = 16;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = 64;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

  if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan descriptor pool");
  }
}

void VulkanDevice::cleanupSwapchain() {
  for (VkImageView imageView : swapchainImageViews_) {
    if (imageView != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, imageView, nullptr);
    }
  }
  swapchainImageViews_.clear();
  swapchainImages_.clear();
  imagesInFlight_.clear();

  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

void VulkanDevice::recreateSwapchain() {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_, &width, &height);
  while (width == 0 || height == 0) {
    glfwWaitEvents();
    glfwGetFramebufferSize(window_, &width, &height);
  }

  vkDeviceWaitIdle(device_);

  cleanupSwapchain();
  createSwapchain();
  createImageViews();
}

void VulkanDevice::destroyDebugMessenger() {
  if (kEnableValidationLayers && debugMessenger_ != VK_NULL_HANDLE) {
    destroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    debugMessenger_ = VK_NULL_HANDLE;
  }
}

Device *create_vulkan_device(void *window_handle) {
  if (!window_handle) {
    throw std::invalid_argument("Window handle for Vulkan device cannot be null");
  }

  auto *window = static_cast<GLFWwindow *>(window_handle);
  return new VulkanDevice(window);
}

} // namespace pixel::rhi

