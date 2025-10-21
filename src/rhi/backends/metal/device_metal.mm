// src/rhi/backends/metal/device_metal.mm
// Metal Device Implementation
#ifdef __APPLE__

#include "device_metal.hpp"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>

#include <iostream>
#include <cstring>

namespace pixel::rhi {

// ============================================================================
// Uniforms Structure (matches shaders.metal)
// ============================================================================
struct Uniforms {
  float model[16];
  float view[16];
  float projection[16];
  float lightPos[3];
  float _pad1;
  float viewPos[3];
  float _pad2;
  float time;
  int useTexture;
  int useTextureArray;
  int ditherEnabled;
  float ditherScale;
  float crossfadeDuration;
  float _pad3[2];
};

// ============================================================================
// Metal Resource Structures
// ============================================================================
struct MTLBufferResource {
  id<MTLBuffer> buffer = nil;
  size_t size = 0;
  bool host_visible = false;
};

struct MTLTextureResource {
  id<MTLTexture> texture = nil;
  int width = 0;
  int height = 0;
  int layers = 1;
  Format format = Format::Unknown;
};

struct MTLSamplerResource {
  id<MTLSamplerState> sampler = nil;
};

struct MTLShaderResource {
  id<MTLFunction> function = nil;
  std::string stage;
};

struct MTLPipelineResource {
  id<MTLRenderPipelineState> pipeline_state = nil;
  id<MTLComputePipelineState> compute_pipeline_state = nil;
  id<MTLDepthStencilState> depth_stencil_state = nil;
};

// ============================================================================
// Helper Functions
// ============================================================================
static MTLPixelFormat toMTLFormat(Format format) {
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

static size_t getBytesPerPixel(Format format) {
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

// ============================================================================
// MetalDevice::Impl - Private Implementation
// ============================================================================
struct MetalDevice::Impl {
  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> command_queue_ = nil;
  CAMetalLayer *layer_ = nil;
  id<MTLTexture> depth_texture_ = nil;
  id<MTLDepthStencilState> default_depth_stencil_ = nil;
  id<MTLLibrary> library_ = nil;
  id<MTLBuffer> uniform_buffer_ = nil;

  std::unordered_map<uint32_t, MTLBufferResource> buffers_;
  std::unordered_map<uint32_t, MTLTextureResource> textures_;
  std::unordered_map<uint32_t, MTLSamplerResource> samplers_;
  std::unordered_map<uint32_t, MTLShaderResource> shaders_;
  std::unordered_map<uint32_t, MTLPipelineResource> pipelines_;

  uint32_t next_buffer_id_ = 1;
  uint32_t next_texture_id_ = 1;
  uint32_t next_sampler_id_ = 1;
  uint32_t next_shader_id_ = 1;
  uint32_t next_pipeline_id_ = 1;

  std::unique_ptr<MetalCmdList> immediate_;

  Impl(id<MTLDevice> device, CAMetalLayer *layer, id<MTLTexture> depth_texture)
      : device_(device), layer_(layer), depth_texture_(depth_texture) {
    command_queue_ = [device_ newCommandQueue];

    // Create default depth stencil state
    MTLDepthStencilDescriptor *depthDesc =
        [[MTLDepthStencilDescriptor alloc] init];
    depthDesc.depthCompareFunction = MTLCompareFunctionLess;
    depthDesc.depthWriteEnabled = YES;
    default_depth_stencil_ =
        [device_ newDepthStencilStateWithDescriptor:depthDesc];

    // Initialize uniform buffer
    size_t uniformSize = sizeof(Uniforms);
    uniform_buffer_ =
        [device_ newBufferWithLength:uniformSize
                             options:MTLResourceStorageModeShared];
    memset(uniform_buffer_.contents, 0, uniformSize);

    immediate_ = std::make_unique<MetalCmdList>(this);
  }

  ~Impl() {
    // ARC handles cleanup
    for (auto &pair : buffers_)
      pair.second.buffer = nil;
    for (auto &pair : textures_)
      pair.second.texture = nil;
    for (auto &pair : samplers_)
      pair.second.sampler = nil;
    for (auto &pair : shaders_)
      pair.second.function = nil;
    for (auto &pair : pipelines_) {
      pair.second.pipeline_state = nil;
      pair.second.compute_pipeline_state = nil;
      pair.second.depth_stencil_state = nil;
    }
  }
};

// ============================================================================
// MetalDevice Implementation
// ============================================================================
MetalDevice::MetalDevice(void *device, void *layer, void *depth_texture)
    : impl_(std::make_unique<Impl>((__bridge id<MTLDevice>)device,
                                   (__bridge CAMetalLayer *)layer,
                                   (__bridge id<MTLTexture>)depth_texture)) {}

MetalDevice::~MetalDevice() = default;

const Caps &MetalDevice::caps() const {
  static Caps caps;
  caps.instancing = true;
  caps.samplerAniso = true;
  caps.uniformBuffers = true;
  caps.clipSpaceYDown = true; // Metal uses Y-down clip space
  return caps;
}

BufferHandle MetalDevice::createBuffer(const BufferDesc &desc) {
  MTLBufferResource buffer;
  buffer.size = desc.size;
  buffer.host_visible = desc.hostVisible;

  MTLResourceOptions options = buffer.host_visible
                                   ? MTLResourceStorageModeShared
                                   : MTLResourceStorageModePrivate;

  buffer.buffer = [impl_->device_ newBufferWithLength:desc.size
                                              options:options];

  if (!buffer.buffer) {
    std::cerr << "Failed to create Metal buffer" << std::endl;
    return BufferHandle{0};
  }

  // No initial data upload - data is uploaded via copyToBuffer
  // This matches the OpenGL backend behavior

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

// Part 2: Sampler, Shader, and Pipeline Creation

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
  MTLShaderResource shader;

  // Load default library
  if (!impl_->library_) {
    impl_->library_ = [impl_->device_ newDefaultLibrary];
    if (!impl_->library_) {
      std::cerr << "Failed to load default Metal library" << std::endl;
      return ShaderHandle{0};
    }
  }

  // Map stage to function name
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
  } else {
    std::cerr << "Unknown shader stage: " << stage << std::endl;
    return ShaderHandle{0};
  }

  shader.function = [impl_->library_ newFunctionWithName:functionName];
  shader.stage = std::string(stage);

  if (!shader.function) {
    std::cerr << "Failed to load shader function: " << [functionName UTF8String]
              << std::endl;
    return ShaderHandle{0};
  }

  uint32_t handle_id = impl_->next_shader_id_++;
  impl_->shaders_[handle_id] = shader;
  return ShaderHandle{handle_id};
}

PipelineHandle MetalDevice::createPipeline(const PipelineDesc &desc) {
  MTLPipelineResource pipeline;

  // Check if compute pipeline
  if (desc.cs.id != 0) {
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
      std::cerr << "Failed to create compute pipeline: " <<
          [[error localizedDescription] UTF8String] << std::endl;
      return PipelineHandle{0};
    }

    uint32_t handle_id = impl_->next_pipeline_id_++;
    impl_->pipelines_[handle_id] = pipeline;
    return PipelineHandle{handle_id};
  }

  // Render pipeline
  auto vs_it = impl_->shaders_.find(desc.vs.id);
  auto fs_it = impl_->shaders_.find(desc.fs.id);

  if (vs_it == impl_->shaders_.end() || fs_it == impl_->shaders_.end()) {
    std::cerr << "Vertex or fragment shader not found" << std::endl;
    return PipelineHandle{0};
  }

  // Create pipeline descriptor
  MTLRenderPipelineDescriptor *pipelineDesc =
      [[MTLRenderPipelineDescriptor alloc] init];
  pipelineDesc.vertexFunction = vs_it->second.function;
  pipelineDesc.fragmentFunction = fs_it->second.function;
  pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  pipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

  // Configure blending
  pipelineDesc.colorAttachments[0].blendingEnabled = YES;
  pipelineDesc.colorAttachments[0].sourceRGBBlendFactor =
      MTLBlendFactorSourceAlpha;
  pipelineDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
  pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
  pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;

  // Setup vertex descriptor for Vertex layout (48 bytes)
  MTLVertexDescriptor *vertexDesc = [[MTLVertexDescriptor alloc] init];

  // Position (vec3, offset 0, location 0)
  vertexDesc.attributes[0].format = MTLVertexFormatFloat3;
  vertexDesc.attributes[0].offset = 0;
  vertexDesc.attributes[0].bufferIndex = 0;

  // Normal (vec3, offset 12, location 1)
  vertexDesc.attributes[1].format = MTLVertexFormatFloat3;
  vertexDesc.attributes[1].offset = 12;
  vertexDesc.attributes[1].bufferIndex = 0;

  // TexCoord (vec2, offset 24, location 2)
  vertexDesc.attributes[2].format = MTLVertexFormatFloat2;
  vertexDesc.attributes[2].offset = 24;
  vertexDesc.attributes[2].bufferIndex = 0;

  // Color (vec4, offset 32, location 3)
  vertexDesc.attributes[3].format = MTLVertexFormatFloat4;
  vertexDesc.attributes[3].offset = 32;
  vertexDesc.attributes[3].bufferIndex = 0;

  // Vertex buffer layout (stride = 48)
  vertexDesc.layouts[0].stride = 48;
  vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

  pipelineDesc.vertexDescriptor = vertexDesc;

  NSError *error = nil;
  pipeline.pipeline_state =
      [impl_->device_ newRenderPipelineStateWithDescriptor:pipelineDesc
                                                     error:&error];

  if (!pipeline.pipeline_state) {
    std::cerr << "Failed to create render pipeline: " <<
        [[error localizedDescription] UTF8String] << std::endl;
    return PipelineHandle{0};
  }

  // Use default depth stencil state
  pipeline.depth_stencil_state = impl_->default_depth_stencil_;

  uint32_t handle_id = impl_->next_pipeline_id_++;
  impl_->pipelines_[handle_id] = pipeline;
  return PipelineHandle{handle_id};
}

CmdList *MetalDevice::getImmediate() { return impl_->immediate_.get(); }

void MetalDevice::present() {
  // Present is handled by the command list
}

// Part 3: MetalCmdList Implementation - Core and Setup

// ============================================================================
// MetalCmdList::Impl - Private Implementation
// ============================================================================
struct MetalCmdList::Impl {
  id<MTLDevice> device_;
  id<MTLCommandQueue> command_queue_;
  CAMetalLayer *layer_;
  id<MTLTexture> depth_texture_;
  id<MTLBuffer> uniform_buffer_;

  std::unordered_map<uint32_t, MTLBufferResource> *buffers_;
  std::unordered_map<uint32_t, MTLTextureResource> *textures_;
  std::unordered_map<uint32_t, MTLPipelineResource> *pipelines_;

  id<MTLCommandBuffer> command_buffer_ = nil;
  id<CAMetalDrawable> current_drawable_ = nil;
  id<MTLRenderCommandEncoder> render_encoder_ = nil;
  id<MTLComputeCommandEncoder> compute_encoder_ = nil;
  id<MTLSamplerState> default_sampler_ = nil;

  bool recording_ = false;
  PipelineHandle current_pipeline_{0};
  PipelineHandle current_compute_pipeline_{0};
  BufferHandle current_vb_{0};
  BufferHandle current_ib_{0};
  size_t current_vb_offset_ = 0;
  size_t current_ib_offset_ = 0;

  Impl(MetalDevice::Impl *device_impl)
      : device_(device_impl->device_),
        command_queue_(device_impl->command_queue_),
        layer_(device_impl->layer_),
        depth_texture_(device_impl->depth_texture_),
        uniform_buffer_(device_impl->uniform_buffer_),
        buffers_(&device_impl->buffers_), textures_(&device_impl->textures_),
        pipelines_(&device_impl->pipelines_) {}

  ~Impl() {
    // ARC handles cleanup
  }
};

// ============================================================================
// MetalCmdList Implementation
// ============================================================================
MetalCmdList::MetalCmdList(MetalDevice::Impl *device_impl)
    : impl_(std::make_unique<Impl>(device_impl)) {}

MetalCmdList::~MetalCmdList() = default;

void MetalCmdList::begin() {
  impl_->recording_ = true;
  impl_->command_buffer_ = [impl_->command_queue_ commandBuffer];
}

void MetalCmdList::beginRender(TextureHandle rtColor, TextureHandle rtDepth,
                               float clear[4], float clearDepth,
                               uint8_t clearStencil) {
  impl_->current_drawable_ = [impl_->layer_ nextDrawable];

  if (!impl_->current_drawable_) {
    std::cerr << "Failed to get next drawable" << std::endl;
    return;
  }

  MTLRenderPassDescriptor *renderPassDesc =
      [MTLRenderPassDescriptor renderPassDescriptor];

  // Color attachment
  renderPassDesc.colorAttachments[0].texture = impl_->current_drawable_.texture;
  renderPassDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
  renderPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
  renderPassDesc.colorAttachments[0].clearColor =
      MTLClearColorMake(clear[0], clear[1], clear[2], clear[3]);

  // Depth attachment
  renderPassDesc.depthAttachment.texture = impl_->depth_texture_;
  renderPassDesc.depthAttachment.loadAction = MTLLoadActionClear;
  renderPassDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
  renderPassDesc.depthAttachment.clearDepth = clearDepth;

  impl_->render_encoder_ = [impl_->command_buffer_
      renderCommandEncoderWithDescriptor:renderPassDesc];
}

void MetalCmdList::setPipeline(PipelineHandle handle) {
  auto it = impl_->pipelines_->find(handle.id);
  if (it == impl_->pipelines_->end())
    return;

  impl_->current_pipeline_ = handle;
  const MTLPipelineResource &pipeline = it->second;

  [impl_->render_encoder_ setRenderPipelineState:pipeline.pipeline_state];
  [impl_->render_encoder_ setDepthStencilState:pipeline.depth_stencil_state];
  [impl_->render_encoder_ setCullMode:MTLCullModeBack];
  [impl_->render_encoder_ setFrontFacingWinding:MTLWindingCounterClockwise];
}

void MetalCmdList::setVertexBuffer(BufferHandle handle, size_t offset) {
  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end())
    return;

  impl_->current_vb_ = handle;
  impl_->current_vb_offset_ = offset;

  [impl_->render_encoder_ setVertexBuffer:it->second.buffer
                                   offset:offset
                                  atIndex:0];
}

void MetalCmdList::setIndexBuffer(BufferHandle handle, size_t offset) {
  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end())
    return;

  impl_->current_ib_ = handle;
  impl_->current_ib_offset_ = offset;
}

void MetalCmdList::setInstanceBuffer(BufferHandle handle, size_t stride,
                                     size_t offset) {
  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end())
    return;

  // Bind instance buffer at index 2
  [impl_->render_encoder_ setVertexBuffer:it->second.buffer
                                   offset:offset
                                  atIndex:2];
}

// Part 4: Uniform Setters

void MetalCmdList::setUniformMat4(const char *name, const float *mat4x4) {
  Uniforms *uniforms = (Uniforms *)impl_->uniform_buffer_.contents;
  std::string name_str(name);

  if (name_str == "model") {
    memcpy(uniforms->model, mat4x4, sizeof(float) * 16);
  } else if (name_str == "view") {
    memcpy(uniforms->view, mat4x4, sizeof(float) * 16);
  } else if (name_str == "projection") {
    memcpy(uniforms->projection, mat4x4, sizeof(float) * 16);
  }

  // Bind uniform buffer to both vertex and fragment shaders (index 1)
  [impl_->render_encoder_ setVertexBuffer:impl_->uniform_buffer_
                                   offset:0
                                  atIndex:1];
  [impl_->render_encoder_ setFragmentBuffer:impl_->uniform_buffer_
                                     offset:0
                                    atIndex:1];
}

void MetalCmdList::setUniformVec3(const char *name, const float *vec3) {
  Uniforms *uniforms = (Uniforms *)impl_->uniform_buffer_.contents;
  std::string name_str(name);

  if (name_str == "lightPos") {
    uniforms->lightPos[0] = vec3[0];
    uniforms->lightPos[1] = vec3[1];
    uniforms->lightPos[2] = vec3[2];
  } else if (name_str == "viewPos") {
    uniforms->viewPos[0] = vec3[0];
    uniforms->viewPos[1] = vec3[1];
    uniforms->viewPos[2] = vec3[2];
  }

  [impl_->render_encoder_ setVertexBuffer:impl_->uniform_buffer_
                                   offset:0
                                  atIndex:1];
  [impl_->render_encoder_ setFragmentBuffer:impl_->uniform_buffer_
                                     offset:0
                                    atIndex:1];
}

void MetalCmdList::setUniformVec4(const char *name, const float *vec4) {
  // Currently not used in shaders, but available for future use
  [impl_->render_encoder_ setVertexBuffer:impl_->uniform_buffer_
                                   offset:0
                                  atIndex:1];
  [impl_->render_encoder_ setFragmentBuffer:impl_->uniform_buffer_
                                     offset:0
                                    atIndex:1];
}

void MetalCmdList::setUniformInt(const char *name, int value) {
  Uniforms *uniforms = (Uniforms *)impl_->uniform_buffer_.contents;
  std::string name_str(name);

  if (name_str == "useTexture") {
    uniforms->useTexture = value;
  } else if (name_str == "useTextureArray") {
    uniforms->useTextureArray = value;
  } else if (name_str == "ditherEnabled" || name_str == "uDitherEnabled") {
    uniforms->ditherEnabled = value;
  }

  [impl_->render_encoder_ setVertexBuffer:impl_->uniform_buffer_
                                   offset:0
                                  atIndex:1];
  [impl_->render_encoder_ setFragmentBuffer:impl_->uniform_buffer_
                                     offset:0
                                    atIndex:1];
}

void MetalCmdList::setUniformFloat(const char *name, float value) {
  Uniforms *uniforms = (Uniforms *)impl_->uniform_buffer_.contents;
  std::string name_str(name);

  if (name_str == "time" || name_str == "uTime") {
    uniforms->time = value;
  } else if (name_str == "uDitherScale") {
    uniforms->ditherScale = value;
  } else if (name_str == "uCrossfadeDuration") {
    uniforms->crossfadeDuration = value;
  }

  [impl_->render_encoder_ setVertexBuffer:impl_->uniform_buffer_
                                   offset:0
                                  atIndex:1];
  [impl_->render_encoder_ setFragmentBuffer:impl_->uniform_buffer_
                                     offset:0
                                    atIndex:1];
}

void MetalCmdList::setUniformBuffer(uint32_t binding, BufferHandle buffer,
                                    size_t offset, size_t size) {
  auto it = impl_->buffers_->find(buffer.id);
  if (it == impl_->buffers_->end())
    return;

  [impl_->render_encoder_ setVertexBuffer:it->second.buffer
                                   offset:offset
                                  atIndex:binding];
  [impl_->render_encoder_ setFragmentBuffer:it->second.buffer
                                     offset:offset
                                    atIndex:binding];
}

void MetalCmdList::setTexture(const char *name, TextureHandle texture,
                              uint32_t slot) {
  auto it = impl_->textures_->find(texture.id);
  if (it == impl_->textures_->end())
    return;

  [impl_->render_encoder_ setFragmentTexture:it->second.texture atIndex:slot];

  // Create default sampler if needed
  if (!impl_->default_sampler_) {
    MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
    samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
    samplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
    samplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;
    impl_->default_sampler_ =
        [impl_->device_ newSamplerStateWithDescriptor:samplerDesc];
  }

  [impl_->render_encoder_ setFragmentSamplerState:impl_->default_sampler_
                                          atIndex:slot];
}

// Part 5: Texture Operations and Drawing

void MetalCmdList::copyToTexture(TextureHandle texture, uint32_t mipLevel,
                                 std::span<const std::byte> data) {
  auto it = impl_->textures_->find(texture.id);
  if (it == impl_->textures_->end())
    return;

  const MTLTextureResource &tex = it->second;
  size_t bytesPerRow = tex.width * getBytesPerPixel(tex.format);
  MTLRegion region = MTLRegionMake2D(0, 0, tex.width, tex.height);

  [tex.texture replaceRegion:region
                 mipmapLevel:mipLevel
                       slice:0
                   withBytes:data.data()
                 bytesPerRow:bytesPerRow
               bytesPerImage:0];
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

  size_t bytesPerRow = tex.width * getBytesPerPixel(tex.format);
  MTLRegion region = MTLRegionMake2D(0, 0, tex.width, tex.height);

  [tex.texture replaceRegion:region
                 mipmapLevel:mipLevel
                       slice:layer
                   withBytes:data.data()
                 bytesPerRow:bytesPerRow
               bytesPerImage:0];
}

void MetalCmdList::drawIndexed(uint32_t indexCount, uint32_t firstIndex,
                               uint32_t instanceCount) {
  if (impl_->current_pipeline_.id == 0 || impl_->current_ib_.id == 0)
    return;

  auto ib_it = impl_->buffers_->find(impl_->current_ib_.id);
  if (ib_it == impl_->buffers_->end())
    return;

  size_t indexOffset =
      impl_->current_ib_offset_ + firstIndex * sizeof(uint32_t);

  [impl_->render_encoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                     indexCount:indexCount
                                      indexType:MTLIndexTypeUInt32
                                    indexBuffer:ib_it->second.buffer
                              indexBufferOffset:indexOffset
                                  instanceCount:instanceCount];
}

void MetalCmdList::setComputePipeline(PipelineHandle handle) {
  auto it = impl_->pipelines_->find(handle.id);
  if (it == impl_->pipelines_->end())
    return;

  impl_->current_compute_pipeline_ = handle;
  const MTLPipelineResource &pipeline = it->second;

  // End render encoder if active
  if (impl_->render_encoder_) {
    [impl_->render_encoder_ endEncoding];
    impl_->render_encoder_ = nil;
  }

  // Create compute encoder if needed
  if (!impl_->compute_encoder_) {
    impl_->compute_encoder_ = [impl_->command_buffer_ computeCommandEncoder];
  }

  [impl_->compute_encoder_
      setComputePipelineState:pipeline.compute_pipeline_state];
}

void MetalCmdList::setStorageBuffer(uint32_t binding, BufferHandle buffer,
                                    size_t offset, size_t size) {
  auto it = impl_->buffers_->find(buffer.id);
  if (it == impl_->buffers_->end())
    return;

  const MTLBufferResource &buf = it->second;

  if (impl_->compute_encoder_) {
    [impl_->compute_encoder_ setBuffer:buf.buffer
                                offset:offset
                               atIndex:binding];
  }
}

void MetalCmdList::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                            uint32_t groupCountZ) {
  if (!impl_->compute_encoder_)
    return;

  auto it = impl_->pipelines_->find(impl_->current_compute_pipeline_.id);
  if (it == impl_->pipelines_->end())
    return;

  NSUInteger maxThreads =
      it->second.compute_pipeline_state.maxTotalThreadsPerThreadgroup;

  MTLSize threadsPerGroup = MTLSizeMake(MIN(256, maxThreads), 1, 1);
  MTLSize threadgroups = MTLSizeMake(groupCountX, groupCountY, groupCountZ);

  [impl_->compute_encoder_ dispatchThreadgroups:threadgroups
                          threadsPerThreadgroup:threadsPerGroup];
}

void MetalCmdList::memoryBarrier() {
  // Metal handles memory barriers automatically between encoders
}

void MetalCmdList::endRender() {
  if (impl_->render_encoder_) {
    [impl_->render_encoder_ endEncoding];
    impl_->render_encoder_ = nil;
  }
  if (impl_->compute_encoder_) {
    [impl_->compute_encoder_ endEncoding];
    impl_->compute_encoder_ = nil;
  }
}

void MetalCmdList::copyToBuffer(BufferHandle handle, size_t dstOff,
                                std::span<const std::byte> src) {
  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end())
    return;

  MTLBufferResource &buffer = it->second;

  if (!buffer.host_visible) {
    std::cerr << "Cannot copy to non-host-visible buffer" << std::endl;
    return;
  }

  if (dstOff + src.size() > buffer.size) {
    std::cerr << "Buffer copy out of bounds" << std::endl;
    return;
  }

  uint8_t *contents = (uint8_t *)buffer.buffer.contents;
  memcpy(contents + dstOff, src.data(), src.size());
}

void MetalCmdList::end() {
  if (impl_->current_drawable_ && impl_->command_buffer_) {
    [impl_->command_buffer_ presentDrawable:impl_->current_drawable_];
    [impl_->command_buffer_ commit];
  }

  impl_->command_buffer_ = nil;
  impl_->current_drawable_ = nil;
  impl_->recording_ = false;
}

// Part 6: Device Factory Function

// ============================================================================
// Device Creation Factory
// ============================================================================

Device *create_metal_device(void *window) {
  GLFWwindow *glfwWindow = static_cast<GLFWwindow *>(window);
  NSWindow *nsWindow = glfwGetCocoaWindow(glfwWindow);

  if (!nsWindow) {
    std::cerr << "Failed to get Cocoa window" << std::endl;
    return nullptr;
  }

  // Create Metal device
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (!device) {
    std::cerr << "Metal is not supported on this device" << std::endl;
    return nullptr;
  }

  std::cout << "Metal device: " << [[device name] UTF8String] << std::endl;

  // Create Metal layer
  CAMetalLayer *metalLayer = [CAMetalLayer layer];
  metalLayer.device = device;
  metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metalLayer.framebufferOnly = NO;

  // Set layer on the window
  nsWindow.contentView.layer = metalLayer;
  nsWindow.contentView.wantsLayer = YES;

  // Get window size
  int width, height;
  glfwGetFramebufferSize(glfwWindow, &width, &height);
  metalLayer.drawableSize = CGSizeMake(width, height);

  // Create depth texture
  MTLTextureDescriptor *depthDesc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                   width:width
                                  height:height
                               mipmapped:NO];
  depthDesc.usage = MTLTextureUsageRenderTarget;
  depthDesc.storageMode = MTLStorageModePrivate;

  id<MTLTexture> depthTexture = [device newTextureWithDescriptor:depthDesc];

  if (!depthTexture) {
    std::cerr << "Failed to create depth texture" << std::endl;
    return nullptr;
  }

  // Create and return device (bridging for C++ ownership)
  return new MetalDevice((__bridge void *)device, (__bridge void *)metalLayer,
                         (__bridge void *)depthTexture);
}

} // namespace pixel::rhi

#endif // __APPLE__
