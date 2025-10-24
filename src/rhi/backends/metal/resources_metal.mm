// src/rhi/backends/metal/resources_metal.mm
// Metal resource creation and management
#ifdef __APPLE__

#include "pixel/rhi/backends/metal/metal_internal.hpp"
#include <functional>
#include <iostream>
#include <string_view>

namespace pixel::rhi {

BufferHandle MetalDevice::createBuffer(const BufferDesc &desc) {
  std::cerr << "MetalDevice::createBuffer() size=" << desc.size
            << " hostVisible=" << (desc.hostVisible ? "true" : "false")
            << " usage=" << static_cast<int>(desc.usage) << std::endl;
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
  std::cerr << "  Buffer handle allocated: " << handle_id << std::endl;
  return BufferHandle{handle_id};
}

TextureHandle MetalDevice::createTexture(const TextureDesc &desc) {
  std::cerr << "MetalDevice::createTexture() size=" << desc.size.w << "x"
            << desc.size.h << " layers=" << desc.layers
            << " format=" << static_cast<int>(desc.format)
            << " mipLevels=" << desc.mipLevels << std::endl;
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
  std::cerr << "  Texture handle allocated: " << handle_id << std::endl;
  return TextureHandle{handle_id};
}

SamplerHandle MetalDevice::createSampler(const SamplerDesc &desc) {
  std::cerr << "MetalDevice::createSampler()" << std::endl;
  std::cerr << "  minFilter="
            << (desc.minFilter == FilterMode::Linear ? "Linear" : "Nearest")
            << " magFilter="
            << (desc.magFilter == FilterMode::Linear ? "Linear" : "Nearest")
            << " compareEnable=" << (desc.compareEnable ? "true" : "false")
            << std::endl;
  MTLSamplerResource sampler;

  MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
  samplerDesc.minFilter = desc.minFilter == FilterMode::Linear
                               ? MTLSamplerMinMagFilterLinear
                               : MTLSamplerMinMagFilterNearest;
  samplerDesc.magFilter = desc.magFilter == FilterMode::Linear
                               ? MTLSamplerMinMagFilterLinear
                               : MTLSamplerMinMagFilterNearest;
  samplerDesc.mipFilter = MTLSamplerMipFilterLinear;

  auto to_address = [](AddressMode mode) {
    switch (mode) {
    case AddressMode::Repeat:
      return MTLSamplerAddressModeRepeat;
    case AddressMode::ClampToEdge:
      return MTLSamplerAddressModeClampToEdge;
    case AddressMode::ClampToBorder:
    default:
#ifdef MTLSamplerAddressModeClampToBorderColor
      return MTLSamplerAddressModeClampToBorderColor;
#else
      return MTLSamplerAddressModeClampToEdge;
#endif
    }
  };

  samplerDesc.sAddressMode = to_address(desc.addressU);
  samplerDesc.tAddressMode = to_address(desc.addressV);
  samplerDesc.rAddressMode = to_address(desc.addressW);

  if (desc.mipLodBias != 0.0f) {
    samplerDesc.lodBias = desc.mipLodBias;
  }

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

#if defined(MTLSamplerAddressModeClampToBorderColor) &&                            \
    defined(MTLSamplerBorderColorOpaqueWhite)
  if (desc.addressU == AddressMode::ClampToBorder ||
      desc.addressV == AddressMode::ClampToBorder ||
      desc.addressW == AddressMode::ClampToBorder) {
    // Metal only supports preset border colors
    bool is_white = desc.borderColor[0] >= 0.99f && desc.borderColor[1] >= 0.99f &&
                    desc.borderColor[2] >= 0.99f && desc.borderColor[3] >= 0.99f;
    samplerDesc.borderColor =
        is_white ? MTLSamplerBorderColorOpaqueWhite
                 : MTLSamplerBorderColorOpaqueBlack;
  }
#endif

  sampler.sampler = [impl_->device_ newSamplerStateWithDescriptor:samplerDesc];

  if (!sampler.sampler) {
    std::cerr << "Failed to create Metal sampler" << std::endl;
    return SamplerHandle{0};
  }

  uint32_t handle_id = impl_->next_sampler_id_++;
  impl_->samplers_[handle_id] = sampler;
  std::cerr << "  Sampler handle allocated: " << handle_id << std::endl;
  return SamplerHandle{handle_id};
}

ShaderHandle MetalDevice::createShader(std::string_view stage,
                                       std::span<const uint8_t> bytes) {
  std::cerr << "MetalDevice::createShader() stage=" << stage
            << " byteCount=" << bytes.size() << std::endl;
  MTLShaderResource shader;

  NSString *functionName = nil;
  if (stage == "vs") {
    functionName = @"vertex_main";
  } else if (stage == "vs_instanced") {
    functionName = @"vertex_instanced";
  } else if (stage == "vs_shadow") {
    functionName = @"vertex_shadow_depth";
  } else if (stage == "fs") {
    functionName = @"fragment_main";
  } else if (stage == "fs_instanced") {
    functionName = @"fragment_instanced";
  } else if (stage == "fs_shadow") {
    functionName = @"fragment_shadow_depth";
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

  bool is_compute_stage = stage.size() >= 2 && stage[0] == 'c' && stage[1] == 's';

  id<MTLLibrary> library = nil;
  if (!bytes.empty() && !is_compute_stage) {
    std::string_view src(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
    size_t hash = std::hash<std::string_view>{}(src);
    auto it = impl_->shader_library_cache_.find(hash);
    if (it != impl_->shader_library_cache_.end()) {
      library = it->second;
      std::cerr << "  Reusing cached Metal shader library for hash=" << hash
                << std::endl;
    } else {
      NSString *source = [[NSString alloc] initWithBytes:bytes.data()
                                                length:bytes.size()
                                              encoding:NSUTF8StringEncoding];
      if (!source) {
        std::cerr << "Failed to decode Metal shader source as UTF-8"
                  << std::endl;
        return ShaderHandle{0};
      }

      NSError *error = nil;
      MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
      library = [impl_->device_ newLibraryWithSource:source
                                             options:options
                                               error:&error];
      if (!library) {
        std::cerr << "Failed to compile Metal shader source: "
                  << [[error localizedDescription] UTF8String] << std::endl;
        return ShaderHandle{0};
      }

      impl_->shader_library_cache_[hash] = library;
      std::cerr << "  Compiled Metal shader library hash=" << hash
                << std::endl;
    }
  }

  if (!library) {
    if (!impl_->library_) {
      impl_->library_ = [impl_->device_ newDefaultLibrary];
      if (!impl_->library_) {
        std::cerr << "Failed to load default Metal library" << std::endl;
        return ShaderHandle{0};
      }
    }
    library = impl_->library_;
  }

  shader.function = [library newFunctionWithName:functionName];
  shader.library = library;
  shader.stage = std::string(stage);

  if (!shader.function) {
    std::cerr << "Failed to load shader function: " << [functionName UTF8String]
              << std::endl;
    return ShaderHandle{0};
  }

  uint32_t handle_id = impl_->next_shader_id_++;
  impl_->shaders_[handle_id] = shader;
  std::cerr << "  Shader handle allocated: " << handle_id << std::endl;
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
  while (dispatch_semaphore_wait(it->second.semaphore, DISPATCH_TIME_NOW) ==
         0) {
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
