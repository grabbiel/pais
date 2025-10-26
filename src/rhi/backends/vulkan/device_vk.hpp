#pragma once

#include "pixel/rhi/rhi.hpp"

#include "vk_mem_alloc.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pixel::rhi {

class VulkanCmdList;

class VulkanDevice final : public Device {
public:
  explicit VulkanDevice(GLFWwindow *window);
  ~VulkanDevice() override;

  const char *backend_name() const override;
  const Caps &caps() const override;

  BufferHandle createBuffer(const BufferDesc &) override;
  TextureHandle createTexture(const TextureDesc &) override;
  SamplerHandle createSampler(const SamplerDesc &) override;
  ShaderHandle createShader(std::string_view stage,
                            std::span<const uint8_t> bytes) override;
  ShaderHandle createShaderFromBytecode(std::string_view stage,
                                        std::span<const uint8_t> bytes) override;
  PipelineHandle createPipeline(const PipelineDesc &) override;
  FramebufferHandle createFramebuffer(const FramebufferDesc &) override;
  QueryHandle createQuery(QueryType type) override;
  void destroyQuery(QueryHandle handle) override;
  bool getQueryResult(QueryHandle handle, uint64_t &result,
                      bool wait) override;
  FenceHandle createFence(bool signaled) override;
  void destroyFence(FenceHandle handle) override;
  void waitFence(FenceHandle handle, uint64_t timeout_ns) override;
  void resetFence(FenceHandle handle) override;

  CmdList *getImmediate() override;
  void present() override;

  void readBuffer(BufferHandle handle, void *dst, size_t size,
                  size_t offset) override;

  // Internal helpers accessed by the command list implementation.
  VkDevice vkDevice() const { return device_; }
  VkCommandBuffer currentCommandBuffer() const;
  VkExtent2D swapchainExtent() const { return swapchainExtent_; }
  VkFormat swapchainFormat() const { return swapchainImageFormat_; }
  VkRenderPass swapchainRenderPass() const { return VK_NULL_HANDLE; }
  VmaAllocator allocator() const { return allocator_; }

  void beginFrameIfNeeded();
  void finishFrame();

private:
  friend class VulkanCmdList;

  struct BufferResource {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{nullptr};
    VmaAllocationInfo allocationInfo{};
    BufferDesc desc{};
  };

  struct TextureResource {
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VmaAllocation allocation{nullptr};
    TextureDesc desc{};
    VkImageLayout currentLayout{VK_IMAGE_LAYOUT_UNDEFINED};
  };

  struct SamplerResource {
    VkSampler sampler{VK_NULL_HANDLE};
    SamplerDesc desc{};
  };

  struct ShaderResource {
    VkShaderModule module{VK_NULL_HANDLE};
    VkShaderStageFlagBits stage{VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM};
    std::string stageLabel{};
    bool instanced{false};
    bool isCompute{false};
  };

  struct PipelineResource {
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
    VkRenderPass renderPass{VK_NULL_HANDLE};
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{};
    bool isCompute{false};
  };

  struct FenceResource {
    VkFence fence{VK_NULL_HANDLE};
  };

  void createInstance();
  void setupDebugMessenger();
  void createSurface(GLFWwindow *window);
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createAllocator();
  void createSwapchain();
  void createImageViews();
  void createCommandPool();
  void allocateCommandBuffers();
  void createSyncObjects();
  void createDescriptorPool();
  void cleanupSwapchain();
  void recreateSwapchain();
  VkFence fenceFromHandle(FenceHandle handle) const;

  struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
      return graphicsFamily.has_value() && presentFamily.has_value();
    }
  };

  struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
  };

  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
  SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device) const;
  bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR> &formats) const;
  VkPresentModeKHR chooseSwapPresentMode(
      const std::vector<VkPresentModeKHR> &presentModes) const;
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities,
                              GLFWwindow *window) const;

  void destroyDebugMessenger();

private:
  Caps caps_{};

  VkInstance instance_{VK_NULL_HANDLE};
  GLFWwindow *window_{nullptr};
  VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue graphicsQueue_{VK_NULL_HANDLE};
  VkQueue presentQueue_{VK_NULL_HANDLE};
  uint32_t graphicsQueueFamilyIndex_{0};
  uint32_t presentQueueFamilyIndex_{0};

  VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
  VkFormat swapchainImageFormat_{VK_FORMAT_B8G8R8A8_SRGB};
  VkExtent2D swapchainExtent_{};
  std::vector<VkImage> swapchainImages_{};
  std::vector<VkImageView> swapchainImageViews_{};

  VkCommandPool commandPool_{VK_NULL_HANDLE};
  static constexpr size_t kMaxFramesInFlight = 2;
  std::array<VkCommandBuffer, kMaxFramesInFlight> commandBuffers_{};
  std::array<VkSemaphore, kMaxFramesInFlight> imageAvailableSemaphores_{};
  std::array<VkSemaphore, kMaxFramesInFlight> renderFinishedSemaphores_{};
  std::array<VkFence, kMaxFramesInFlight> inFlightFences_{};
  std::array<VkFence, kMaxFramesInFlight> frameFences_{};
  std::vector<VkFence> imagesInFlight_{};
  size_t currentFrame_{0};
  uint32_t currentImageIndex_{0};
  bool frameActive_{false};

  VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};
  VmaAllocator allocator_{nullptr};

  std::unique_ptr<VulkanCmdList> immediateCmdList_{};

  std::vector<BufferResource> buffers_{1};
  std::vector<TextureResource> textures_{1};
  std::vector<SamplerResource> samplers_{1};
  std::vector<ShaderResource> shaders_{1};
  std::vector<PipelineResource> pipelines_{1};
  std::vector<FenceResource> fences_{1};
};

class VulkanCmdList final : public CmdList {
public:
  explicit VulkanCmdList(VulkanDevice &device);
  ~VulkanCmdList() override = default;

  void begin() override;
  void beginRender(const RenderPassDesc &desc) override;
  void setPipeline(PipelineHandle) override;
  void setVertexBuffer(BufferHandle, size_t) override;
  void setIndexBuffer(BufferHandle, size_t) override;
  void setInstanceBuffer(BufferHandle, size_t, size_t) override;

  void setDepthStencilState(const DepthStencilState &) override;
  void setDepthBias(const DepthBiasState &) override;

  void setUniformMat4(const char *name, const float *mat4x4) override;
  void setUniformVec3(const char *name, const float *vec3) override;
  void setUniformVec4(const char *name, const float *vec4) override;
  void setUniformInt(const char *name, int value) override;
  void setUniformFloat(const char *name, float value) override;

  void setUniformBuffer(uint32_t binding, BufferHandle buffer,
                        size_t offset, size_t size) override;

  void setTexture(const char *name, TextureHandle texture, uint32_t slot,
                  SamplerHandle sampler) override;
  void copyToTexture(TextureHandle texture, uint32_t mipLevel,
                     std::span<const std::byte> data) override;
  void copyToTextureLayer(TextureHandle texture, uint32_t layer,
                          uint32_t mipLevel,
                          std::span<const std::byte> data) override;

  void setComputePipeline(PipelineHandle) override;
  void setStorageBuffer(uint32_t binding, BufferHandle buffer,
                        size_t offset, size_t size) override;
  void dispatch(uint32_t groupCountX, uint32_t groupCountY,
                uint32_t groupCountZ) override;
  void memoryBarrier() override;
  void resourceBarrier(
      std::span<const ResourceBarrierDesc> barriers) override;

  void beginQuery(QueryHandle handle, QueryType type) override;
  void endQuery(QueryHandle handle, QueryType type) override;
  void signalFence(FenceHandle handle) override;

  void drawIndexed(uint32_t indexCount, uint32_t firstIndex,
                   uint32_t instanceCount) override;
  void endRender() override;
  void copyToBuffer(BufferHandle, size_t, std::span<const std::byte>) override;
  void end() override;

private:
  [[noreturn]] void notImplemented(const char *feature) const;

  void resetDescriptorState();
  void ensurePixelUniformResources();
  void uploadPixelUniformsIfNeeded();
  void ensureDescriptorSetForCurrentPipeline();
  void bindDescriptorSetIfNeeded();
  SamplerHandle ensureDefaultSampler();

  friend class VulkanDevice;

  std::optional<FenceHandle> takePendingFence();

  VulkanDevice &device_;
  VkCommandBuffer activeCommandBuffer_{VK_NULL_HANDLE};
  RenderPassDesc pendingRenderPass_{};
  bool renderPassPending_{false};
  bool renderPassActive_{false};
  VkFramebuffer activeFramebuffer_{VK_NULL_HANDLE};
  PipelineHandle currentGraphicsPipeline_{};
  PipelineHandle currentComputePipeline_{};
  std::optional<FenceHandle> pendingFence_{};

  struct PixelUniformBufferData;
  struct BoundUniformBuffer {
    BufferHandle handle{};
    size_t offset{0};
    size_t size{0};
    VkDescriptorBufferInfo bufferInfo{};
  };

  struct BoundTexture {
    TextureHandle handle{};
    SamplerHandle sampler{};
    VkDescriptorImageInfo imageInfo{};
  };

  static constexpr uint32_t kPixelUniformBinding = 1;

  PixelUniformBufferData pixelUniforms_{};
  bool pixelUniformsDirty_{false};
  BufferHandle pixelUniformBuffer_{};
  void *pixelUniformMapped_{nullptr};
  SamplerHandle defaultSampler_{};

  VkDescriptorSet currentDescriptorSet_{VK_NULL_HANDLE};
  PipelineHandle descriptorSetPipeline_{};
  bool descriptorSetDirty_{true};

  std::unordered_map<uint32_t, BoundUniformBuffer> uniformBuffers_{};
  std::unordered_map<uint32_t, BoundTexture> boundTextures_{};
};

} // namespace pixel::rhi

