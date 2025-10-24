// src/rhi/backends/metal/cmd_metal.mm
// Metal command list core implementation
#ifdef __APPLE__

#include "pixel/rhi/backends/metal/metal_internal.hpp"

#include <GLFW/glfw3.h>

namespace pixel::rhi {

MetalCmdList::MetalCmdList(MetalDevice::Impl *device_impl)
    : impl_(std::make_unique<Impl>(device_impl)) {}

MetalCmdList::~MetalCmdList() = default;

void MetalCmdList::begin() {
  std::cerr << "MetalCmdList::begin()" << std::endl;
  if (impl_->uniform_allocator_) {
    impl_->uniform_allocator_->reset(*impl_->frame_index_);
  }
  impl_->resetUniformBlock();
  impl_->staging_uploads_.clear();

  impl_->recording_ = true;
  impl_->command_buffer_ = [impl_->command_queue_ commandBuffer];
  impl_->render_encoder_ = nil;
  impl_->compute_encoder_ = nil;
  impl_->current_drawable_ = nil;
  impl_->current_pipeline_ = PipelineHandle{0};
  impl_->current_compute_pipeline_ = PipelineHandle{0};
  impl_->current_vb_ = BufferHandle{0};
  impl_->current_ib_ = BufferHandle{0};
  impl_->current_vb_offset_ = 0;
  impl_->current_ib_offset_ = 0;
  impl_->active_encoder_ = Impl::EncoderState::None;
  impl_->depth_stencil_state_initialized_ = false;
  impl_->depth_bias_initialized_ = false;
}

void MetalCmdList::beginRender(const RenderPassDesc &desc) {
  std::cerr << "\n" << std::string(60, '=') << std::endl;
  std::cerr << "beginRender() CALLED" << std::endl;
  std::cerr << "  Color attachments: " << desc.colorAttachmentCount
            << std::endl;
  std::cerr << "  Depth attachment:  "
            << (desc.depthAttachment.clearDepth ? "YES" : "NO") << std::endl;
  std::cerr << std::string(60, '=') << "\n" << std::endl;
  impl_->endComputeEncoderIfNeeded();

  impl_->resetUniformBlock();

  const MTLFramebufferResource *framebuffer = nullptr;
  if (desc.framebuffer.id != 0) {
    auto fb_it = impl_->framebuffers_->find(desc.framebuffer.id);
    if (fb_it == impl_->framebuffers_->end()) {
      std::cerr << "Metal render pass referenced invalid framebuffer handle"
                << std::endl;
      return;
    }
    framebuffer = &fb_it->second;
  }

  uint32_t colorAttachmentCount = framebuffer
                                      ? framebuffer->desc.colorAttachmentCount
                                      : desc.colorAttachmentCount;
  bool hasDepthAttachment = framebuffer ? framebuffer->desc.hasDepthAttachment
                                        : desc.hasDepthAttachment;

  if (colorAttachmentCount > kMaxColorAttachments) {
    std::cerr << "Metal render pass exceeds maximum color attachments"
              << std::endl;
    return;
  }

  if (colorAttachmentCount == 0 && !hasDepthAttachment) {
    std::cerr << "Metal render pass requires at least one attachment"
              << std::endl;
    return;
  }

  bool requiresDrawable = false;
  bool usesSwapchainDepth = false;

  if (!framebuffer) {
    usesSwapchainDepth =
        hasDepthAttachment && desc.depthAttachment.texture.id == 0;
    if (colorAttachmentCount == 0) {
      requiresDrawable = usesSwapchainDepth;
    } else {
      for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
        if (desc.colorAttachments[i].texture.id == 0) {
          requiresDrawable = true;
          break;
        }
      }
    }
  }

  NSUInteger targetWidth = framebuffer ? framebuffer->width : 0;
  NSUInteger targetHeight = framebuffer ? framebuffer->height : 0;

  if (!framebuffer && impl_->glfw_window_) {
    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(impl_->glfw_window_, &fbWidth, &fbHeight);
    if (fbWidth > 0 && fbHeight > 0) {
      targetWidth = static_cast<NSUInteger>(fbWidth);
      targetHeight = static_cast<NSUInteger>(fbHeight);
    }
  }

  if (!framebuffer && impl_->layer_) {
    CGSize drawableSize = impl_->layer_.drawableSize;
    if (targetWidth == 0) {
      targetWidth = static_cast<NSUInteger>(drawableSize.width);
    }
    if (targetHeight == 0) {
      targetHeight = static_cast<NSUInteger>(drawableSize.height);
    }
  }

  NSUInteger resolvedWidth = targetWidth;
  NSUInteger resolvedHeight = targetHeight;

  if (requiresDrawable) {
    if (!impl_->layer_) {
      std::cerr
          << "Metal render pass requested swapchain drawable but layer was"
             " null"
          << std::endl;
      return;
    }

    if (targetWidth == 0 || targetHeight == 0) {
      std::cerr << "Metal drawable size unresolved before acquisition ("
                << targetWidth << "x" << targetHeight
                << ") - forcing minimum size" << std::endl;
      targetWidth = std::max<NSUInteger>(targetWidth, 1);
      targetHeight = std::max<NSUInteger>(targetHeight, 1);
    }

    if (impl_->device_impl_) {
      impl_->device_impl_->updateSwapchainSize(targetWidth, targetHeight);
      impl_->depth_texture_ = impl_->device_impl_->depth_texture_;
    }

    std::cerr << "DEBUG: Attempting to acquire drawable for " << targetWidth
              << "x" << targetHeight << std::endl;
    impl_->current_drawable_ = [impl_->layer_ nextDrawable];

    if (!impl_->current_drawable_) {
      std::cerr << "DEBUG: *** FAILED TO ACQUIRE DRAWABLE *** on frame "
                << *impl_->frame_index_ << std::endl;
      return;
    }

    id<MTLTexture> drawableTex = impl_->current_drawable_.texture;
    NSUInteger drawableWidth = drawableTex ? drawableTex.width : 0;
    NSUInteger drawableHeight = drawableTex ? drawableTex.height : 0;
    std::cerr << "DEBUG: Successfully acquired drawable (" << drawableWidth
              << "x" << drawableHeight << ")" << std::endl;
    if (drawableWidth > 0 && drawableHeight > 0) {
      resolvedWidth = drawableWidth;
      resolvedHeight = drawableHeight;
    }
  } else {
    impl_->current_drawable_ = nil;
  }

  MTLRenderPassDescriptor *renderPassDesc =
      [MTLRenderPassDescriptor renderPassDescriptor];

  if (!renderPassDesc) {
    std::cerr
        << "DEBUG: EARLY RETURN - failed to allocate render pass descriptor"
        << std::endl;
    return;
  }

  for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
    const RenderPassColorAttachment *attachment =
        (i < desc.colorAttachmentCount) ? &desc.colorAttachments[i] : nullptr;
    TextureHandle textureHandle =
        framebuffer ? framebuffer->desc.colorAttachments[i].texture
                    : (attachment ? attachment->texture : TextureHandle{0});
    uint32_t mipLevel = framebuffer
                            ? framebuffer->desc.colorAttachments[i].mipLevel
                            : (attachment ? attachment->mipLevel : 0);
    uint32_t arraySlice = framebuffer
                              ? framebuffer->desc.colorAttachments[i].arraySlice
                              : (attachment ? attachment->arraySlice : 0);

    id<MTLTexture> targetTexture = nil;
    if (textureHandle.id == 0) {
      targetTexture =
          impl_->current_drawable_ ? impl_->current_drawable_.texture : nil;
    } else {
      auto tex_it = impl_->textures_->find(textureHandle.id);
      if (tex_it == impl_->textures_->end()) {
        std::cerr << "Render pass references invalid color texture"
                  << std::endl;
        return;
      }
      targetTexture = tex_it->second.texture;
      targetWidth = tex_it->second.width;
      targetHeight = tex_it->second.height;
      resolvedWidth = targetWidth;
      resolvedHeight = targetHeight;
    }

    if (!targetTexture) {
      std::cerr << "Render pass missing color target " << i << std::endl;
      return;
    }

    auto *colorDesc = renderPassDesc.colorAttachments[i];
    colorDesc.texture = targetTexture;
    colorDesc.level = mipLevel;
    colorDesc.slice = arraySlice;

    if (attachment) {
      colorDesc.loadAction = toMTLLoadAction(attachment->loadOp);
      colorDesc.storeAction = toMTLStoreAction(attachment->storeOp);
      colorDesc.clearColor = MTLClearColorMake(
          attachment->clearColor[0], attachment->clearColor[1],
          attachment->clearColor[2], attachment->clearColor[3]);
    } else {
      colorDesc.loadAction = MTLLoadActionLoad;
      colorDesc.storeAction = MTLStoreActionStore;
    }
  }

  for (uint32_t i = colorAttachmentCount; i < kMaxColorAttachments; ++i) {
    renderPassDesc.colorAttachments[i].texture = nil;
    renderPassDesc.colorAttachments[i].loadAction = MTLLoadActionDontCare;
    renderPassDesc.colorAttachments[i].storeAction = MTLStoreActionDontCare;
  }

  if (hasDepthAttachment) {
    const FramebufferDepthAttachmentDesc *fbAttachment =
        (framebuffer && framebuffer->desc.hasDepthAttachment)
            ? &framebuffer->desc.depthAttachment
            : nullptr;
    const RenderPassDepthAttachment *attachment =
        desc.hasDepthAttachment ? &desc.depthAttachment : nullptr;
    RenderPassDepthAttachment defaultAttachment{};
    const RenderPassDepthAttachment *opsAttachment =
        attachment ? attachment : &defaultAttachment;

    TextureHandle textureHandle =
        fbAttachment ? fbAttachment->texture : opsAttachment->texture;

    id<MTLTexture> depthTexture = nil;
    if (textureHandle.id == 0) {
      depthTexture = usesSwapchainDepth ? impl_->depth_texture_ : nil;
    } else {
      auto tex_it = impl_->textures_->find(textureHandle.id);
      if (tex_it == impl_->textures_->end()) {
        std::cerr << "Render pass references invalid depth texture"
                  << std::endl;
        return;
      }
      depthTexture = tex_it->second.texture;
      resolvedWidth = tex_it->second.width;
      resolvedHeight = tex_it->second.height;
    }

    if (depthTexture) {
      renderPassDesc.depthAttachment.texture = depthTexture;
      renderPassDesc.depthAttachment.level =
          fbAttachment ? fbAttachment->mipLevel : opsAttachment->mipLevel;
      renderPassDesc.depthAttachment.slice =
          fbAttachment ? fbAttachment->arraySlice : opsAttachment->arraySlice;

      LoadOp depthLoadOp = opsAttachment->depthLoadOp;
      StoreOp depthStoreOp = opsAttachment->depthStoreOp;
      float clearDepth = opsAttachment->clearDepth;

      renderPassDesc.depthAttachment.loadAction = toMTLLoadAction(depthLoadOp);
      renderPassDesc.depthAttachment.storeAction =
          toMTLStoreAction(depthStoreOp);
      renderPassDesc.depthAttachment.clearDepth = clearDepth;

      bool hasStencil = opsAttachment->hasStencil ||
                        (fbAttachment ? fbAttachment->hasStencil : false);

      if (hasStencil) {
        renderPassDesc.stencilAttachment.texture = depthTexture;
        renderPassDesc.stencilAttachment.level =
            fbAttachment ? fbAttachment->mipLevel : opsAttachment->mipLevel;
        renderPassDesc.stencilAttachment.slice =
            fbAttachment ? fbAttachment->arraySlice : opsAttachment->arraySlice;

        LoadOp stencilLoadOp = opsAttachment->stencilLoadOp;
        StoreOp stencilStoreOp = opsAttachment->stencilStoreOp;
        uint32_t clearStencil = opsAttachment->clearStencil;

        renderPassDesc.stencilAttachment.loadAction =
            toMTLLoadAction(stencilLoadOp);
        renderPassDesc.stencilAttachment.storeAction =
            toMTLStoreAction(stencilStoreOp);
        renderPassDesc.stencilAttachment.clearStencil = clearStencil;
      }
    }
  }

  impl_->render_encoder_ = [impl_->command_buffer_
      renderCommandEncoderWithDescriptor:renderPassDesc];

  if (!impl_->render_encoder_) {
    std::cerr << "Failed to create Metal render encoder" << std::endl;
    return;
  }

  // Configure viewport & scissor to target size so draws are visible
  if (resolvedWidth == 0 || resolvedHeight == 0) {
    resolvedWidth = std::max<NSUInteger>(resolvedWidth, 1);
    resolvedHeight = std::max<NSUInteger>(resolvedHeight, 1);
  }

  MTLViewport viewport;
  viewport.originX = 0.0;
  viewport.originY = 0.0;
  viewport.width = static_cast<double>(resolvedWidth);
  viewport.height = static_cast<double>(resolvedHeight);
  viewport.znear = 0.0;
  viewport.zfar = 1.0;
  [impl_->render_encoder_ setViewport:viewport];

  MTLScissorRect scissor;
  scissor.x = 0;
  scissor.y = 0;
  scissor.width = resolvedWidth;
  scissor.height = resolvedHeight;
  [impl_->render_encoder_ setScissorRect:scissor];

  std::cerr << "DEBUG: Viewport set to " << resolvedWidth << "x"
            << resolvedHeight << std::endl;

  impl_->active_encoder_ = Impl::EncoderState::Render;
}

void MetalCmdList::setPipeline(PipelineHandle handle) {
  std::cerr << "MetalCmdList::setPipeline(): handle=" << handle.id << std::endl;
  if (handle.id == 0) {
    std::cerr << "  WARNING: Attempted to bind null pipeline" << std::endl;
    return;
  }

  auto it = impl_->pipelines_->find(handle.id);
  if (it == impl_->pipelines_->end()) {
    std::cerr << "Attempted to bind invalid Metal pipeline handle" << std::endl;
    return;
  }

  const auto &pipeline = it->second;
  if (!pipeline.pipeline_state) {
    std::cerr << "Metal pipeline missing render state" << std::endl;
    return;
  }

  if (!impl_->render_encoder_) {
    std::cerr << "Attempted to set pipeline without an active render encoder"
              << std::endl;
    return;
  }

  impl_->render_encoder_.label = @"RenderEncoder";
  [impl_->render_encoder_ setRenderPipelineState:pipeline.pipeline_state];
  if (pipeline.depth_stencil_state) {
    [impl_->render_encoder_ setDepthStencilState:pipeline.depth_stencil_state];
    std::cerr << "  Depth stencil state bound" << std::endl;
  } else {
    std::cerr << "  WARNING: Pipeline missing depth stencil state" << std::endl;
  }

  impl_->current_pipeline_ = handle;
  impl_->current_compute_pipeline_ = PipelineHandle{0};
  std::cerr << "  Pipeline bound successfully" << std::endl;
}

void MetalCmdList::setVertexBuffer(BufferHandle handle, size_t offset) {
  // === DIAGNOSTIC LOGGING ===
  std::cerr << "setVertexBuffer(): handle=" << handle.id
            << ", offset=" << offset << std::endl;
  // === END ===

  impl_->current_vb_ = handle;
  impl_->current_vb_offset_ = offset;

  if (handle.id == 0) {
    return;
  }

  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end()) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ERROR: Vertex buffer " << handle.id << " not found!"
              << std::endl;
    // === END ===
    return;
  }

  if (impl_->render_encoder_) {
    [impl_->render_encoder_ setVertexBuffer:it->second.buffer
                                     offset:offset
                                    atIndex:0];
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ✓ Vertex buffer bound to index 0" << std::endl;
    // === END ===
  }
}

void MetalCmdList::setIndexBuffer(BufferHandle handle, size_t offset) {
  // === DIAGNOSTIC LOGGING ===
  std::cerr << "setIndexBuffer(): handle=" << handle.id << ", offset=" << offset
            << std::endl;
  // === END ===

  impl_->current_ib_ = handle;
  impl_->current_ib_offset_ = offset;

  if (handle.id == 0) {
    return;
  }

  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end()) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ERROR: Index buffer " << handle.id << " not found!"
              << std::endl;
    // === END ===
    return;
  }

  if (impl_->render_encoder_) {
    [impl_->render_encoder_ setVertexBuffer:it->second.buffer
                                     offset:offset
                                    atIndex:3];
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ✓ Index buffer bound to index 3" << std::endl;
    // === END ===
  }
}

void MetalCmdList::setInstanceBuffer(BufferHandle handle, size_t stride,
                                     size_t offset) {
  // === DIAGNOSTIC LOGGING START ===
  std::cerr << "\n" << std::string(60, '=') << std::endl;
  std::cerr << "setInstanceBuffer() CALLED" << std::endl;
  std::cerr << std::string(60, '=') << std::endl;
  std::cerr << "  handle.id:      " << handle.id << std::endl;
  std::cerr << "  stride:         " << stride << " bytes" << std::endl;
  std::cerr << "  offset:         " << offset << " bytes" << std::endl;
  std::cerr << "  encoder active: " << (impl_->render_encoder_ ? "YES" : "NO")
            << std::endl;
  // === DIAGNOSTIC LOGGING END ===

  (void)stride;
  if (handle.id == 0) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ERROR: Invalid buffer handle (0)!" << std::endl;
    std::cerr << std::string(60, '=') << "\n" << std::endl;
    // === END ===
    return;
  }

  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end()) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "  ERROR: Buffer " << handle.id << " not found in buffer map!"
              << std::endl;
    std::cerr << std::string(60, '=') << "\n" << std::endl;
    // === END ===
    return;
  }

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "  ✓ Buffer found:" << std::endl;
  std::cerr << "    - size:        " << it->second.size << " bytes"
            << std::endl;
  std::cerr << "    - host_visible: "
            << (it->second.host_visible ? "YES" : "NO") << std::endl;
  std::cerr << "  Setting vertex buffer at index 2 (instance data)..."
            << std::endl;
  // === END ===

  [impl_->render_encoder_ setVertexBuffer:it->second.buffer
                                   offset:offset
                                  atIndex:2];

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "  ✓ SUCCESS: Instance buffer bound to buffer index 2"
            << std::endl;
  std::cerr << std::string(60, '=') << "\n" << std::endl;
  // === END ===
}

void MetalCmdList::copyToTexture(TextureHandle texture, uint32_t mipLevel,
                                 std::span<const std::byte> data) {
  auto it = impl_->textures_->find(texture.id);
  if (it == impl_->textures_->end())
    return;

  const MTLTextureResource &tex = it->second;
  int mipWidth = std::max(1, tex.width >> mipLevel);
  int mipHeight = std::max(1, tex.height >> mipLevel);
  size_t bytesPerRow = mipWidth * getBytesPerPixel(tex.format);
  size_t bytesPerImage = bytesPerRow * mipHeight;
  MTLRegion region = MTLRegionMake2D(0, 0, mipWidth, mipHeight);

  [tex.texture replaceRegion:region
                 mipmapLevel:mipLevel
                       slice:0
                   withBytes:data.data()
                 bytesPerRow:bytesPerRow
               bytesPerImage:bytesPerImage];
}

void MetalCmdList::copyToTextureLayer(TextureHandle texture, uint32_t layer,
                                      uint32_t mipLevel,
                                      std::span<const std::byte> data) {
  auto it = impl_->textures_->find(texture.id);
  if (it == impl_->textures_->end())
    return;

  const MTLTextureResource &tex = it->second;

  if (layer >= static_cast<uint32_t>(tex.layers)) {
    std::cerr << "Invalid layer index: " << layer << " (max: " << tex.layers - 1
              << ")" << std::endl;
    return;
  }

  int mipWidth = std::max(1, tex.width >> mipLevel);
  int mipHeight = std::max(1, tex.height >> mipLevel);
  size_t bytesPerRow = mipWidth * getBytesPerPixel(tex.format);
  size_t bytesPerImage = bytesPerRow * mipHeight;
  MTLRegion region = MTLRegionMake2D(0, 0, mipWidth, mipHeight);

  [tex.texture replaceRegion:region
                 mipmapLevel:mipLevel
                       slice:layer
                   withBytes:data.data()
                 bytesPerRow:bytesPerRow
               bytesPerImage:bytesPerImage];
}

void MetalCmdList::drawIndexed(uint32_t indexCount, uint32_t firstIndex,
                               uint32_t instanceCount) {
  // === DIAGNOSTIC LOGGING START ===
  std::cerr << "\n" << std::string(60, '=') << std::endl;
  std::cerr << "drawIndexed() CALLED" << std::endl;
  std::cerr << std::string(60, '=') << std::endl;
  std::cerr << "Draw Parameters:" << std::endl;
  std::cerr << "  indexCount:     " << indexCount << std::endl;
  std::cerr << "  firstIndex:     " << firstIndex << std::endl;
  std::cerr << "  instanceCount:  " << instanceCount << std::endl;
  std::cerr << "\nPipeline State:" << std::endl;
  std::cerr << "  pipeline.id:    " << impl_->current_pipeline_.id << std::endl;
  std::cerr << "  index_buf.id:   " << impl_->current_ib_.id << std::endl;
  std::cerr << "  index_offset:   " << impl_->current_ib_offset_ << std::endl;
  std::cerr << "\nEncoder State:" << std::endl;
  std::cerr << "  render_encoder: "
            << (impl_->render_encoder_ ? "ACTIVE" : "NULL") << std::endl;
  std::cerr << "  encoder_state:  ";
  switch (impl_->active_encoder_) {
  case Impl::EncoderState::None:
    std::cerr << "None";
    break;
  case Impl::EncoderState::Render:
    std::cerr << "Render";
    break;
  case Impl::EncoderState::Compute:
    std::cerr << "Compute";
    break;
  }
  std::cerr << std::endl;
  // === DIAGNOSTIC LOGGING END ===

  if (impl_->current_pipeline_.id == 0 || impl_->current_ib_.id == 0) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "\n❌ FATAL ERROR: Pipeline or index buffer not set!"
              << std::endl;
    if (impl_->current_pipeline_.id == 0) {
      std::cerr << "   - Pipeline ID is 0 (not set)" << std::endl;
    }
    if (impl_->current_ib_.id == 0) {
      std::cerr << "   - Index buffer ID is 0 (not set)" << std::endl;
    }
    std::cerr << std::string(60, '=') << "\n" << std::endl;
    // === END ===
    return;
  }

  if (!impl_->render_encoder_) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "\n❌ FATAL ERROR: No active render pass!" << std::endl;
    std::cerr << "   - beginRender() must be called before drawing"
              << std::endl;
    std::cerr << std::string(60, '=') << "\n" << std::endl;
    // === END ===
    return;
  }

  if (impl_->active_encoder_ != Impl::EncoderState::Render) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "\n❌ FATAL ERROR: Encoder not in render state!" << std::endl;
    std::cerr << "   - Current state: "
              << static_cast<int>(impl_->active_encoder_) << std::endl;
    std::cerr << std::string(60, '=') << "\n" << std::endl;
    // === END ===
    return;
  }

  auto ib_it = impl_->buffers_->find(impl_->current_ib_.id);
  if (ib_it == impl_->buffers_->end()) {
    // === DIAGNOSTIC LOGGING ===
    std::cerr << "\n❌ FATAL ERROR: Index buffer not found!" << std::endl;
    std::cerr << "   - Requested buffer ID: " << impl_->current_ib_.id
              << std::endl;
    std::cerr << std::string(60, '=') << "\n" << std::endl;
    // === END ===
    return;
  }

  size_t indexOffset =
      impl_->current_ib_offset_ + firstIndex * sizeof(uint32_t);

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "\n✓ All preconditions met, issuing draw call..." << std::endl;
  std::cerr << "  Final indexOffset: " << indexOffset << " bytes" << std::endl;
  // === END ===

  [impl_->render_encoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                     indexCount:indexCount
                                      indexType:MTLIndexTypeUInt32
                                    indexBuffer:ib_it->second.buffer
                              indexBufferOffset:indexOffset
                                  instanceCount:instanceCount];

  // === DIAGNOSTIC LOGGING ===
  std::cerr << "✓ SUCCESS: Draw call issued to Metal" << std::endl;
  std::cerr << std::string(60, '=') << "\n" << std::endl;
  // === END ===

  impl_->resetUniformBlock();
}

void MetalCmdList::setComputePipeline(PipelineHandle handle) {
  std::cerr << "MetalCmdList::setComputePipeline(): handle=" << handle.id
            << std::endl;
  auto it = impl_->pipelines_->find(handle.id);
  if (it == impl_->pipelines_->end()) {
    std::cerr << "  ERROR: Invalid compute pipeline handle" << std::endl;
    return;
  }

  const MTLPipelineResource &pipeline = it->second;

  if (!pipeline.compute_pipeline_state) {
    std::cerr << "  ERROR: Pipeline handle does not reference a compute pipeline"
              << std::endl;
    return;
  }

  if (!impl_->command_buffer_) {
    std::cerr << "  ERROR: Compute pipeline set without an active command buffer"
              << std::endl;
    return;
  }

  impl_->transitionToComputeEncoder();
  std::cerr << "  Transitioned to compute encoder" << std::endl;
  [impl_->compute_encoder_
      setComputePipelineState:pipeline.compute_pipeline_state];
  impl_->current_compute_pipeline_ = handle;
  impl_->current_pipeline_ = PipelineHandle{0};
  std::cerr << "  Compute pipeline bound successfully" << std::endl;
}

void MetalCmdList::setStorageBuffer(uint32_t binding, BufferHandle buffer,
                                    size_t offset, size_t size) {
  std::cerr << "MetalCmdList::setStorageBuffer(): binding=" << binding
            << " handle=" << buffer.id << " offset=" << offset
            << " size=" << size << std::endl;
  (void)size;
  auto it = impl_->buffers_->find(buffer.id);
  if (it == impl_->buffers_->end()) {
    std::cerr << "  ERROR: Storage buffer handle not found" << std::endl;
    return;
  }

  const MTLBufferResource &buf = it->second;

  if (!impl_->compute_encoder_) {
    std::cerr << "  ERROR: Attempted to bind storage buffer without an active "
                 "compute encoder"
              << std::endl;
    return;
  }

  [impl_->compute_encoder_ setBuffer:buf.buffer offset:offset atIndex:binding];
  std::cerr << "  Storage buffer bound to compute index " << binding
            << std::endl;
}

void MetalCmdList::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                            uint32_t groupCountZ) {
  std::cerr << "MetalCmdList::dispatch(): groups=(" << groupCountX << ", "
            << groupCountY << ", " << groupCountZ << ")" << std::endl;
  if (!impl_->compute_encoder_) {
    std::cerr << "  ERROR: No active compute encoder for dispatch" << std::endl;
    return;
  }

  auto it = impl_->pipelines_->find(impl_->current_compute_pipeline_.id);
  if (it == impl_->pipelines_->end()) {
    std::cerr << "  ERROR: Current compute pipeline handle invalid" << std::endl;
    return;
  }

  id<MTLComputePipelineState> state = it->second.compute_pipeline_state;
  if (!state) {
    std::cerr << "  ERROR: Compute pipeline state is null" << std::endl;
    return;
  }

  if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0) {
    std::cerr << "  WARNING: Zero-sized dispatch requested, skipping"
              << std::endl;
    return;
  }

  NSUInteger maxThreads = state.maxTotalThreadsPerThreadgroup;
  NSUInteger executionWidth = state.threadExecutionWidth;

  NSUInteger threadsX = std::max<NSUInteger>(
      1, std::min<NSUInteger>(executionWidth,
                              static_cast<NSUInteger>(groupCountX)));
  threadsX = std::min(threadsX, maxThreads);

  NSUInteger remaining = std::max<NSUInteger>(1, maxThreads / threadsX);
  NSUInteger threadsY = std::max<NSUInteger>(
      1, std::min<NSUInteger>(static_cast<NSUInteger>(groupCountY), remaining));
  threadsY = std::min(threadsY, remaining);
  if (threadsY == 0)
    threadsY = 1;

  remaining = std::max<NSUInteger>(1, remaining / threadsY);
  NSUInteger threadsZ = std::max<NSUInteger>(
      1, std::min<NSUInteger>(static_cast<NSUInteger>(groupCountZ), remaining));
  threadsZ = std::min(threadsZ, remaining);
  if (threadsZ == 0)
    threadsZ = 1;

  MTLSize threadsPerGroup = MTLSizeMake(threadsX, threadsY, threadsZ);

  auto ceilDiv = [](NSUInteger total, NSUInteger denom) -> NSUInteger {
    return (total + denom - 1) / denom;
  };

  NSUInteger groupsX =
      ceilDiv(static_cast<NSUInteger>(groupCountX), threadsPerGroup.width);
  NSUInteger groupsY =
      ceilDiv(static_cast<NSUInteger>(groupCountY), threadsPerGroup.height);
  NSUInteger groupsZ =
      ceilDiv(static_cast<NSUInteger>(groupCountZ), threadsPerGroup.depth);

  groupsX = std::max<NSUInteger>(groupsX, 1);
  groupsY = std::max<NSUInteger>(groupsY, 1);
  groupsZ = std::max<NSUInteger>(groupsZ, 1);

  MTLSize threadgroups = MTLSizeMake(groupsX, groupsY, groupsZ);

  [impl_->compute_encoder_ dispatchThreadgroups:threadgroups
                          threadsPerThreadgroup:threadsPerGroup];
  std::cerr << "  Dispatch issued with threadgroups=(" << groupsX << ", "
            << groupsY << ", " << groupsZ << ") and threadsPerGroup=("
            << threadsPerGroup.width << ", " << threadsPerGroup.height << ", "
            << threadsPerGroup.depth << ")" << std::endl;
}

void MetalCmdList::memoryBarrier() {
  if (!impl_->compute_encoder_)
    return;

  if ([impl_->compute_encoder_
          respondsToSelector:@selector(memoryBarrierWithScope:options:)]) {
    [impl_->compute_encoder_ memoryBarrierWithScope:MTLBarrierScopeBuffers
                                            options:0];
  } else if ([impl_->compute_encoder_
                 respondsToSelector:@selector(memoryBarrierWithScope:)]) {
    [impl_->compute_encoder_ memoryBarrierWithScope:MTLBarrierScopeBuffers];
  }
}

void MetalCmdList::resourceBarrier(
    std::span<const ResourceBarrierDesc> barriers) {
  if (barriers.empty())
    return;
  impl_->endRenderEncoderIfNeeded();
  impl_->endComputeEncoderIfNeeded();
}

void MetalCmdList::beginQuery(QueryHandle handle, QueryType type) {
  if (!impl_->queries_)
    return;
  auto it = impl_->queries_->find(handle.id);
  if (it == impl_->queries_->end())
    return;
  MetalQueryResource &query = it->second;
  query.type = type;
  query.active = true;
  query.available = false;
  query.result = 0;
  query.pending_command_buffer = impl_->command_buffer_;
}

void MetalCmdList::endQuery(QueryHandle handle, QueryType type) {
  if (!impl_->queries_)
    return;
  auto it = impl_->queries_->find(handle.id);
  if (it == impl_->queries_->end())
    return;
  MetalQueryResource &query = it->second;
  query.type = type;
  query.active = false;
  if (!impl_->command_buffer_)
    return;
  query.pending_command_buffer = impl_->command_buffer_;
  __block MetalQueryResource *blockQuery = &query;
  [impl_->command_buffer_ addCompletedHandler:^(id<MTLCommandBuffer> cb) {
    double gpuStart = 0.0;
    double gpuEnd = 0.0;
    if ([cb respondsToSelector:@selector(GPUStartTime)]) {
      gpuStart = cb.GPUStartTime;
    }
    if ([cb respondsToSelector:@selector(GPUEndTime)]) {
      gpuEnd = cb.GPUEndTime;
    }
    uint64_t value = 0;
    if (blockQuery->type == QueryType::Timestamp) {
      value = static_cast<uint64_t>(gpuEnd * 1e9);
    } else {
      double delta = std::max(0.0, gpuEnd - gpuStart);
      value = static_cast<uint64_t>(delta * 1e9);
    }
    blockQuery->result = value;
    blockQuery->available = true;
    blockQuery->pending_command_buffer = nil;
  }];
}

void MetalCmdList::signalFence(FenceHandle handle) {
  if (!impl_->fences_)
    return;
  auto it = impl_->fences_->find(handle.id);
  if (it == impl_->fences_->end())
    return;
  MetalFenceResource &fence = it->second;
  if (!fence.semaphore)
    return;
  fence.signaled = false;
  if (!impl_->command_buffer_) {
    dispatch_semaphore_signal(fence.semaphore);
    fence.signaled = true;
    return;
  }
  dispatch_semaphore_t semaphore = fence.semaphore;
  __block MetalFenceResource *blockFence = &fence;
  [impl_->command_buffer_ addCompletedHandler:^(id<MTLCommandBuffer> cb) {
    (void)cb;
    dispatch_semaphore_signal(semaphore);
    blockFence->signaled = true;
  }];
}

void MetalCmdList::endRender() {
  std::cerr << "MetalCmdList::endRender()" << std::endl;
  impl_->resetEncoders();
  impl_->current_pipeline_ = PipelineHandle{0};
  impl_->current_compute_pipeline_ = PipelineHandle{0};
  impl_->resetUniformBlock();
}

void MetalCmdList::copyToBuffer(BufferHandle handle, size_t dstOff,
                                std::span<const std::byte> src) {
  std::cerr << "MetalCmdList::copyToBuffer(): handle=" << handle.id
            << " dstOff=" << dstOff << " size=" << src.size() << std::endl;
  if (src.empty()) {
    std::cerr << "  WARNING: Source span is empty, skipping copy" << std::endl;
    return;
  }

  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end()) {
    std::cerr << "  ERROR: Buffer handle not found" << std::endl;
    return;
  }

  MTLBufferResource &buffer = it->second;
  std::cerr << "  Target buffer size=" << buffer.size
            << " hostVisible=" << (buffer.host_visible ? "YES" : "NO")
            << std::endl;

  if (dstOff + src.size() > buffer.size) {
    std::cerr << "Buffer copy out of bounds" << std::endl;
    return;
  }

  if (buffer.host_visible) {
    uint8_t *contents = (uint8_t *)buffer.buffer.contents;
    memcpy(contents + dstOff, src.data(), src.size());
    if ([buffer.buffer respondsToSelector:@selector(didModifyRange:)]) {
      [buffer.buffer didModifyRange:NSMakeRange(dstOff, src.size())];
    }
    std::cerr << "  ✓ Wrote " << src.size()
              << " bytes directly into host-visible buffer" << std::endl;
    return;
  }

  if (!impl_->command_buffer_) {
    std::cerr << "Metal command buffer not initialized before buffer upload"
              << std::endl;
    return;
  }

  id<MTLBuffer> staging = [impl_->device_
      newBufferWithLength:src.size()
                  options:(MTLResourceStorageModeShared |
                           MTLResourceCPUCacheModeWriteCombined)];

  if (!staging) {
    std::cerr << "Failed to allocate Metal staging buffer for upload"
              << std::endl;
    return;
  }

  memcpy(staging.contents, src.data(), src.size());
  if ([staging respondsToSelector:@selector(didModifyRange:)]) {
    [staging didModifyRange:NSMakeRange(0, src.size())];
  }

  std::cerr << "  Using staging upload path (" << src.size()
            << " bytes). Pending command buffer="
            << (impl_->command_buffer_ ? "YES" : "NO") << std::endl;

  impl_->endRenderEncoderIfNeeded();
  impl_->endComputeEncoderIfNeeded();

  id<MTLBlitCommandEncoder> blit = [impl_->command_buffer_ blitCommandEncoder];
  if (!blit) {
    std::cerr << "Failed to create Metal blit encoder for buffer upload"
              << std::endl;
    return;
  }
  [blit copyFromBuffer:staging
           sourceOffset:0
               toBuffer:buffer.buffer
      destinationOffset:dstOff
                   size:src.size()];
  [blit endEncoding];

  impl_->staging_uploads_.push_back(staging);
  std::cerr << "  ✓ Enqueued staging copy for submission" << std::endl;
}

void MetalCmdList::end() {
  impl_->resetEncoders();

  if (impl_->command_buffer_) {
    if (impl_->current_drawable_) {
      [impl_->command_buffer_ presentDrawable:impl_->current_drawable_];
    }

    [impl_->command_buffer_ commit];
  }

  impl_->staging_uploads_.clear();
  impl_->resetUniformBlock();
  impl_->command_buffer_ = nil;
  impl_->current_drawable_ = nil;
  impl_->recording_ = false;
  impl_->active_encoder_ = Impl::EncoderState::None;

  (*impl_->frame_index_) = ((*impl_->frame_index_) + 1) % kFramesInFlight;
}

} // namespace pixel::rhi

#endif // __APPLE__
