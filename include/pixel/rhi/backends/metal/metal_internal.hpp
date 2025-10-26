// include/pixel/rhi/backends/metal/metal_internal.hpp
#pragma once

#ifdef __APPLE__

#include "device_metal.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>
#import <dispatch/dispatch.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace pixel::rhi {

MTLCompareFunction to_mtl_compare(CompareOp op);
MTLStencilOperation to_mtl_stencil(StencilOp op);
MTLPixelFormat toMTLFormat(Format format);
size_t getBytesPerPixel(Format format);
MTLBlendFactor toMTLBlendFactor(BlendFactor factor);
MTLBlendOperation toMTLBlendOp(BlendOp op);
MTLLoadAction toMTLLoadAction(LoadOp op);
MTLStoreAction toMTLStoreAction(StoreOp op);

constexpr uint32_t kFramesInFlight = 3;          // Triple buffering
constexpr uint32_t kMaxDrawCallsPerFrame = 1024; // Maximum draws per frame
constexpr uint32_t kTotalUniformSlots = kFramesInFlight * kMaxDrawCallsPerFrame;

struct Uniforms {
  float model[16];
  float view[16];
  float projection[16];
  float normalMatrix[16];
  float lightViewProj[16];
  float materialColor[4];
  float lightPos[3];
  float alphaCutoff;
  float viewPos[3];
  float baseAlpha;
  float lightColor[3];
  float shadowBias;
  float uTime;
  float ditherScale;
  float crossfadeDuration;
  float _padMisc;
  int useTexture;
  int useTextureArray;
  int uDitherEnabled;
  int shadowsEnabled;
};

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
  id<MTLLibrary> library = nil;
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

struct DepthStencilStateHash {
  std::size_t operator()(const DepthStencilState &state) const noexcept {
    std::size_t seed = 0;
    auto hash_combine = [&seed](std::size_t value) {
      seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };

    using CompareUnderlying = std::underlying_type_t<CompareOp>;
    using StencilUnderlying = std::underlying_type_t<StencilOp>;

    hash_combine(std::hash<bool>{}(state.depthTestEnable));
    hash_combine(std::hash<bool>{}(state.depthWriteEnable));
    hash_combine(std::hash<CompareUnderlying>{}(
        static_cast<CompareUnderlying>(state.depthCompare)));
    hash_combine(std::hash<bool>{}(state.stencilEnable));
    hash_combine(std::hash<CompareUnderlying>{}(
        static_cast<CompareUnderlying>(state.stencilCompare)));
    hash_combine(std::hash<StencilUnderlying>{}(
        static_cast<StencilUnderlying>(state.stencilFailOp)));
    hash_combine(std::hash<StencilUnderlying>{}(
        static_cast<StencilUnderlying>(state.stencilDepthFailOp)));
    hash_combine(std::hash<StencilUnderlying>{}(
        static_cast<StencilUnderlying>(state.stencilPassOp)));
    hash_combine(std::hash<uint32_t>{}(state.stencilReadMask));
    hash_combine(std::hash<uint32_t>{}(state.stencilWriteMask));
    hash_combine(std::hash<uint32_t>{}(state.stencilReference));

    return seed;
  }
};

struct MetalDevice::Impl {
  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> command_queue_ = nil;
  CAMetalLayer *layer_ = nil;
  id<MTLTexture> depth_texture_ = nil;
  GLFWwindow *glfw_window_ = nullptr;
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
  std::unordered_map<size_t, id<MTLLibrary>> shader_library_cache_;

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

  Impl(id<MTLDevice> device, CAMetalLayer *layer, id<MTLTexture> depth_texture,
       GLFWwindow *window)
      : device_(device), layer_(layer), depth_texture_(depth_texture),
        glfw_window_(window) {
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
      // Matches pixel::renderer3d::InstanceGPUData layout used by the GL
      // backend
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

  void updateSwapchainSize(NSUInteger width, NSUInteger height) {
    if (!layer_ || width == 0 || height == 0) {
      return;
    }

    CGSize current = layer_.drawableSize;
    if (static_cast<NSUInteger>(current.width) != width ||
        static_cast<NSUInteger>(current.height) != height) {
      layer_.drawableSize = CGSizeMake(width, height);
      std::cerr << "DEBUG: CAMetalLayer drawable resized to " << width << "x"
                << height << std::endl;
    }

    if (!depth_texture_ || depth_texture_.width != width ||
        depth_texture_.height != height) {
      MTLTextureDescriptor *depthDesc = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
      depthDesc.usage = MTLTextureUsageRenderTarget;
      depthDesc.storageMode = MTLStorageModePrivate;

      id<MTLTexture> newDepth = [device_ newTextureWithDescriptor:depthDesc];
      if (!newDepth) {
        std::cerr << "Failed to resize Metal depth texture to " << width << "x"
                  << height << std::endl;
      } else {
        depth_texture_ = newDepth;
        std::cerr << "DEBUG: Recreated depth texture for drawable size "
                  << width << "x" << height << std::endl;
      }
    }
  }
};

struct MetalCmdList::Impl {
  MetalDevice::Impl *device_impl_ = nullptr;
  id<MTLDevice> device_;
  id<MTLCommandQueue> command_queue_;
  CAMetalLayer *layer_;
  id<MTLTexture> depth_texture_;
  GLFWwindow *glfw_window_;
  UniformAllocator *uniform_allocator_ = nullptr;
  UniformAllocator::Allocation current_uniform_{};

  std::unordered_map<uint32_t, MTLBufferResource> *buffers_;
  std::unordered_map<uint32_t, MTLTextureResource> *textures_;
  std::unordered_map<uint32_t, MTLPipelineResource> *pipelines_;
  std::unordered_map<uint32_t, MTLSamplerResource> *samplers_;
  std::unordered_map<uint32_t, MTLFramebufferResource> *framebuffers_;
  std::unordered_map<uint32_t, MetalQueryResource> *queries_;
  std::unordered_map<uint32_t, MetalFenceResource> *fences_;

  std::unordered_map<DepthStencilState, id<MTLDepthStencilState>,
                     DepthStencilStateHash>
      depth_stencil_cache_;
  DepthStencilState current_depth_stencil_state_{};
  DepthBiasState current_depth_bias_state_{};
  bool depth_stencil_state_initialized_ = false;
  bool depth_bias_initialized_ = false;

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
      : device_impl_(device_impl),
        device_(device_impl->device_),
        command_queue_(device_impl->command_queue_),
        layer_(device_impl->layer_),
        depth_texture_(device_impl->depth_texture_),
        glfw_window_(device_impl->glfw_window_),
        uniform_allocator_(&device_impl->uniform_allocator_),
        buffers_(&device_impl->buffers_), textures_(&device_impl->textures_),
        pipelines_(&device_impl->pipelines_),
        samplers_(&device_impl->samplers_),
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

} // namespace pixel::rhi

#endif // __APPLE__
