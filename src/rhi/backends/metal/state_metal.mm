// src/rhi/backends/metal/state_metal.mm
// Metal state conversion and binding helpers
#ifdef __APPLE__

#include "pixel/rhi/backends/metal/metal_internal.hpp"

#include <string>
#include <iostream>

namespace pixel::rhi {

MTLCompareFunction to_mtl_compare(CompareOp op) {
  switch (op) {
  case CompareOp::Never:
    return MTLCompareFunctionNever;
  case CompareOp::Less:
    return MTLCompareFunctionLess;
  case CompareOp::Equal:
    return MTLCompareFunctionEqual;
  case CompareOp::LessEqual:
    return MTLCompareFunctionLessEqual;
  case CompareOp::Greater:
    return MTLCompareFunctionGreater;
  case CompareOp::NotEqual:
    return MTLCompareFunctionNotEqual;
  case CompareOp::GreaterEqual:
    return MTLCompareFunctionGreaterEqual;
  case CompareOp::Always:
  default:
    return MTLCompareFunctionAlways;
  }
}

MTLStencilOperation to_mtl_stencil(StencilOp op) {
  switch (op) {
  case StencilOp::Keep:
    return MTLStencilOperationKeep;
  case StencilOp::Zero:
    return MTLStencilOperationZero;
  case StencilOp::Replace:
    return MTLStencilOperationReplace;
  case StencilOp::IncrementClamp:
    return MTLStencilOperationIncrementClamp;
  case StencilOp::DecrementClamp:
    return MTLStencilOperationDecrementClamp;
  case StencilOp::Invert:
    return MTLStencilOperationInvert;
  case StencilOp::IncrementWrap:
    return MTLStencilOperationIncrementWrap;
  case StencilOp::DecrementWrap:
    return MTLStencilOperationDecrementWrap;
  default:
    return MTLStencilOperationKeep;
  }
}

MTLPixelFormat toMTLFormat(Format format) {
  switch (format) {
  case Format::RGBA8:
    return MTLPixelFormatRGBA8Unorm;
  case Format::BGRA8:
    return MTLPixelFormatBGRA8Unorm;
  case Format::R8:
    return MTLPixelFormatR8Unorm;
  case Format::R16F:
    return MTLPixelFormatR16Float;
  case Format::RG16F:
    return MTLPixelFormatRG16Float;
  case Format::RGBA16F:
    return MTLPixelFormatRGBA16Float;
  case Format::D24S8:
    return MTLPixelFormatDepth24Unorm_Stencil8;
  case Format::D32F:
    return MTLPixelFormatDepth32Float;
  default:
    return MTLPixelFormatRGBA8Unorm;
  }
}

size_t getBytesPerPixel(Format format) {
  switch (format) {
  case Format::RGBA8:
  case Format::BGRA8:
    return 4;
  case Format::R8:
    return 1;
  case Format::R16F:
    return 2;
  case Format::RG16F:
    return 4;
  case Format::RGBA16F:
    return 8;
  case Format::D24S8:
  case Format::D32F:
    return 4;
  default:
    return 4;
  }
}

MTLBlendFactor toMTLBlendFactor(BlendFactor factor) {
  switch (factor) {
  case BlendFactor::Zero:
    return MTLBlendFactorZero;
  case BlendFactor::One:
    return MTLBlendFactorOne;
  case BlendFactor::SrcColor:
    return MTLBlendFactorSourceColor;
  case BlendFactor::OneMinusSrcColor:
    return MTLBlendFactorOneMinusSourceColor;
  case BlendFactor::DstColor:
    return MTLBlendFactorDestinationColor;
  case BlendFactor::OneMinusDstColor:
    return MTLBlendFactorOneMinusDestinationColor;
  case BlendFactor::SrcAlpha:
    return MTLBlendFactorSourceAlpha;
  case BlendFactor::OneMinusSrcAlpha:
    return MTLBlendFactorOneMinusSourceAlpha;
  case BlendFactor::DstAlpha:
    return MTLBlendFactorDestinationAlpha;
  case BlendFactor::OneMinusDstAlpha:
    return MTLBlendFactorOneMinusDestinationAlpha;
  case BlendFactor::SrcAlphaSaturated:
    return MTLBlendFactorSourceAlphaSaturated;
  }
  return MTLBlendFactorOne;
}

MTLBlendOperation toMTLBlendOp(BlendOp op) {
  switch (op) {
  case BlendOp::Add:
    return MTLBlendOperationAdd;
  case BlendOp::Subtract:
    return MTLBlendOperationSubtract;
  case BlendOp::ReverseSubtract:
    return MTLBlendOperationReverseSubtract;
  case BlendOp::Min:
    return MTLBlendOperationMin;
  case BlendOp::Max:
    return MTLBlendOperationMax;
  }
  return MTLBlendOperationAdd;
}

MTLLoadAction toMTLLoadAction(LoadOp op) {
  switch (op) {
  case LoadOp::Load:
    return MTLLoadActionLoad;
  case LoadOp::Clear:
    return MTLLoadActionClear;
  case LoadOp::DontCare:
  default:
    return MTLLoadActionDontCare;
  }
}

MTLStoreAction toMTLStoreAction(StoreOp op) {
  switch (op) {
  case StoreOp::Store:
    return MTLStoreActionStore;
  case StoreOp::DontCare:
  default:
    return MTLStoreActionDontCare;
  }
}

void MetalCmdList::setDepthStencilState(const DepthStencilState &state) {
  std::cerr << "MetalCmdList::setDepthStencilState()" << std::endl;
  std::cerr << "  depthTestEnable=" << (state.depthTestEnable ? "true" : "false")
            << " depthWriteEnable="
            << (state.depthWriteEnable ? "true" : "false")
            << " depthCompare=" << static_cast<int>(state.depthCompare)
            << std::endl;
  std::cerr << "  stencilEnable=" << (state.stencilEnable ? "true" : "false")
            << " compare=" << static_cast<int>(state.stencilCompare)
            << " ref=" << state.stencilReference << std::endl;
  if (impl_->depth_stencil_state_initialized_ &&
      state == impl_->current_depth_stencil_state_) {
    std::cerr << "  Skipping depth stencil update (state unchanged)" << std::endl;
    return;
  }

  if (!impl_->render_encoder_) {
    std::cerr
        << "Attempted to set depth/stencil state without an active render pass"
        << std::endl;
    return;
  }

  impl_->depth_stencil_state_initialized_ = true;
  impl_->current_depth_stencil_state_ = state;

  if (!state.depthTestEnable && !state.stencilEnable) {
    [impl_->render_encoder_ setDepthStencilState:nil];
    std::cerr << "  Disabled depth/stencil state" << std::endl;
    return;
  }

  auto it = impl_->depth_stencil_cache_.find(state);
  id<MTLDepthStencilState> depth_state = nil;
  if (it != impl_->depth_stencil_cache_.end()) {
    depth_state = it->second;
    std::cerr << "  Reusing cached depth stencil state" << std::endl;
  } else {
    MTLDepthStencilDescriptor *descriptor =
        [[MTLDepthStencilDescriptor alloc] init];
    descriptor.depthCompareFunction = state.depthTestEnable
                                          ? to_mtl_compare(state.depthCompare)
                                          : MTLCompareFunctionAlways;
    descriptor.depthWriteEnabled = state.depthWriteEnable;

    if (state.stencilEnable) {
      MTLStencilDescriptor *front = [[MTLStencilDescriptor alloc] init];
      front.stencilCompareFunction = to_mtl_compare(state.stencilCompare);
      front.stencilFailureOperation = to_mtl_stencil(state.stencilFailOp);
      front.depthFailureOperation = to_mtl_stencil(state.stencilDepthFailOp);
      front.depthStencilPassOperation = to_mtl_stencil(state.stencilPassOp);
      front.readMask = state.stencilReadMask;
      front.writeMask = state.stencilWriteMask;

      MTLStencilDescriptor *back = [front copy];

      descriptor.frontFaceStencil = front;
      descriptor.backFaceStencil = back;
    }

    depth_state =
        [impl_->device_ newDepthStencilStateWithDescriptor:descriptor];

    if (!depth_state) {
      std::cerr << "Failed to create Metal depth stencil state" << std::endl;
      return;
    }

    impl_->depth_stencil_cache_[state] = depth_state;
    std::cerr << "  Created new depth stencil state" << std::endl;
  }

  [impl_->render_encoder_ setDepthStencilState:depth_state];
  if (state.stencilEnable) {
    [impl_->render_encoder_ setStencilReferenceValue:state.stencilReference];
    std::cerr << "  Stencil reference set to " << state.stencilReference
              << std::endl;
  }
}

void MetalCmdList::setDepthBias(const DepthBiasState &state) {
  std::cerr << "MetalCmdList::setDepthBias()" << std::endl;
  std::cerr << "  enable=" << (state.enable ? "true" : "false")
            << " constant=" << state.constantFactor
            << " slope=" << state.slopeFactor << std::endl;
  if (impl_->depth_bias_initialized_ &&
      state.enable == impl_->current_depth_bias_state_.enable &&
      state.constantFactor == impl_->current_depth_bias_state_.constantFactor &&
      state.slopeFactor == impl_->current_depth_bias_state_.slopeFactor) {
    std::cerr << "  Skipping depth bias update (state unchanged)" << std::endl;
    return;
  }

  if (!impl_->render_encoder_) {
    std::cerr << "Attempted to set depth bias without an active render pass"
              << std::endl;
    return;
  }

  impl_->depth_bias_initialized_ = true;
  impl_->current_depth_bias_state_ = state;

  if (state.enable) {
    [impl_->render_encoder_ setDepthBias:state.constantFactor
                              slopeScale:state.slopeFactor
                                   clamp:0.0f];
    std::cerr << "  Depth bias applied" << std::endl;
  } else {
    [impl_->render_encoder_ setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    std::cerr << "  Depth bias disabled" << std::endl;
  }
}

void MetalCmdList::setUniformMat4(const char *name, const float *mat4x4) {
  std::cerr << "MetalCmdList::setUniformMat4(" << name << ")" << std::endl;
  Uniforms *uniforms = impl_->getCurrentUniformSlot();
  if (!uniforms) {
    std::cerr << "  ERROR: No uniform slot available" << std::endl;
    return;
  }
  std::string name_str(name);

  if (name_str == "model") {
    memcpy(uniforms->model, mat4x4, sizeof(float) * 16);
  } else if (name_str == "view") {
    memcpy(uniforms->view, mat4x4, sizeof(float) * 16);
  } else if (name_str == "projection") {
    memcpy(uniforms->projection, mat4x4, sizeof(float) * 16);
  } else if (name_str == "normalMatrix") {
    memcpy(uniforms->normalMatrix, mat4x4, sizeof(float) * 16);
  } else if (name_str == "lightViewProj") {
    memcpy(uniforms->lightViewProj, mat4x4, sizeof(float) * 16);
  }

  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
}

void MetalCmdList::setUniformVec3(const char *name, const float *vec3) {
  std::cerr << "MetalCmdList::setUniformVec3(" << name << ") value=" << vec3[0]
            << "," << vec3[1] << "," << vec3[2] << std::endl;
  Uniforms *uniforms = impl_->getCurrentUniformSlot();
  if (!uniforms) {
    std::cerr << "  ERROR: No uniform slot available" << std::endl;
    return;
  }
  std::string name_str(name);

  if (name_str == "lightPos") {
    memcpy(uniforms->lightPos, vec3, sizeof(float) * 3);
    uniforms->lightPos[3] = 0.0f;
  } else if (name_str == "viewPos") {
    memcpy(uniforms->viewPos, vec3, sizeof(float) * 3);
    uniforms->viewPos[3] = 0.0f;
  } else if (name_str == "lightColor") {
    memcpy(uniforms->lightColor, vec3, sizeof(float) * 3);
    uniforms->lightColor[3] = 0.0f;
  }

  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
}

void MetalCmdList::setUniformVec4(const char *name, const float *vec4) {
  std::cerr << "MetalCmdList::setUniformVec4(" << name << ") value=" << vec4[0]
            << "," << vec4[1] << "," << vec4[2] << "," << vec4[3]
            << std::endl;
  Uniforms *uniforms = impl_->getCurrentUniformSlot();
  if (!uniforms) {
    std::cerr << "  ERROR: No uniform slot available" << std::endl;
    return;
  }
  std::string name_str(name);

  if (name_str == "materialColor") {
    memcpy(uniforms->materialColor, vec4, sizeof(float) * 4);
  }

  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
}

void MetalCmdList::setUniformInt(const char *name, int value) {
  std::cerr << "MetalCmdList::setUniformInt(" << name << ") value=" << value
            << std::endl;
  Uniforms *uniforms = impl_->getCurrentUniformSlot();
  if (!uniforms) {
    std::cerr << "  ERROR: No uniform slot available" << std::endl;
    return;
  }
  std::string name_str(name);

  if (name_str == "useTexture") {
    uniforms->useTexture = value;
  } else if (name_str == "useTextureArray") {
    uniforms->useTextureArray = value;
  } else if (name_str == "ditherEnabled" || name_str == "uDitherEnabled") {
    uniforms->uDitherEnabled = value;
  } else if (name_str == "shadowsEnabled") {
    uniforms->shadowsEnabled = value;
  }

  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
}

void MetalCmdList::setUniformFloat(const char *name, float value) {
  std::cerr << "MetalCmdList::setUniformFloat(" << name << ") value=" << value
            << std::endl;
  Uniforms *uniforms = impl_->getCurrentUniformSlot();
  if (!uniforms) {
    std::cerr << "  ERROR: No uniform slot available" << std::endl;
    return;
  }
  std::string name_str(name);

  if (name_str == "time" || name_str == "uTime") {
    uniforms->uTime = value;
  } else if (name_str == "ditherScale" || name_str == "uDitherScale") {
    uniforms->ditherScale = value;
  } else if (name_str == "crossfadeDuration" ||
             name_str == "uCrossfadeDuration") {
    uniforms->crossfadeDuration = value;
  } else if (name_str == "shadowBias") {
    uniforms->shadowBias = value;
  } else if (name_str == "alphaCutoff") {
    uniforms->alphaCutoff = value;
  } else if (name_str == "baseAlpha") {
    uniforms->baseAlpha = value;
  }

  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
}

void MetalCmdList::setUniformBuffer(uint32_t binding, BufferHandle buffer,
                                    size_t offset, size_t size) {
  std::cerr << "MetalCmdList::setUniformBuffer(): binding=" << binding
            << " handle=" << buffer.id << " offset=" << offset
            << " size=" << size << std::endl;
  (void)size;
  auto it = impl_->buffers_->find(buffer.id);
  if (it == impl_->buffers_->end()) {
    std::cerr << "  ERROR: Uniform buffer handle not found" << std::endl;
    return;
  }

  if (!impl_->render_encoder_) {
    std::cerr << "  WARNING: Uniform buffer bound without active render encoder"
              << std::endl;
  }

  [impl_->render_encoder_ setVertexBuffer:it->second.buffer
                                   offset:offset
                                  atIndex:binding];
  [impl_->render_encoder_ setFragmentBuffer:it->second.buffer
                                     offset:offset
                                    atIndex:binding];
  std::cerr << "  Uniform buffer bound to render stages" << std::endl;
}

void MetalCmdList::setTexture(const char *name, TextureHandle texture,
                              uint32_t slot, SamplerHandle sampler) {
  (void)name;
  auto it = impl_->textures_->find(texture.id);
  if (it == impl_->textures_->end())
    return;

  [impl_->render_encoder_ setFragmentTexture:it->second.texture atIndex:slot];

  id<MTLSamplerState> sampler_state = nil;
  if (sampler.id != 0 && impl_->samplers_) {
    auto sit = impl_->samplers_->find(sampler.id);
    if (sit != impl_->samplers_->end()) {
      sampler_state = sit->second.sampler;
    }
  }

  if (!sampler_state) {
    if (!impl_->default_sampler_) {
      MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
      samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
      samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
      samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
      samplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
      samplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;
      samplerDesc.rAddressMode = MTLSamplerAddressModeRepeat;
      impl_->default_sampler_ =
          [impl_->device_ newSamplerStateWithDescriptor:samplerDesc];
    }
    sampler_state = impl_->default_sampler_;
  }

  if (sampler_state) {
    [impl_->render_encoder_ setFragmentSamplerState:sampler_state
                                            atIndex:slot];
  }
}

} // namespace pixel::rhi

#endif // __APPLE__
