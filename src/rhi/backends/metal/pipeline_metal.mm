// src/rhi/backends/metal/pipeline_metal.mm
// Metal pipeline construction
#ifdef __APPLE__

#include "metal_internal.hpp"

namespace pixel::rhi {

PipelineHandle MetalDevice::createPipeline(const PipelineDesc &desc) {
  MTLPipelineResource pipeline;

  if (desc.cs.id != 0) {
    PipelineCacheKey cacheKey{};
    cacheKey.cs_id = desc.cs.id;
    auto cached = impl_->pipeline_cache_.find(cacheKey);
    if (cached != impl_->pipeline_cache_.end()) {
      return cached->second;
    }

    auto cs_it = impl_->shaders_.find(desc.cs.id);
    if (cs_it == impl_->shaders_.end()) {
      std::cerr << "Compute shader not found" << std::endl;
      return PipelineHandle{0};
    }

    NSError *error = nil;
    pipeline.compute_pipeline_state = [impl_->device_
        newComputePipelineStateWithFunction:cs_it->second.function
                                      error:&error];

    if (!pipeline.compute_pipeline_state) {
      std::cerr << "Failed to create compute pipeline: "
                << [[error localizedDescription] UTF8String] << std::endl;
      return PipelineHandle{0};
    }

    uint32_t handle_id = impl_->next_pipeline_id_++;
    impl_->pipelines_[handle_id] = pipeline;

    PipelineHandle handle{handle_id};
    impl_->pipeline_cache_[cacheKey] = handle;
    return handle;
  }

  auto vs_it = impl_->shaders_.find(desc.vs.id);
  auto fs_it = impl_->shaders_.find(desc.fs.id);

  if (vs_it == impl_->shaders_.end() || fs_it == impl_->shaders_.end()) {
    std::cerr << "Vertex or fragment shader not found" << std::endl;
    return PipelineHandle{0};
  }

  bool isInstanced = (vs_it->second.stage == "vs_instanced");

  PipelineCacheKey cacheKey{};
  cacheKey.vs_id = desc.vs.id;
  cacheKey.fs_id = desc.fs.id;
  cacheKey.instanced = isInstanced;

  std::array<ColorAttachmentDesc, kMaxColorAttachments> attachments{};
  uint32_t colorAttachmentCount = desc.colorAttachmentCount;
  if (colorAttachmentCount > kMaxColorAttachments) {
    colorAttachmentCount = kMaxColorAttachments;
  }
  if (colorAttachmentCount == 0) {
    colorAttachmentCount = 1;
    attachments[0].format = Format::BGRA8;
    attachments[0].blend = make_alpha_blend_state();
  } else {
    for (uint32_t i = 0; i < colorAttachmentCount && i < kMaxColorAttachments;
         ++i) {
      attachments[i] = desc.colorAttachments[i];
    }
  }

  cacheKey.color_attachment_count = colorAttachmentCount;
  cacheKey.color_attachments = attachments;

  auto cached = impl_->pipeline_cache_.find(cacheKey);
  if (cached != impl_->pipeline_cache_.end()) {
    return cached->second;
  }

  MTLRenderPipelineDescriptor *pipelineDesc =
      [[MTLRenderPipelineDescriptor alloc] init];
  pipelineDesc.vertexFunction = vs_it->second.function;
  pipelineDesc.fragmentFunction = fs_it->second.function;
  for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
    const auto &attachment = attachments[i];
    pipelineDesc.colorAttachments[i].pixelFormat =
        toMTLFormat(attachment.format);
    pipelineDesc.colorAttachments[i].writeMask = MTLColorWriteMaskAll;
    if (attachment.blend.enabled) {
      pipelineDesc.colorAttachments[i].blendingEnabled = YES;
      pipelineDesc.colorAttachments[i].sourceRGBBlendFactor =
          toMTLBlendFactor(attachment.blend.srcColor);
      pipelineDesc.colorAttachments[i].destinationRGBBlendFactor =
          toMTLBlendFactor(attachment.blend.dstColor);
      pipelineDesc.colorAttachments[i].rgbBlendOperation =
          toMTLBlendOp(attachment.blend.colorOp);
      pipelineDesc.colorAttachments[i].sourceAlphaBlendFactor =
          toMTLBlendFactor(attachment.blend.srcAlpha);
      pipelineDesc.colorAttachments[i].destinationAlphaBlendFactor =
          toMTLBlendFactor(attachment.blend.dstAlpha);
      pipelineDesc.colorAttachments[i].alphaBlendOperation =
          toMTLBlendOp(attachment.blend.alphaOp);
    } else {
      pipelineDesc.colorAttachments[i].blendingEnabled = NO;
      pipelineDesc.colorAttachments[i].sourceRGBBlendFactor = MTLBlendFactorOne;
      pipelineDesc.colorAttachments[i].destinationRGBBlendFactor =
          MTLBlendFactorZero;
      pipelineDesc.colorAttachments[i].rgbBlendOperation = MTLBlendOperationAdd;
      pipelineDesc.colorAttachments[i].sourceAlphaBlendFactor =
          MTLBlendFactorOne;
      pipelineDesc.colorAttachments[i].destinationAlphaBlendFactor =
          MTLBlendFactorZero;
      pipelineDesc.colorAttachments[i].alphaBlendOperation =
          MTLBlendOperationAdd;
    }
  }

  for (uint32_t i = colorAttachmentCount; i < kMaxColorAttachments; ++i) {
    pipelineDesc.colorAttachments[i].pixelFormat = MTLPixelFormatInvalid;
    pipelineDesc.colorAttachments[i].blendingEnabled = NO;
  }

  pipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

  MTLVertexDescriptor *vertexDesc =
      impl_->getOrCreateVertexDescriptor(isInstanced);

  pipelineDesc.vertexDescriptor = vertexDesc;

  NSError *error = nil;
  pipeline.pipeline_state =
      [impl_->device_ newRenderPipelineStateWithDescriptor:pipelineDesc
                                                     error:&error];

  if (!pipeline.pipeline_state) {
    std::cerr << "Failed to create render pipeline: "
              << [[error localizedDescription] UTF8String] << std::endl;
    return PipelineHandle{0};
  }

  pipeline.depth_stencil_state = impl_->default_depth_stencil_;

  uint32_t handle_id = impl_->next_pipeline_id_++;
  impl_->pipelines_[handle_id] = pipeline;

  PipelineHandle handle{handle_id};
  impl_->pipeline_cache_[cacheKey] = handle;
  return handle;
}

} // namespace pixel::rhi

#endif // __APPLE__
