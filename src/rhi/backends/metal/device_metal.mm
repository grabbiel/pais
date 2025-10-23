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
#import <dispatch/dispatch.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace pixel::rhi {

namespace {

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

} // namespace

// ============================================================================
// Uniforms Structure (matches shaders.metal)
// ============================================================================
struct Uniforms {
  float model[16];
  float view[16];
  float projection[16];
  float normalMatrix[16];
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
// Ring Buffer Constants
// ============================================================================
constexpr uint32_t kFramesInFlight = 3;          // Triple buffering
constexpr uint32_t kMaxDrawCallsPerFrame = 1024; // Maximum draws per frame
constexpr uint32_t kTotalUniformSlots = kFramesInFlight * kMaxDrawCallsPerFrame;

// ============================================================================
// Metal Resource Structures
// ============================================================================

struct UniformAllocator {
  struct Allocation {
    id<MTLBuffer> buffer = nil;
    size_t offset = 0;
    void *cpu_ptr = nullptr;
  };

  id<MTLBuffer> buffer = nil;
  size_t total_size = 0;
  size_t frame_size = 0;
  size_t cursor = 0;
  uint32_t frame_index = 0;

  bool initialize(id<MTLDevice> device, size_t size) {
    total_size = size;
    frame_size = size / kFramesInFlight;
    if (frame_size == 0) {
      frame_size = size;
    }
    buffer =
        [device newBufferWithLength:size
                            options:(MTLResourceStorageModeShared |
                                     MTLResourceCPUCacheModeWriteCombined)];
    if (!buffer) {
      return false;
    }

    memset(buffer.contents, 0, size);
    cursor = 0;
    frame_index = 0;
    return true;
  }

  void reset(uint32_t new_frame_index) {
    frame_index = new_frame_index % kFramesInFlight;
    cursor = std::min(frame_index * frame_size, total_size);
  }

  std::optional<Allocation> allocate(size_t size, size_t alignment) {
    if (!buffer || !buffer.contents) {
      return std::nullopt;
    }

    size_t frame_start = frame_index * frame_size;
    size_t frame_end = std::min(frame_start + frame_size, total_size);
    size_t aligned = (cursor + (alignment - 1)) & ~(alignment - 1);

    if (aligned + size > frame_end) {
      return std::nullopt;
    }

    cursor = aligned + size;

    Allocation alloc;
    alloc.buffer = buffer;
    alloc.offset = aligned;
    alloc.cpu_ptr = (uint8_t *)buffer.contents + aligned;
    return alloc;
  }
};

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

struct MTLFramebufferResource {
  FramebufferDesc desc{};
  uint32_t width{0};
  uint32_t height{0};
};

struct MetalQueryResource {
  QueryType type{QueryType::TimeElapsed};
  bool active{false};
  bool available{false};
  uint64_t result{0};
  id<MTLCommandBuffer> pending_command_buffer = nil;
};

struct MetalFenceResource {
  dispatch_semaphore_t semaphore = nullptr;
  bool signaled = false;
};

struct PipelineCacheKey {
  uint32_t vs_id{0};
  uint32_t fs_id{0};
  uint32_t cs_id{0};
  bool instanced{false};
  uint32_t color_attachment_count{0};
  std::array<ColorAttachmentDesc, kMaxColorAttachments> color_attachments{};

  bool operator==(const PipelineCacheKey &other) const {
    if (vs_id != other.vs_id || fs_id != other.fs_id || cs_id != other.cs_id ||
        instanced != other.instanced ||
        color_attachment_count != other.color_attachment_count) {
      return false;
    }

    for (uint32_t i = 0; i < color_attachment_count; ++i) {
      if (!(color_attachments[i] == other.color_attachments[i])) {
        return false;
      }
    }

    return true;
  }
};

struct PipelineCacheKeyHash {
  std::size_t operator()(const PipelineCacheKey &key) const noexcept {
    std::size_t seed = 0;
    auto hash_combine = [&seed](std::size_t value) {
      seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };
    hash_combine(std::hash<uint32_t>{}(key.vs_id));
    hash_combine(std::hash<uint32_t>{}(key.fs_id));
    hash_combine(std::hash<uint32_t>{}(key.cs_id));
    hash_combine(std::hash<bool>{}(key.instanced));
    hash_combine(std::hash<uint32_t>{}(key.color_attachment_count));

    using FormatUnderlying = std::underlying_type_t<Format>;
    using BlendFactorUnderlying = std::underlying_type_t<BlendFactor>;
    using BlendOpUnderlying = std::underlying_type_t<BlendOp>;

    for (uint32_t i = 0; i < key.color_attachment_count; ++i) {
      const auto &attachment = key.color_attachments[i];
      hash_combine(std::hash<FormatUnderlying>{}(
          static_cast<FormatUnderlying>(attachment.format)));
      const auto &blend = attachment.blend;
      hash_combine(std::hash<bool>{}(blend.enabled));
      hash_combine(std::hash<BlendFactorUnderlying>{}(
          static_cast<BlendFactorUnderlying>(blend.srcColor)));
      hash_combine(std::hash<BlendFactorUnderlying>{}(
          static_cast<BlendFactorUnderlying>(blend.dstColor)));
      hash_combine(std::hash<BlendOpUnderlying>{}(
          static_cast<BlendOpUnderlying>(blend.colorOp)));
      hash_combine(std::hash<BlendFactorUnderlying>{}(
          static_cast<BlendFactorUnderlying>(blend.srcAlpha)));
      hash_combine(std::hash<BlendFactorUnderlying>{}(
          static_cast<BlendFactorUnderlying>(blend.dstAlpha)));
      hash_combine(std::hash<BlendOpUnderlying>{}(
          static_cast<BlendOpUnderlying>(blend.alphaOp)));
    }
    return seed;
  }
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

static MTLBlendFactor toMTLBlendFactor(BlendFactor factor) {
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

static MTLBlendOperation toMTLBlendOp(BlendOp op) {
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

static MTLLoadAction toMTLLoadAction(LoadOp op) {
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

static MTLStoreAction toMTLStoreAction(StoreOp op) {
  switch (op) {
  case StoreOp::Store:
    return MTLStoreActionStore;
  case StoreOp::DontCare:
  default:
    return MTLStoreActionDontCare;
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
  UniformAllocator uniform_allocator_;

  std::unordered_map<uint32_t, MTLBufferResource> buffers_;
  std::unordered_map<uint32_t, MTLTextureResource> textures_;
  std::unordered_map<uint32_t, MTLSamplerResource> samplers_;
  std::unordered_map<uint32_t, MTLShaderResource> shaders_;
  std::unordered_map<uint32_t, MTLPipelineResource> pipelines_;
  std::unordered_map<uint32_t, MTLFramebufferResource> framebuffers_;
  std::unordered_map<uint32_t, MetalQueryResource> queries_;
  std::unordered_map<uint32_t, MetalFenceResource> fences_;
  std::unordered_map<PipelineCacheKey, PipelineHandle, PipelineCacheKeyHash>
      pipeline_cache_;
  std::unordered_map<size_t, MTLVertexDescriptor *> vertex_descriptor_library_;

  uint32_t next_buffer_id_ = 1;
  uint32_t next_texture_id_ = 1;
  uint32_t next_sampler_id_ = 1;
  uint32_t next_shader_id_ = 1;
  uint32_t next_pipeline_id_ = 1;
  uint32_t next_framebuffer_id_ = 1;
  uint32_t next_query_id_ = 1;
  uint32_t next_fence_id_ = 1;

  uint32_t frame_index_ = 0; // Current frame index for ring buffer

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

    // Initialize ring buffer allocator for frequently changing uniform data
    size_t ringBufferSize = sizeof(Uniforms) * kTotalUniformSlots;
    if (!uniform_allocator_.initialize(device_, ringBufferSize)) {
      std::cerr << "Failed to create Metal uniform allocator buffer"
                << std::endl;
    }

    immediate_ = std::make_unique<MetalCmdList>(this);
  }

  MTLVertexDescriptor *getOrCreateVertexDescriptor(bool instanced) {
    size_t key = instanced ? 1u : 0u;
    auto it = vertex_descriptor_library_.find(key);
    if (it != vertex_descriptor_library_.end()) {
      return it->second;
    }

    MTLVertexDescriptor *vertexDesc = [MTLVertexDescriptor vertexDescriptor];

    // Per-vertex attributes (buffer 0, locations 0-3)
    vertexDesc.attributes[0].format = MTLVertexFormatFloat3; // Position
    vertexDesc.attributes[0].offset = 0;
    vertexDesc.attributes[0].bufferIndex = 0;

    vertexDesc.attributes[1].format = MTLVertexFormatFloat3; // Normal
    vertexDesc.attributes[1].offset = 12;
    vertexDesc.attributes[1].bufferIndex = 0;

    vertexDesc.attributes[2].format = MTLVertexFormatFloat2; // TexCoord
    vertexDesc.attributes[2].offset = 24;
    vertexDesc.attributes[2].bufferIndex = 0;

    vertexDesc.attributes[3].format = MTLVertexFormatFloat4; // Color
    vertexDesc.attributes[3].offset = 32;
    vertexDesc.attributes[3].bufferIndex = 0;

    vertexDesc.layouts[0].stride = 48;
    vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    if (instanced) {
      // Per-instance attributes (buffer 2, locations 4-9)
      // Matches pixel::renderer3d::InstanceGPUData layout used by the GL backend
      vertexDesc.attributes[4].format = MTLVertexFormatFloat3; // position
      vertexDesc.attributes[4].offset = 0;
      vertexDesc.attributes[4].bufferIndex = 2;

      vertexDesc.attributes[5].format = MTLVertexFormatFloat3; // rotation
      vertexDesc.attributes[5].offset = 12;
      vertexDesc.attributes[5].bufferIndex = 2;

      vertexDesc.attributes[6].format = MTLVertexFormatFloat3; // scale
      vertexDesc.attributes[6].offset = 24;
      vertexDesc.attributes[6].bufferIndex = 2;

      vertexDesc.attributes[7].format = MTLVertexFormatFloat4; // color
      vertexDesc.attributes[7].offset = 36;
      vertexDesc.attributes[7].bufferIndex = 2;

      vertexDesc.attributes[8].format = MTLVertexFormatFloat; // texture index
      vertexDesc.attributes[8].offset = 52;
      vertexDesc.attributes[8].bufferIndex = 2;

      // Skip culling radius at offset 56 (CPU side only)

      vertexDesc.attributes[9].format = MTLVertexFormatFloat; // LOD alpha
      vertexDesc.attributes[9].offset = 60;
      vertexDesc.attributes[9].bufferIndex = 2;

      vertexDesc.attributes[10].format = MTLVertexFormatInvalid;

      vertexDesc.layouts[2].stride = sizeof(float) * 17; // 68 bytes
      vertexDesc.layouts[2].stepFunction = MTLVertexStepFunctionPerInstance;
      vertexDesc.layouts[2].stepRate = 1;
    }

    vertex_descriptor_library_[key] = vertexDesc;
    return vertexDesc;
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
    for (auto &pair : vertex_descriptor_library_) {
      pair.second = nil;
    }
    for (auto &pair : fences_) {
      if (pair.second.semaphore) {
#if defined(OS_OBJECT_USE_OBJC) && OS_OBJECT_USE_OBJC
        pair.second.semaphore = nullptr;
#else
        dispatch_release(pair.second.semaphore);
        pair.second.semaphore = nullptr;
#endif
      }
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
  caps.maxSamplerAnisotropy = std::max(
      1.0f, static_cast<float>([impl_->device_ maxSamplerAnisotropy]));
  caps.samplerCompare = true;
  caps.uniformBuffers = true;
  caps.clipSpaceYDown = true; // Metal uses Y-down clip space
  return caps;
}

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
    // Texture arrays currently store color data in RGBA8 format.
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

  if (desc.aniso || desc.maxAnisotropy > 1.0f) {
    float requested = desc.maxAnisotropy > 1.0f
                          ? desc.maxAnisotropy
                          : static_cast<float>([impl_->device_ maxSamplerAnisotropy]);
    float device_max = static_cast<float>([impl_->device_ maxSamplerAnisotropy]);
    requested = std::min(requested, device_max);
    samplerDesc.maxAnisotropy = std::max<NSUInteger>(1, static_cast<NSUInteger>(requested));
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
  } else if (stage == "cs_test") {
    functionName = @"test_compute";
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
      std::cerr << "Failed to create compute pipeline: " <<
          [[error localizedDescription] UTF8String] << std::endl;
      return PipelineHandle{0};
    }

    uint32_t handle_id = impl_->next_pipeline_id_++;
    impl_->pipelines_[handle_id] = pipeline;

    PipelineHandle handle{handle_id};
    impl_->pipeline_cache_[cacheKey] = handle;
    return handle;
  }

  // Render pipeline
  auto vs_it = impl_->shaders_.find(desc.vs.id);
  auto fs_it = impl_->shaders_.find(desc.fs.id);

  if (vs_it == impl_->shaders_.end() || fs_it == impl_->shaders_.end()) {
    std::cerr << "Vertex or fragment shader not found" << std::endl;
    return PipelineHandle{0};
  }

  // Check if this is an instanced pipeline
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

  // Create pipeline descriptor
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
    std::cerr << "Failed to create render pipeline: " <<
        [[error localizedDescription] UTF8String] << std::endl;
    return PipelineHandle{0};
  }

  // Use default depth stencil state
  pipeline.depth_stencil_state = impl_->default_depth_stencil_;

  uint32_t handle_id = impl_->next_pipeline_id_++;
  impl_->pipelines_[handle_id] = pipeline;

  PipelineHandle handle{handle_id};
  impl_->pipeline_cache_[cacheKey] = handle;
  return handle;
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
  dispatch_time_t timeout = timeout_ns == ~0ull
                                 ? DISPATCH_TIME_FOREVER
                                 : dispatch_time(DISPATCH_TIME_NOW,
                                                 static_cast<int64_t>(timeout_ns));
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

CmdList *MetalDevice::getImmediate() { return impl_->immediate_.get(); }

void MetalDevice::present() {
  // Present is handled by the command list
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

// Part 3: MetalCmdList Implementation - Core and Setup

// ============================================================================
// MetalCmdList::Impl - Private Implementation
// ============================================================================
struct MetalCmdList::Impl {
  id<MTLDevice> device_;
  id<MTLCommandQueue> command_queue_;
  CAMetalLayer *layer_;
  id<MTLTexture> depth_texture_;
  UniformAllocator *uniform_allocator_ = nullptr;
  UniformAllocator::Allocation current_uniform_{};

  std::unordered_map<uint32_t, MTLBufferResource> *buffers_;
  std::unordered_map<uint32_t, MTLTextureResource> *textures_;
  std::unordered_map<uint32_t, MTLPipelineResource> *pipelines_;
  std::unordered_map<uint32_t, MTLFramebufferResource> *framebuffers_;
  std::unordered_map<uint32_t, MetalQueryResource> *queries_;
  std::unordered_map<uint32_t, MetalFenceResource> *fences_;

  id<MTLCommandBuffer> command_buffer_ = nil;
  id<CAMetalDrawable> current_drawable_ = nil;
  id<MTLRenderCommandEncoder> render_encoder_ = nil;
  id<MTLComputeCommandEncoder> compute_encoder_ = nil;
  id<MTLSamplerState> default_sampler_ = nil;

  bool recording_ = false;
  enum class EncoderState { None, Render, Compute };
  EncoderState active_encoder_ = EncoderState::None;
  PipelineHandle current_pipeline_{0};
  PipelineHandle current_compute_pipeline_{0};
  BufferHandle current_vb_{0};
  BufferHandle current_ib_{0};
  size_t current_vb_offset_ = 0;
  size_t current_ib_offset_ = 0;

  // Ring buffer tracking
  uint32_t *frame_index_; // Pointer to device's frame index
  bool uniform_block_active_ = false;
  std::vector<id<MTLBuffer>> staging_uploads_;

  Impl(MetalDevice::Impl *device_impl)
      : device_(device_impl->device_),
        command_queue_(device_impl->command_queue_),
        layer_(device_impl->layer_),
        depth_texture_(device_impl->depth_texture_),
        uniform_allocator_(&device_impl->uniform_allocator_),
        buffers_(&device_impl->buffers_), textures_(&device_impl->textures_),
        pipelines_(&device_impl->pipelines_),
        framebuffers_(&device_impl->framebuffers_),
        queries_(&device_impl->queries_), fences_(&device_impl->fences_),
        frame_index_(&device_impl->frame_index_) {}

  ~Impl() {
    // ARC handles cleanup
  }

  bool ensureUniformBlock() {
    if (uniform_block_active_) {
      return current_uniform_.cpu_ptr != nullptr;
    }

    if (!uniform_allocator_) {
      return false;
    }

    auto allocation =
        uniform_allocator_->allocate(sizeof(Uniforms), alignof(Uniforms));
    if (!allocation) {
      std::cerr << "Metal uniform allocator exhausted for frame" << std::endl;
      return false;
    }

    current_uniform_ = *allocation;
    memset(current_uniform_.cpu_ptr, 0, sizeof(Uniforms));
    uniform_block_active_ = true;
    return true;
  }

  void resetUniformBlock() {
    uniform_block_active_ = false;
    current_uniform_ = {};
  }

  size_t getCurrentUniformOffset() const { return current_uniform_.offset; }

  Uniforms *getCurrentUniformSlot() {
    if (!ensureUniformBlock()) {
      return nullptr;
    }
    return reinterpret_cast<Uniforms *>(current_uniform_.cpu_ptr);
  }

  void bindCurrentUniformBlock(id<MTLRenderCommandEncoder> encoder) {
    if (!uniform_block_active_ || !current_uniform_.buffer || !encoder) {
      return;
    }
    [encoder setVertexBuffer:current_uniform_.buffer
                      offset:current_uniform_.offset
                     atIndex:1];
    [encoder setFragmentBuffer:current_uniform_.buffer
                        offset:current_uniform_.offset
                       atIndex:1];
  }

  void endRenderEncoderIfNeeded() {
    if (render_encoder_) {
      [render_encoder_ endEncoding];
      render_encoder_ = nil;
    }
    if (active_encoder_ == EncoderState::Render) {
      active_encoder_ = EncoderState::None;
    }
  }

  void endComputeEncoderIfNeeded() {
    if (compute_encoder_) {
      [compute_encoder_ endEncoding];
      compute_encoder_ = nil;
    }
    if (active_encoder_ == EncoderState::Compute) {
      active_encoder_ = EncoderState::None;
    }
  }

  void transitionToComputeEncoder() {
    if (active_encoder_ == EncoderState::Render) {
      endRenderEncoderIfNeeded();
    }
    if (!compute_encoder_) {
      compute_encoder_ = [command_buffer_ computeCommandEncoder];
    }
    active_encoder_ = EncoderState::Compute;
  }

  void resetEncoders() {
    endRenderEncoderIfNeeded();
    endComputeEncoderIfNeeded();
    active_encoder_ = EncoderState::None;
  }
};

// ============================================================================
// MetalCmdList Implementation
// ============================================================================
MetalCmdList::MetalCmdList(MetalDevice::Impl *device_impl)
    : impl_(std::make_unique<Impl>(device_impl)) {}

MetalCmdList::~MetalCmdList() = default;

void MetalCmdList::begin() {
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
}

void MetalCmdList::beginRender(const RenderPassDesc &desc) {
  std::cerr << "DEBUG: beginRender() called on frame " << *impl_->frame_index_
            << std::endl;
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
  bool hasDepthAttachment =
      framebuffer ? framebuffer->desc.hasDepthAttachment : desc.hasDepthAttachment;

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
    usesSwapchainDepth = hasDepthAttachment && desc.depthAttachment.texture.id == 0;
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

  if (requiresDrawable) {
    std::cerr << "DEBUG: Attempting to acquire drawable..." << std::endl;
    impl_->current_drawable_ = [impl_->layer_ nextDrawable];

    if (!impl_->current_drawable_) {
      std::cerr << "DEBUG: *** FAILED TO ACQUIRE DRAWABLE *** on frame "
                << *impl_->frame_index_ << std::endl;
      return;
    }
    std::cerr << "DEBUG: Successfully acquired drawable" << std::endl;
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

  NSUInteger targetWidth = framebuffer ? framebuffer->width : 0;
  NSUInteger targetHeight = framebuffer ? framebuffer->height : 0;

  for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
    const RenderPassColorAttachment *ops =
        (i < desc.colorAttachmentCount) ? &desc.colorAttachments[i] : nullptr;
    TextureHandle textureHandle = framebuffer
                                      ? framebuffer->desc.colorAttachments[i].texture
                                      : (ops ? ops->texture : TextureHandle{0});
    uint32_t mipLevel = framebuffer ? framebuffer->desc.colorAttachments[i].mipLevel
                                    : (ops ? ops->mipLevel : 0);
    uint32_t arraySlice = framebuffer
                               ? framebuffer->desc.colorAttachments[i].arraySlice
                               : (ops ? ops->arraySlice : 0);

    MTLRenderPassColorAttachmentDescriptor *colorDesc =
        renderPassDesc.colorAttachments[i];

    id<MTLTexture> texture = nil;
    if (textureHandle.id == 0) {
      if (!impl_->current_drawable_) {
        std::cerr
            << "Render pass requested swapchain attachment without drawable"
            << std::endl;
        return;
      }
      texture = impl_->current_drawable_.texture;
    } else {
      auto it = impl_->textures_->find(textureHandle.id);
      if (it == impl_->textures_->end() || !it->second.texture) {
        std::cerr << "DEBUG: EARLY RETURN - invalid color attachment texture "
                     "at index "
                  << i << std::endl;
        return;
      }
      texture = it->second.texture;
      if (mipLevel >= (uint32_t)texture.mipmapLevelCount) {
        std::cerr << "Metal color attachment mip level out of range"
                  << std::endl;
        return;
      }
      if (texture.textureType == MTLTextureType2D) {
        if (arraySlice != 0) {
          std::cerr
              << "Metal color attachment slice must be zero for 2D textures"
              << std::endl;
          return;
        }
      } else if (arraySlice >= (uint32_t)texture.arrayLength) {
        std::cerr << "Metal color attachment array slice out of range"
                  << std::endl;
        return;
      } else {
        colorDesc.slice = arraySlice;
      }
    }

    colorDesc.texture = texture;
    colorDesc.level = mipLevel;
    if (ops) {
      colorDesc.loadAction = toMTLLoadAction(ops->loadOp);
      colorDesc.storeAction = toMTLStoreAction(ops->storeOp);
      if (ops->loadOp == LoadOp::Clear) {
        colorDesc.clearColor =
            MTLClearColorMake(ops->clearColor[0], ops->clearColor[1],
                              ops->clearColor[2], ops->clearColor[3]);
      }
    } else {
      colorDesc.loadAction = MTLLoadActionLoad;
      colorDesc.storeAction = MTLStoreActionStore;
    }

    if (texture) {
      if (targetWidth == 0) {
        targetWidth = texture.width;
        targetHeight = texture.height;
      } else if (targetWidth != texture.width ||
                 targetHeight != texture.height) {
        std::cerr << "Metal color attachments must have matching dimensions"
                  << std::endl;
        return;
      }
    }
  }

  for (uint32_t i = colorAttachmentCount; i < kMaxColorAttachments; ++i) {
    MTLRenderPassColorAttachmentDescriptor *colorDesc =
        renderPassDesc.colorAttachments[i];
    colorDesc.texture = nil;
    colorDesc.loadAction = MTLLoadActionDontCare;
    colorDesc.storeAction = MTLStoreActionDontCare;
  }

  if (hasDepthAttachment) {
    RenderPassDepthAttachment depthOps = desc.depthAttachment;
    if (framebuffer && !desc.hasDepthAttachment) {
      depthOps.hasStencil = framebuffer->desc.depthAttachment.hasStencil;
    }
    TextureHandle depthHandle = framebuffer
                                    ? framebuffer->desc.depthAttachment.texture
                                    : depthOps.texture;
    uint32_t depthMipLevel = framebuffer
                                 ? framebuffer->desc.depthAttachment.mipLevel
                                 : depthOps.mipLevel;
    uint32_t depthSlice = framebuffer
                               ? framebuffer->desc.depthAttachment.arraySlice
                               : depthOps.arraySlice;
    bool depthHasStencil = framebuffer
                               ? framebuffer->desc.depthAttachment.hasStencil
                               : depthOps.hasStencil;

    id<MTLTexture> depthTexture = nil;
    if (depthHandle.id == 0) {
      depthTexture = impl_->depth_texture_;
    } else {
      auto it = impl_->textures_->find(depthHandle.id);
      if (it == impl_->textures_->end() || !it->second.texture) {
        std::cerr << "Invalid Metal depth attachment texture handle"
                  << std::endl;
        return;
      }
      depthTexture = it->second.texture;
      if (depthMipLevel >= (uint32_t)depthTexture.mipmapLevelCount) {
        std::cerr << "Metal depth attachment mip level out of range"
                  << std::endl;
        return;
      }
      if (depthTexture.textureType != MTLTextureType2D && depthSlice != 0) {
        std::cerr
            << "Metal depth attachment array slices are not currently supported"
            << std::endl;
        return;
      }
    }

    if (depthTexture) {
      renderPassDesc.depthAttachment.texture = depthTexture;
      renderPassDesc.depthAttachment.level = depthMipLevel;
      renderPassDesc.depthAttachment.loadAction =
          toMTLLoadAction(depthOps.depthLoadOp);
      renderPassDesc.depthAttachment.storeAction =
          toMTLStoreAction(depthOps.depthStoreOp);
      renderPassDesc.depthAttachment.clearDepth = depthOps.clearDepth;

      if (depthHasStencil) {
        renderPassDesc.stencilAttachment.texture = depthTexture;
        renderPassDesc.stencilAttachment.loadAction =
            toMTLLoadAction(depthOps.stencilLoadOp);
        renderPassDesc.stencilAttachment.storeAction =
            toMTLStoreAction(depthOps.stencilStoreOp);
        renderPassDesc.stencilAttachment.clearStencil = depthOps.clearStencil;
      } else {
        renderPassDesc.stencilAttachment.texture = nil;
      }

      if (targetWidth == 0) {
        targetWidth = depthTexture.width;
        targetHeight = depthTexture.height;
      } else if (targetWidth != depthTexture.width ||
                 targetHeight != depthTexture.height) {
        std::cerr
            << "Metal depth attachment dimensions must match color attachments"
            << std::endl;
        return;
      }
    } else {
      std::cerr << "Metal render pass missing depth texture" << std::endl;
      return;
    }
  } else {
    renderPassDesc.depthAttachment.texture = nil;
    renderPassDesc.stencilAttachment.texture = nil;
  }

  if (targetWidth != 0 && targetHeight != 0) {
    renderPassDesc.renderTargetWidth = targetWidth;
    renderPassDesc.renderTargetHeight = targetHeight;
    renderPassDesc.renderTargetArrayLength = 1;
  }
  impl_->endRenderEncoderIfNeeded();
  std::cerr
      << "DEBUG: All validations passed, creating render encoder on frame "
      << *impl_->frame_index_ << std::endl;
  impl_->render_encoder_ = [impl_->command_buffer_
      renderCommandEncoderWithDescriptor:renderPassDesc];
  if (!impl_->render_encoder_) {
    std::cerr << "DEBUG: *** ENCODER CREATION FAILED *** on frame "
              << *impl_->frame_index_ << std::endl;

    return;
  }
  impl_->active_encoder_ = Impl::EncoderState::Render;
  std::cerr << "DEBUG: Render encoder successfully created" << std::endl;
}

void MetalCmdList::setPipeline(PipelineHandle handle) {
  auto it = impl_->pipelines_->find(handle.id);
  if (it == impl_->pipelines_->end())
    return;

  impl_->current_pipeline_ = handle;
  const MTLPipelineResource &pipeline = it->second;

  if (!impl_->render_encoder_) {
    std::cerr
        << "Attempted to set render pipeline without an active render pass"
        << std::endl;
    return;
  }

  if (impl_->active_encoder_ != Impl::EncoderState::Render) {
    std::cerr << "Attempted to set render pipeline when encoder is not in "
                 "render state"
              << std::endl;
    return;
  }

  [impl_->render_encoder_ setRenderPipelineState:pipeline.pipeline_state];
  [impl_->render_encoder_ setDepthStencilState:pipeline.depth_stencil_state];
  [impl_->render_encoder_ setCullMode:MTLCullModeBack];
  [impl_->render_encoder_ setFrontFacingWinding:MTLWindingCounterClockwise];
  impl_->active_encoder_ = Impl::EncoderState::Render;
}

void MetalCmdList::setVertexBuffer(BufferHandle handle, size_t offset) {
  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end())
    return;

  if (!impl_->render_encoder_) {
    std::cerr << "Attempted to set vertex buffer without an active render pass"
              << std::endl;
    return;
  }

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

  if (!impl_->render_encoder_) {
    std::cerr << "Attempted to set index buffer without an active render pass"
              << std::endl;
    return;
  }

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
  Uniforms *uniforms = impl_->getCurrentUniformSlot();
  if (!uniforms) {
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
  }

  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
}

void MetalCmdList::setUniformVec3(const char *name, const float *vec3) {
  Uniforms *uniforms = impl_->getCurrentUniformSlot();
  if (!uniforms) {
    return;
  }
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

  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
}

void MetalCmdList::setUniformVec4(const char *name, const float *vec4) {
  (void)name;
  (void)vec4;
  if (!impl_->getCurrentUniformSlot()) {
    return;
  }
  // Currently not used in shaders, but ensure buffer is bound
  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
}

void MetalCmdList::setUniformInt(const char *name, int value) {
  Uniforms *uniforms = impl_->getCurrentUniformSlot();
  if (!uniforms) {
    return;
  }
  std::string name_str(name);

  if (name_str == "useTexture") {
    uniforms->useTexture = value;
  } else if (name_str == "useTextureArray") {
    uniforms->useTextureArray = value;
  } else if (name_str == "ditherEnabled" || name_str == "uDitherEnabled") {
    uniforms->ditherEnabled = value;
  }

  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
}

void MetalCmdList::setUniformFloat(const char *name, float value) {
  Uniforms *uniforms = impl_->getCurrentUniformSlot();
  if (!uniforms) {
    return;
  }
  std::string name_str(name);

  if (name_str == "time" || name_str == "uTime") {
    uniforms->time = value;
  } else if (name_str == "uDitherScale") {
    uniforms->ditherScale = value;
  } else if (name_str == "uCrossfadeDuration") {
    uniforms->crossfadeDuration = value;
  }

  impl_->bindCurrentUniformBlock(impl_->render_encoder_);
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
  if (impl_->current_pipeline_.id == 0 || impl_->current_ib_.id == 0)
    return;

  if (!impl_->render_encoder_) {
    std::cerr << "Attempted to draw without an active render pass" << std::endl;
    return;
  }

  if (impl_->active_encoder_ != Impl::EncoderState::Render) {
    std::cerr << "Attempted to draw when encoder is not in render state"
              << std::endl;
    return;
  }

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

  impl_->resetUniformBlock();
}

void MetalCmdList::setComputePipeline(PipelineHandle handle) {
  auto it = impl_->pipelines_->find(handle.id);
  if (it == impl_->pipelines_->end())
    return;

  const MTLPipelineResource &pipeline = it->second;

  if (!pipeline.compute_pipeline_state) {
    std::cerr << "Pipeline handle does not reference a compute pipeline"
              << std::endl;
    return;
  }

  if (!impl_->command_buffer_) {
    std::cerr << "Compute pipeline set without an active command buffer"
              << std::endl;
    return;
  }

  impl_->transitionToComputeEncoder();
  [impl_->compute_encoder_
      setComputePipelineState:pipeline.compute_pipeline_state];
  impl_->current_compute_pipeline_ = handle;
  impl_->current_pipeline_ = PipelineHandle{0};
}

void MetalCmdList::setStorageBuffer(uint32_t binding, BufferHandle buffer,
                                    size_t offset, size_t size) {
  auto it = impl_->buffers_->find(buffer.id);
  if (it == impl_->buffers_->end())
    return;

  const MTLBufferResource &buf = it->second;

  if (!impl_->compute_encoder_) {
    std::cerr
        << "Attempted to bind storage buffer without an active compute encoder"
        << std::endl;
    return;
  }

  [impl_->compute_encoder_ setBuffer:buf.buffer offset:offset atIndex:binding];
}

void MetalCmdList::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                            uint32_t groupCountZ) {
  if (!impl_->compute_encoder_)
    return;

  auto it = impl_->pipelines_->find(impl_->current_compute_pipeline_.id);
  if (it == impl_->pipelines_->end())
    return;

  id<MTLComputePipelineState> state = it->second.compute_pipeline_state;
  if (!state)
    return;

  if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
    return;

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
  impl_->resetEncoders();
  impl_->current_pipeline_ = PipelineHandle{0};
  impl_->current_compute_pipeline_ = PipelineHandle{0};
  impl_->resetUniformBlock();
}

void MetalCmdList::copyToBuffer(BufferHandle handle, size_t dstOff,
                                std::span<const std::byte> src) {
  if (src.empty()) {
    return;
  }

  auto it = impl_->buffers_->find(handle.id);
  if (it == impl_->buffers_->end())
    return;

  MTLBufferResource &buffer = it->second;

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
}

void MetalCmdList::end() {
  impl_->resetEncoders();

  if (impl_->command_buffer_) {
    if (impl_->current_drawable_) {
      [impl_->command_buffer_ presentDrawable:impl_->current_drawable_];
    }

    [impl_->command_buffer_ commit];
    /* [impl_->command_buffer_ waitUntilCompleted]; */
  }

  impl_->staging_uploads_.clear();
  impl_->resetUniformBlock();
  impl_->command_buffer_ = nil;
  impl_->current_drawable_ = nil;
  impl_->recording_ = false;
  impl_->active_encoder_ = Impl::EncoderState::None;

  // Advance to next frame in the ring buffer
  (*impl_->frame_index_) = ((*impl_->frame_index_) + 1) % kFramesInFlight;
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
