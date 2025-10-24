#include "device_vk.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
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

BufferHandle VulkanDevice::createBuffer(const BufferDesc &) {
  throw std::runtime_error("Vulkan buffer creation not implemented yet");
}

TextureHandle VulkanDevice::createTexture(const TextureDesc &) {
  throw std::runtime_error("Vulkan texture creation not implemented yet");
}

SamplerHandle VulkanDevice::createSampler(const SamplerDesc &) {
  throw std::runtime_error("Vulkan sampler creation not implemented yet");
}

ShaderHandle VulkanDevice::createShader(std::string_view,
                                        std::span<const uint8_t>) {
  throw std::runtime_error("Vulkan shader creation not implemented yet");
}

PipelineHandle VulkanDevice::createPipeline(const PipelineDesc &) {
  throw std::runtime_error("Vulkan pipeline creation not implemented yet");
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

