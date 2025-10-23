// src/rhi/backends/metal/resources_metal.mm
// Metal resource creation and management
#ifdef __APPLE__

#include "metal_internal.hpp"

namespace pixel::rhi {

BufferHandle MetalDevice::createBuffer(const BufferDesc &desc) {
  MTLBufferResource buffer;
  buffer.size = desc.size;
  buffer.host_visible = desc.hostVisible;

  MTLResourceOptions options = buffer.host_visible
                                   ? (MTLResourceStorageModeShared |
                                      MTLResourceCPUCacheModeWriteCombined)
                                   : MTLResourceStorageModePrivate;

  buffer.buffer = [impl_->device_ newBufferWithLength:desc.size
                                              options:options];

  if (!buffer.buffer) {
    std::cerr << "Failed to create Metal buffer" << std::endl;
    return BufferHandle{0};
  }

  uint32_t handle_id = impl_->next_buffer_id_++;
  impl_->buffers_[handle_id] = buffer;
  return BufferHandle{handle_id};
}

TextureHandle MetalDevice::createTexture(const TextureDesc &desc) {
  MTLTextureResource tex;
  tex.width = desc.size.w;
  tex.height = desc.size.h;
  tex.layers = desc.layers;
  tex.format = desc.format;

  MTLPixelFormat mtlFormat = toMTLFormat(desc.format);

  MTLTextureDescriptor *texDesc;
  if (desc.layers > 1) {
    texDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:mtlFormat
                                     width:desc.size.w
                                    height:desc.size.h
                                 mipmapped:(desc.mipLevels > 1)];
    texDesc.textureType = MTLTextureType2DArray;
    texDesc.arrayLength = desc.layers;
    texDesc.pixelFormat = MTLPixelFormatRGBA8Unorm;
  } else {
    texDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:mtlFormat
                                     width:desc.size.w
                                    height:desc.size.h
                                 mipmapped:(desc.mipLevels > 1)];
  }

  texDesc.mipmapLevelCount = desc.mipLevels;
  texDesc.usage = MTLTextureUsageShaderRead;

  if (desc.renderTarget || desc.format == Format::D32F ||
      desc.format == Format::D24S8) {
    texDesc.usage |= MTLTextureUsageRenderTarget;
    texDesc.storageMode = MTLStorageModePrivate;
  }

  tex.texture = [impl_->device_ newTextureWithDescriptor:texDesc];

  if (!tex.texture) {
    std::cerr << "Failed to create Metal texture" << std::endl;
    return TextureHandle{0};
  }

  uint32_t handle_id = impl_->next_texture_id_++;
  impl_->textures_[handle_id] = tex;
  return TextureHandle{handle_id};
}

SamplerHandle MetalDevice::createSampler(const SamplerDesc &desc) {
  MTLSamplerResource sampler;

  MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
  samplerDesc.minFilter = desc.linear ? MTLSamplerMinMagFilterLinear
                                      : MTLSamplerMinMagFilterNearest;
  samplerDesc.magFilter = desc.linear ? MTLSamplerMinMagFilterLinear
                                      : MTLSamplerMinMagFilterNearest;
  samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
  samplerDesc.sAddressMode = desc.repeat ? MTLSamplerAddressModeRepeat
                                         : MTLSamplerAddressModeClampToEdge;
  samplerDesc.tAddressMode = desc.repeat ? MTLSamplerAddressModeRepeat
                                         : MTLSamplerAddressModeClampToEdge;

  if (desc.aniso || desc.maxAnisotropy > 1.0f) {
    const float kMetalMaxAnisotropy = 16.0f;
    float requested =
        desc.maxAnisotropy > 1.0f ? desc.maxAnisotropy : kMetalMaxAnisotropy;
    requested = std::min(requested, kMetalMaxAnisotropy);
    samplerDesc.maxAnisotropy = std::max<NSUInteger>(
        1, std::min<NSUInteger>(16, static_cast<NSUInteger>(requested)));
  }

  samplerDesc.compareFunction = desc.compareEnable
                                    ? to_mtl_compare(desc.compareOp)
                                    : MTLCompareFunctionNever;

  sampler.sampler = [impl_->device_ newSamplerStateWithDescriptor:samplerDesc];

  if (!sampler.sampler) {
    std::cerr << "Failed to create Metal sampler" << std::endl;
    return SamplerHandle{0};
  }

  uint32_t handle_id = impl_->next_sampler_id_++;
  impl_->samplers_[handle_id] = sampler;
  return SamplerHandle{handle_id};
}

ShaderHandle MetalDevice::createShader(std::string_view stage,
                                       std::span<const uint8_t> bytes) {
  (void)bytes;
  MTLShaderResource shader;

  if (!impl_->library_) {
    impl_->library_ = [impl_->device_ newDefaultLibrary];
    if (!impl_->library_) {
      std::cerr << "Failed to load default Metal library" << std::endl;
      return ShaderHandle{0};
    }
  }

  NSString *functionName = nil;
  if (stage == "vs") {
    functionName = @"vertex_main";
  } else if (stage == "vs_instanced") {
    functionName = @"vertex_instanced";
  } else if (stage == "fs") {
    functionName = @"fragment_main";
  } else if (stage == "fs_instanced") {
    functionName = @"fragment_instanced";
  } else if (stage == "cs_culling") {
    functionName = @"culling_compute";
  } else if (stage == "cs_lod") {
    functionName = @"lod_compute";
  } else if (stage == "cs_test") {
    functionName = @"test_compute";
  } else {
    std::cerr << "Unknown shader stage: " << stage << std::endl;
    return ShaderHandle{0};
  }

  shader.function = [impl_->library_ newFunctionWithName:functionName];
  shader.stage = std::string(stage);

  if (!shader.function) {
    std::cerr << "Failed to load shader function: "
              << [functionName UTF8String] << std::endl;
    return ShaderHandle{0};
  }

  uint32_t handle_id = impl_->next_shader_id_++;
  impl_->shaders_[handle_id] = shader;
  return ShaderHandle{handle_id};
}

FramebufferHandle MetalDevice::createFramebuffer(const FramebufferDesc &desc) {
  if (desc.colorAttachmentCount > kMaxColorAttachments) {
    std::cerr << "Metal framebuffer creation exceeded attachment limit"
              << std::endl;
    return FramebufferHandle{0};
  }

  MTLFramebufferResource framebuffer;
  framebuffer.desc = desc;

  uint32_t width = 0;
  uint32_t height = 0;

  for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
    const auto &attachment = desc.colorAttachments[i];
    if (attachment.texture.id == 0) {
      std::cerr << "Metal framebuffer color attachment cannot target swapchain"
                << std::endl;
      return FramebufferHandle{0};
    }

    auto it = impl_->textures_.find(attachment.texture.id);
    if (it == impl_->textures_.end() || !it->second.texture) {
      std::cerr << "Metal framebuffer color attachment uses invalid texture"
                << std::endl;
      return FramebufferHandle{0};
    }

    const auto &tex = it->second;
    if (width == 0) {
      width = static_cast<uint32_t>(tex.width);
      height = static_cast<uint32_t>(tex.height);
    } else if (width != static_cast<uint32_t>(tex.width) ||
               height != static_cast<uint32_t>(tex.height)) {
      std::cerr << "Metal framebuffer color attachments must share dimensions"
                << std::endl;
      return FramebufferHandle{0};
    }
  }

  if (desc.hasDepthAttachment) {
    const auto &depthAttachment = desc.depthAttachment;
    if (depthAttachment.texture.id == 0) {
      std::cerr << "Metal framebuffer depth attachment cannot target swapchain"
                << std::endl;
      return FramebufferHandle{0};
    }

    auto it = impl_->textures_.find(depthAttachment.texture.id);
    if (it == impl_->textures_.end() || !it->second.texture) {
      std::cerr << "Metal framebuffer depth attachment uses invalid texture"
                << std::endl;
      return FramebufferHandle{0};
    }

    const auto &tex = it->second;
    if (width == 0) {
      width = static_cast<uint32_t>(tex.width);
      height = static_cast<uint32_t>(tex.height);
    } else if (width != static_cast<uint32_t>(tex.width) ||
               height != static_cast<uint32_t>(tex.height)) {
      std::cerr
          << "Metal framebuffer depth attachment dimensions must match colors"
          << std::endl;
      return FramebufferHandle{0};
    }
  }

  if (width == 0 || height == 0) {
    std::cerr << "Metal framebuffer requires at least one valid attachment"
              << std::endl;
    return FramebufferHandle{0};
  }

  framebuffer.width = width;
  framebuffer.height = height;

  uint32_t handle_id = impl_->next_framebuffer_id_++;
  impl_->framebuffers_[handle_id] = framebuffer;
  return FramebufferHandle{handle_id};
}

QueryHandle MetalDevice::createQuery(QueryType type) {
  MetalQueryResource query;
  query.type = type;
  uint32_t id = impl_->next_query_id_++;
  impl_->queries_[id] = query;
  return QueryHandle{id};
}

void MetalDevice::destroyQuery(QueryHandle handle) {
  auto it = impl_->queries_.find(handle.id);
  if (it == impl_->queries_.end())
    return;
  it->second.pending_command_buffer = nil;
  impl_->queries_.erase(it);
}

bool MetalDevice::getQueryResult(QueryHandle handle, uint64_t &result,
                                 bool wait) {
  auto it = impl_->queries_.find(handle.id);
  if (it == impl_->queries_.end())
    return false;
  auto &query = it->second;
  if (!query.available && wait && query.pending_command_buffer) {
    [query.pending_command_buffer waitUntilCompleted];
  }
  if (!query.available)
    return false;
  result = query.result;
  return true;
}

FenceHandle MetalDevice::createFence(bool signaled) {
  MetalFenceResource fence;
  fence.semaphore = dispatch_semaphore_create(signaled ? 1 : 0);
  if (!fence.semaphore)
    return FenceHandle{0};
  fence.signaled = signaled;
  uint32_t id = impl_->next_fence_id_++;
  impl_->fences_[id] = fence;
  return FenceHandle{id};
}

void MetalDevice::destroyFence(FenceHandle handle) {
  auto it = impl_->fences_.find(handle.id);
  if (it == impl_->fences_.end())
    return;
  if (it->second.semaphore) {
#if defined(OS_OBJECT_USE_OBJC) && OS_OBJECT_USE_OBJC
    it->second.semaphore = nullptr;
#else
    dispatch_release(it->second.semaphore);
    it->second.semaphore = nullptr;
#endif
  }
  impl_->fences_.erase(it);
}

void MetalDevice::waitFence(FenceHandle handle, uint64_t timeout_ns) {
  auto it = impl_->fences_.find(handle.id);
  if (it == impl_->fences_.end())
    return;
  if (!it->second.semaphore)
    return;
  dispatch_time_t timeout =
      timeout_ns == ~0ull
          ? DISPATCH_TIME_FOREVER
          : dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeout_ns));
  long status = dispatch_semaphore_wait(it->second.semaphore, timeout);
  if (status == 0) {
    it->second.signaled = true;
  }
}

void MetalDevice::resetFence(FenceHandle handle) {
  auto it = impl_->fences_.find(handle.id);
  if (it == impl_->fences_.end())
    return;
  if (!it->second.semaphore)
    return;
  while (dispatch_semaphore_wait(it->second.semaphore, DISPATCH_TIME_NOW) == 0) {
  }
  it->second.signaled = false;
}

void MetalDevice::readBuffer(BufferHandle handle, void *dst, size_t size,
                             size_t offset) {
  auto it = impl_->buffers_.find(handle.id);
  if (it == impl_->buffers_.end()) {
    std::cerr << "Attempted to read from invalid Metal buffer handle"
              << std::endl;
    return;
  }

  const MTLBufferResource &buffer = it->second;
  if (!buffer.host_visible) {
    std::cerr << "Metal buffer is not host-visible; cannot read back"
              << std::endl;
    return;
  }

  if (offset + size > buffer.size) {
    std::cerr << "Read range exceeds Metal buffer size" << std::endl;
    return;
  }

  uint8_t *contents = (uint8_t *)buffer.buffer.contents;
  memcpy(dst, contents + offset, size);
}

} // namespace pixel::rhi

#endif // __APPLE__
