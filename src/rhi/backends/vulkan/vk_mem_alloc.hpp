#pragma once

// Minimal Vulkan Memory Allocator (VMA) style interface used by the Pixel
// Vulkan backend. This is **not** a full implementation of the official VMA
// project but rather a small, self-contained subset that provides a similar
// API surface so that the renderer can evolve without pulling the large
// dependency at this stage of development. The implementation focuses on the
// functionality required by the Phase 2.5 milestone: creating buffers and
// images backed by device memory with optional host visibility.
//
// The interface is intentionally compatible with the most common VMA entry
// points used across graphics samples: allocator creation/destruction,
// resource allocation helpers and basic mapping utilities. If additional VMA
// features are required in the future the header can either be replaced with
// the official release or extended to mirror the desired behaviour.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace pixel::vma {

struct Allocation;
struct Allocator;

using VmaAllocator = Allocator *;
using VmaAllocation = Allocation *;

struct VmaAllocatorCreateInfo {
  VkInstance instance{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
  VkDevice device{VK_NULL_HANDLE};
};

struct VmaAllocationCreateInfo {
  VkMemoryPropertyFlags requiredFlags{0};
  VkMemoryPropertyFlags preferredFlags{0};
  VkBufferUsageFlags bufferUsage{0};
  VkImageUsageFlags imageUsage{0};
};

struct Allocation {
  VkDevice device{VK_NULL_HANDLE};
  VkDeviceMemory memory{VK_NULL_HANDLE};
  VkDeviceSize size{0};
  void *mapped{nullptr};
};

struct Allocator {
  VkInstance instance{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
  VkDevice device{VK_NULL_HANDLE};
};

inline uint32_t find_memory_type(const Allocator &allocator,
                                 uint32_t type_filter,
                                 VkMemoryPropertyFlags required,
                                 VkMemoryPropertyFlags preferred) {
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(allocator.physicalDevice, &props);

  // Try to find a memory type that satisfies both required and preferred flags.
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((type_filter & (1u << i)) == 0) {
      continue;
    }

    VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
    if ((flags & required) == required && (flags & preferred) == preferred) {
      return i;
    }
  }

  // Fall back to any memory type that satisfies the required flags.
  for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((type_filter & (1u << i)) == 0) {
      continue;
    }

    VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
    if ((flags & required) == required) {
      return i;
    }
  }

  throw std::runtime_error("Failed to find suitable Vulkan memory type");
}

inline VkResult vmaCreateAllocator(const VmaAllocatorCreateInfo *info,
                                   VmaAllocator *allocator) {
  if (!info || !allocator) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  auto *result = new Allocator;
  result->instance = info->instance;
  result->physicalDevice = info->physicalDevice;
  result->device = info->device;

  *allocator = result;
  return VK_SUCCESS;
}

inline void vmaDestroyAllocator(VmaAllocator allocator) {
  delete allocator;
}

inline VkResult allocate_memory(const Allocator &allocator,
                                const VkMemoryRequirements &requirements,
                                const VmaAllocationCreateInfo &alloc_info,
                                Allocation **allocation) {
  VkMemoryAllocateInfo alloc{};
  alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc.allocationSize = requirements.size;
  alloc.memoryTypeIndex =
      find_memory_type(allocator, requirements.memoryTypeBits,
                       alloc_info.requiredFlags, alloc_info.preferredFlags);

  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkResult result = vkAllocateMemory(allocator.device, &alloc, nullptr, &memory);
  if (result != VK_SUCCESS) {
    return result;
  }

  auto *out_allocation = new Allocation;
  out_allocation->device = allocator.device;
  out_allocation->memory = memory;
  out_allocation->size = requirements.size;

  *allocation = out_allocation;
  return VK_SUCCESS;
}

inline void vmaFreeMemory(const Allocator &, VmaAllocation allocation) {
  if (!allocation) {
    return;
  }

  vkFreeMemory(allocation->device, allocation->memory, nullptr);
  delete allocation;
}

inline VkResult vmaCreateBuffer(VmaAllocator allocator,
                                const VkBufferCreateInfo *buffer_info,
                                const VmaAllocationCreateInfo *alloc_info,
                                VkBuffer *buffer,
                                VmaAllocation *allocation,
                                void *) {
  if (!allocator || !buffer_info || !alloc_info || !buffer || !allocation) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkResult result = vkCreateBuffer(allocator->device, buffer_info, nullptr, buffer);
  if (result != VK_SUCCESS) {
    return result;
  }

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(allocator->device, *buffer, &requirements);

  Allocation *buffer_allocation = nullptr;
  result = allocate_memory(*allocator, requirements, *alloc_info, &buffer_allocation);
  if (result != VK_SUCCESS) {
    vkDestroyBuffer(allocator->device, *buffer, nullptr);
    return result;
  }

  result = vkBindBufferMemory(allocator->device, *buffer,
                              buffer_allocation->memory, 0);
  if (result != VK_SUCCESS) {
    vmaFreeMemory(*allocator, buffer_allocation);
    vkDestroyBuffer(allocator->device, *buffer, nullptr);
    return result;
  }

  *allocation = buffer_allocation;
  return VK_SUCCESS;
}

inline void vmaDestroyBuffer(VmaAllocator allocator, VkBuffer buffer,
                             VmaAllocation allocation) {
  if (!allocator) {
    return;
  }

  vkDestroyBuffer(allocator->device, buffer, nullptr);
  vmaFreeMemory(*allocator, allocation);
}

inline VkResult vmaCreateImage(VmaAllocator allocator,
                               const VkImageCreateInfo *image_info,
                               const VmaAllocationCreateInfo *alloc_info,
                               VkImage *image,
                               VmaAllocation *allocation,
                               void *) {
  if (!allocator || !image_info || !alloc_info || !image || !allocation) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkResult result = vkCreateImage(allocator->device, image_info, nullptr, image);
  if (result != VK_SUCCESS) {
    return result;
  }

  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(allocator->device, *image, &requirements);

  Allocation *image_allocation = nullptr;
  result = allocate_memory(*allocator, requirements, *alloc_info, &image_allocation);
  if (result != VK_SUCCESS) {
    vkDestroyImage(allocator->device, *image, nullptr);
    return result;
  }

  result = vkBindImageMemory(allocator->device, *image,
                             image_allocation->memory, 0);
  if (result != VK_SUCCESS) {
    vmaFreeMemory(*allocator, image_allocation);
    vkDestroyImage(allocator->device, *image, nullptr);
    return result;
  }

  *allocation = image_allocation;
  return VK_SUCCESS;
}

inline void vmaDestroyImage(VmaAllocator allocator, VkImage image,
                            VmaAllocation allocation) {
  if (!allocator) {
    return;
  }

  vkDestroyImage(allocator->device, image, nullptr);
  vmaFreeMemory(*allocator, allocation);
}

inline VkResult vmaMapMemory(VmaAllocator, VmaAllocation allocation,
                             void **data) {
  if (!allocation || !data) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  VkResult result = vkMapMemory(allocation->device, allocation->memory, 0,
                                allocation->size, 0, data);
  if (result == VK_SUCCESS) {
    allocation->mapped = *data;
  }
  return result;
}

inline void vmaUnmapMemory(VmaAllocator, VmaAllocation allocation) {
  if (!allocation || !allocation->mapped) {
    return;
  }

  vkUnmapMemory(allocation->device, allocation->memory);
  allocation->mapped = nullptr;
}

} // namespace pixel::vma

using VmaAllocator = pixel::vma::VmaAllocator;
using VmaAllocation = pixel::vma::VmaAllocation;
using VmaAllocatorCreateInfo = pixel::vma::VmaAllocatorCreateInfo;
using VmaAllocationCreateInfo = pixel::vma::VmaAllocationCreateInfo;

inline VkResult vmaCreateAllocator(const VmaAllocatorCreateInfo *info,
                                   VmaAllocator *allocator) {
  return pixel::vma::vmaCreateAllocator(info, allocator);
}

inline void vmaDestroyAllocator(VmaAllocator allocator) {
  pixel::vma::vmaDestroyAllocator(allocator);
}

inline VkResult vmaCreateBuffer(VmaAllocator allocator,
                                const VkBufferCreateInfo *buffer_info,
                                const VmaAllocationCreateInfo *alloc_info,
                                VkBuffer *buffer,
                                VmaAllocation *allocation,
                                void *user) {
  return pixel::vma::vmaCreateBuffer(allocator, buffer_info, alloc_info,
                                     buffer, allocation, user);
}

inline void vmaDestroyBuffer(VmaAllocator allocator, VkBuffer buffer,
                             VmaAllocation allocation) {
  pixel::vma::vmaDestroyBuffer(allocator, buffer, allocation);
}

inline VkResult vmaCreateImage(VmaAllocator allocator,
                               const VkImageCreateInfo *image_info,
                               const VmaAllocationCreateInfo *alloc_info,
                               VkImage *image,
                               VmaAllocation *allocation,
                               void *user) {
  return pixel::vma::vmaCreateImage(allocator, image_info, alloc_info,
                                    image, allocation, user);
}

inline void vmaDestroyImage(VmaAllocator allocator, VkImage image,
                            VmaAllocation allocation) {
  pixel::vma::vmaDestroyImage(allocator, image, allocation);
}

inline VkResult vmaMapMemory(VmaAllocator allocator, VmaAllocation allocation,
                             void **data) {
  return pixel::vma::vmaMapMemory(allocator, allocation, data);
}

inline void vmaUnmapMemory(VmaAllocator allocator, VmaAllocation allocation) {
  pixel::vma::vmaUnmapMemory(allocator, allocation);
}

