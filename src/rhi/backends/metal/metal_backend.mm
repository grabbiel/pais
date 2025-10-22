// src/rhi/backends/metal/metal_backend.mm
#ifdef __APPLE__

#include "metal_backend.hpp"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <Metal/MTLCommandBuffer.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <utility>

namespace pixel::renderer3d::metal {

namespace {

std::string to_std_string(NSString *string) {
  if (!string) {
    return std::string();
  }
  const char *c_str = [string UTF8String];
  return c_str ? std::string(c_str) : std::string();
}

std::string describe_nsobject(NSObject *object) {
  if (!object) {
    return std::string();
  }
  NSString *description = [object description];
  return to_std_string(description);
}

MTLCompareFunction to_mtl_compare(rhi::CompareOp op) {
  switch (op) {
  case rhi::CompareOp::Never:
    return MTLCompareFunctionNever;
  case rhi::CompareOp::Less:
    return MTLCompareFunctionLess;
  case rhi::CompareOp::Equal:
    return MTLCompareFunctionEqual;
  case rhi::CompareOp::LessEqual:
    return MTLCompareFunctionLessEqual;
  case rhi::CompareOp::Greater:
    return MTLCompareFunctionGreater;
  case rhi::CompareOp::NotEqual:
    return MTLCompareFunctionNotEqual;
  case rhi::CompareOp::GreaterEqual:
    return MTLCompareFunctionGreaterEqual;
  case rhi::CompareOp::Always:
  default:
    return MTLCompareFunctionAlways;
  }
}

MTLStencilOperation to_mtl_stencil(rhi::StencilOp op) {
  switch (op) {
  case rhi::StencilOp::Zero:
    return MTLStencilOperationZero;
  case rhi::StencilOp::Replace:
    return MTLStencilOperationReplace;
  case rhi::StencilOp::IncrementClamp:
    return MTLStencilOperationIncrementClamp;
  case rhi::StencilOp::DecrementClamp:
    return MTLStencilOperationDecrementClamp;
  case rhi::StencilOp::Invert:
    return MTLStencilOperationInvert;
  case rhi::StencilOp::IncrementWrap:
    return MTLStencilOperationIncrementWrap;
  case rhi::StencilOp::DecrementWrap:
    return MTLStencilOperationDecrementWrap;
  case rhi::StencilOp::Keep:
  default:
    return MTLStencilOperationKeep;
  }
}

} // namespace

// ============================================================================
// Uniforms structure (simplified)
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
};

// ============================================================================
// Metal Shader Implementation (Stub)
// ============================================================================

std::unique_ptr<MetalShader>
MetalShader::create(id<MTLDevice> device, const std::string &vertex_src,
                    const std::string &fragment_src) {
  (void)vertex_src;
  (void)fragment_src;
  auto shader = std::unique_ptr<MetalShader>(new MetalShader());
  shader->device_ = device;

  // Use default library with pre-compiled shaders
  shader->library_ = [device newDefaultLibrary];

  if (!shader->library_) {
    std::cerr << "Failed to create shader library" << std::endl;
    return nullptr;
  }

  shader->library_.label = @"DefaultLibrary";

  // Get vertex and fragment functions
  shader->vertex_function_ =
      [shader->library_ newFunctionWithName:@"vertex_main"];
  shader->fragment_function_ =
      [shader->library_ newFunctionWithName:@"fragment_main"];

  if (!shader->vertex_function_ || !shader->fragment_function_) {
    std::cerr << "Failed to load shader functions" << std::endl;
    return nullptr;
  }

  shader->vertex_function_.label = @"vertex_main";
  shader->fragment_function_.label = @"fragment_main";

  // Create pipeline state
  MTLRenderPipelineDescriptor *pipelineDesc =
      [[MTLRenderPipelineDescriptor alloc] init];
  pipelineDesc.label = @"Pipeline_Default_Opaque";
  pipelineDesc.vertexFunction = shader->vertex_function_;
  pipelineDesc.fragmentFunction = shader->fragment_function_;
  pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  pipelineDesc.depthAttachmentPixelFormat =
      MTLPixelFormatDepth32Float_Stencil8;
  pipelineDesc.stencilAttachmentPixelFormat =
      MTLPixelFormatDepth32Float_Stencil8;

  // Configure blending
  pipelineDesc.colorAttachments[0].blendingEnabled = YES;
  pipelineDesc.colorAttachments[0].sourceRGBBlendFactor =
      MTLBlendFactorSourceAlpha;
  pipelineDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;

  NSError *error = nil;
  shader->pipeline_state_ =
      [device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];

  if (!shader->pipeline_state_) {
    std::cerr << "Failed to create pipeline state: "
              << to_std_string([error localizedDescription]) << std::endl;
    if (error) {
      std::string details = describe_nsobject([error userInfo]);
      if (!details.empty()) {
        std::cerr << "  Details: " << details << std::endl;
      }
    }
    return nullptr;
  }

  if ([shader->pipeline_state_ respondsToSelector:@selector(setLabel:)]) {
    [shader->pipeline_state_ setLabel:@"Pipeline_Default_Opaque"];
  }

  // Create uniform buffer
  shader->uniform_buffers_["uniforms"].buffer =
      [device newBufferWithLength:sizeof(Uniforms)
                          options:(MTLResourceStorageModeShared |
                                   MTLResourceCPUCacheModeWriteCombined)];
  shader->uniform_buffers_["uniforms"].size = sizeof(Uniforms);

  if (!shader->uniform_buffers_["uniforms"].buffer) {
    std::cerr << "Failed to allocate Metal uniform buffer" << std::endl;
    return nullptr;
  }

  shader->uniform_buffers_["uniforms"].buffer.label = @"UniformBuffer_Frame_0";

  return shader;
}

MetalShader::~MetalShader() {
  // ARC handles cleanup
}

void MetalShader::bind(id<MTLRenderCommandEncoder> encoder,
                       id<MTLDepthStencilState> depth_stencil_state) {
  [encoder setRenderPipelineState:pipeline_state_];
  if (depth_stencil_state) {
    [encoder setDepthStencilState:depth_stencil_state];
  }

  // Bind uniform buffers
  for (const auto &[name, buffer] : uniform_buffers_) {
    if (buffer.buffer) {
      [encoder setVertexBuffer:buffer.buffer offset:0 atIndex:1];
      [encoder setFragmentBuffer:buffer.buffer offset:0 atIndex:1];
    }
  }
}

void MetalShader::set_mat4(const std::string &name, const float *value) {
  auto it = uniform_buffers_.find("uniforms");
  if (it != uniform_buffers_.end()) {
    Uniforms *uniforms = (Uniforms *)it->second.buffer.contents;
    if (name == "model") {
      memcpy(uniforms->model, value, sizeof(float) * 16);
    } else if (name == "view") {
      memcpy(uniforms->view, value, sizeof(float) * 16);
    } else if (name == "projection") {
      memcpy(uniforms->projection, value, sizeof(float) * 16);
    }
  }
}

void MetalShader::set_vec3(const std::string &name, const Vec3 &value) {
  auto it = uniform_buffers_.find("uniforms");
  if (it != uniform_buffers_.end()) {
    Uniforms *uniforms = (Uniforms *)it->second.buffer.contents;
    if (name == "lightPos") {
      uniforms->lightPos[0] = value.x;
      uniforms->lightPos[1] = value.y;
      uniforms->lightPos[2] = value.z;
    } else if (name == "viewPos") {
      uniforms->viewPos[0] = value.x;
      uniforms->viewPos[1] = value.y;
      uniforms->viewPos[2] = value.z;
    }
  }
}

void MetalShader::set_int(const std::string &name, int value) {
  auto it = uniform_buffers_.find("uniforms");
  if (it != uniform_buffers_.end()) {
    Uniforms *uniforms = (Uniforms *)it->second.buffer.contents;
    if (name == "useTexture") {
      uniforms->useTexture = value;
    } else if (name == "useTextureArray") {
      uniforms->useTextureArray = value;
    } else if (name == "ditherEnabled") {
      uniforms->ditherEnabled = value;
    }
  }
}

void MetalShader::set_float(const std::string &name, float value) {
  auto it = uniform_buffers_.find("uniforms");
  if (it != uniform_buffers_.end()) {
    Uniforms *uniforms = (Uniforms *)it->second.buffer.contents;
    if (name == "time") {
      uniforms->time = value;
    }
  }
}

// ============================================================================
// Metal Mesh Implementation (Stub)
// ============================================================================

namespace {
static id<MTLBuffer> create_gpu_buffer(id<MTLDevice> device,
                                       id<MTLCommandQueue> command_queue,
                                       const void *data, size_t length,
                                       NSString *label) {
  if (length == 0) {
    return nil;
  }

  if (!command_queue) {
    std::cerr << "Metal command queue is unavailable for buffer upload"
              << std::endl;
    return nil;
  }

  id<MTLBuffer> gpu_buffer = [device newBufferWithLength:length
                                                  options:MTLResourceStorageModePrivate];

  if (!gpu_buffer) {
    std::cerr << "Failed to allocate Metal GPU buffer" << std::endl;
    return nil;
  }

  if (label) {
    gpu_buffer.label = label;
  }

  if (!data) {
    return gpu_buffer;
  }

  id<MTLBuffer> staging_buffer =
      [device newBufferWithLength:length
                           options:(MTLResourceStorageModeShared |
                                    MTLResourceCPUCacheModeWriteCombined)];

  if (!staging_buffer) {
    std::cerr << "Failed to allocate Metal staging buffer" << std::endl;
    return nil;
  }

  memcpy(staging_buffer.contents, data, length);
  if ([staging_buffer respondsToSelector:@selector(didModifyRange:)]) {
    [staging_buffer didModifyRange:NSMakeRange(0, length)];
  }

  id<MTLCommandBuffer> command_buffer = [command_queue commandBuffer];
  if (!command_buffer) {
    std::cerr << "Failed to create Metal command buffer for buffer upload"
              << std::endl;
    return nil;
  }
  id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
  if (!blit) {
    std::cerr << "Failed to create Metal blit encoder for buffer upload"
              << std::endl;
    return nil;
  }
  [blit copyFromBuffer:staging_buffer
          sourceOffset:0
              toBuffer:gpu_buffer
     destinationOffset:0
                  size:length];
  [blit endEncoding];
  [command_buffer commit];
  [command_buffer waitUntilCompleted];

  return gpu_buffer;
}
} // namespace

std::unique_ptr<MetalMesh>
MetalMesh::create(id<MTLDevice> device, id<MTLCommandQueue> command_queue,
                  const std::vector<Vertex> &vertices,
                  const std::vector<uint32_t> &indices) {
  auto mesh = std::unique_ptr<MetalMesh>(new MetalMesh());
  mesh->device_ = device;
  mesh->vertex_count_ = vertices.size();
  mesh->index_count_ = indices.size();

  size_t vertex_buffer_size = vertices.size() * sizeof(Vertex);
  mesh->vertex_buffer_ = create_gpu_buffer(device, command_queue, vertices.data(),
                                           vertex_buffer_size,
                                           @"VertexBuffer_Sphere");

  if (!mesh->vertex_buffer_) {
    std::cerr << "Failed to create Metal vertex buffer for mesh" << std::endl;
    return nullptr;
  }

  size_t index_buffer_size = indices.size() * sizeof(uint32_t);
  mesh->index_buffer_ = create_gpu_buffer(device, command_queue, indices.data(),
                                          index_buffer_size, @"IndexBuffer_Sphere");

  if (!mesh->index_buffer_ && !indices.empty()) {
    std::cerr << "Failed to create Metal index buffer for mesh" << std::endl;
    return nullptr;
  }

  return mesh;
}

MetalMesh::~MetalMesh() {
  // ARC handles cleanup
}

void MetalMesh::draw(id<MTLRenderCommandEncoder> encoder) const {
  [encoder setVertexBuffer:vertex_buffer_ offset:0 atIndex:0];
  [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                      indexCount:index_count_
                       indexType:MTLIndexTypeUInt32
                     indexBuffer:index_buffer_
               indexBufferOffset:0];
}

// ============================================================================
// Metal Texture Implementation (Stub)
// ============================================================================

std::unique_ptr<MetalTexture> MetalTexture::create(id<MTLDevice> device,
                                                   int width, int height,
                                                   const uint8_t *data) {
  auto texture = std::unique_ptr<MetalTexture>(new MetalTexture());
  texture->device_ = device;
  texture->width_ = width;
  texture->height_ = height;

  // Create texture descriptor
  MTLTextureDescriptor *desc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:width
                                  height:height
                               mipmapped:YES];

  texture->texture_ = [device newTextureWithDescriptor:desc];

  if (!texture->texture_) {
    std::cerr << "Failed to create Metal texture resource" << std::endl;
    return nullptr;
  }

  texture->texture_.label = @"Texture_Brick";

  // Upload texture data
  if (data) {
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture->texture_ replaceRegion:region
                         mipmapLevel:0
                           withBytes:data
                         bytesPerRow:width * 4];
  }

  // Create sampler
  MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
  samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
  samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
  samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
  samplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
  samplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;

  texture->sampler_ = [device newSamplerStateWithDescriptor:samplerDesc];

  if (!texture->sampler_) {
    std::cerr << "Failed to create Metal sampler state" << std::endl;
    return nullptr;
  }

  if ([texture->sampler_ respondsToSelector:@selector(setLabel:)]) {
    [texture->sampler_ setLabel:@"Sampler_Default"];
  }

  return texture;
}

MetalTexture::~MetalTexture() {
  // ARC handles cleanup
}

void MetalTexture::bind(id<MTLRenderCommandEncoder> encoder, int slot) {
  [encoder setFragmentTexture:texture_ atIndex:slot];
  [encoder setFragmentSamplerState:sampler_ atIndex:slot];
}

// ============================================================================
// Metal Backend Implementation (Stub)
// ============================================================================

std::unique_ptr<MetalBackend> MetalBackend::create(GLFWwindow *window) {
  auto backend = std::unique_ptr<MetalBackend>(new MetalBackend());

  if (!backend->initialize(window)) {
    return nullptr;
  }

  return backend;
}

bool MetalBackend::initialize(GLFWwindow *window) {
#if !defined(NDEBUG)
  if (setenv("MTL_DEBUG_LAYER", "1", 1) == 0 &&
      setenv("MTL_API_VALIDATION", "1", 1) == 0) {
    validation_enabled_ = true;
    std::cout << "Metal API validation enabled" << std::endl;
  } else {
    std::cerr << "Failed to enable Metal API validation via environment" << std::endl;
  }
#endif

  inflight_command_buffers_ = [[NSMutableArray alloc] init];

  for (auto &frame : frame_contexts_) {
    frame.fence = dispatch_semaphore_create(1);
    if (!frame.fence) {
      std::cerr << "Failed to create Metal frame fence" << std::endl;
      return false;
    }
    frame.frame_id = 0;
    frame.active = false;
    frame.completion_callbacks.clear();
  }

  // Get the native window
  NSWindow *nsWindow = glfwGetCocoaWindow(window);
  if (!nsWindow) {
    std::cerr << "Failed to get Cocoa window" << std::endl;
    return false;
  }

  if (!nsWindow.contentView) {
    std::cerr << "Cocoa window does not have a content view" << std::endl;
    return false;
  }

  // Create Metal device
  device_ = MTLCreateSystemDefaultDevice();
  if (!device_) {
    std::cerr << "Metal is not supported on this device" << std::endl;
    return false;
  }

  std::cout << "Metal device: " << [[device_ name] UTF8String] << std::endl;

  // Create command queue
  command_queue_ = [device_ newCommandQueue];
  if (!command_queue_) {
    std::cerr << "Failed to create Metal command queue" << std::endl;
    return false;
  }

  command_queue_.label = @"CommandQueue_Main";

  inflight_semaphore_ = dispatch_semaphore_create(3);
  if (!inflight_semaphore_) {
    std::cerr << "Failed to create Metal inflight semaphore" << std::endl;
    return false;
  }

  // Create Metal layer
  metal_layer_ = [CAMetalLayer layer];
  if (!metal_layer_) {
    std::cerr << "Failed to create CAMetalLayer" << std::endl;
    return false;
  }
  metal_layer_.device = device_;
  metal_layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metal_layer_.framebufferOnly = NO;

  // Set layer on the window
  nsWindow.contentView.layer = metal_layer_;
  nsWindow.contentView.wantsLayer = YES;

  // Get initial size
  CGSize viewSize = nsWindow.contentView.bounds.size;
  viewport_width_ = viewSize.width;
  viewport_height_ = viewSize.height;
  metal_layer_.drawableSize = viewSize;

  // Create depth-stencil texture
  MTLTextureDescriptor *depthDesc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                   width:viewport_width_
                                  height:viewport_height_
                               mipmapped:NO];
  depthDesc.usage = MTLTextureUsageRenderTarget;
  depthDesc.storageMode = MTLStorageModePrivate;

  depth_texture_ = [device_ newTextureWithDescriptor:depthDesc];
  if (!depth_texture_) {
    std::cerr << "Failed to create Metal depth texture" << std::endl;
    return false;
  }

  depth_texture_.label = @"DepthTexture_Main";

  frame_counter_ = 0;
  current_frame_id_ = 0;
  last_completed_frame_id_ = 0;
  current_frame_context_ = nullptr;

  profiling_enabled_ = false;
  latest_stats_ = {};
  setup_profiling();

  return true;
}

MetalBackend::~MetalBackend() {
  if (inflight_command_buffers_) {
    for (id<MTLCommandBuffer> buffer in inflight_command_buffers_) {
      [buffer waitUntilCompleted];
    }
    [inflight_command_buffers_ removeAllObjects];
  }

  inflight_command_buffers_ = nil;

  for (auto &frame : frame_contexts_) {
    if (frame.fence) {
      dispatch_semaphore_wait(frame.fence, DISPATCH_TIME_FOREVER);
    }

    std::vector<std::function<void()>> callbacks;
    {
      std::lock_guard<std::mutex> lock(frame_mutex_);
      callbacks = std::move(frame.completion_callbacks);
      frame.active = false;
    }

    for (auto &callback : callbacks) {
      if (callback) {
        callback();
      }
    }

#if defined(OS_OBJECT_USE_OBJC) && OS_OBJECT_USE_OBJC
    frame.fence = nullptr;
#else
    if (frame.fence) {
      dispatch_release(frame.fence);
      frame.fence = nullptr;
    }
#endif
  }

  {
    std::vector<std::unique_ptr<MetalResource>> leftovers;
    {
      std::lock_guard<std::mutex> lock(resource_mutex_);
      for (auto &entry : deferred_destruction_queue_) {
        leftovers.push_back(std::move(entry.resource));
      }
      deferred_destruction_queue_.clear();
    }
  }

#if defined(OS_OBJECT_USE_OBJC) && OS_OBJECT_USE_OBJC
  inflight_semaphore_ = nullptr;
#else
  if (inflight_semaphore_) {
    dispatch_release(inflight_semaphore_);
    inflight_semaphore_ = nullptr;
  }
#endif

#if PIXEL_METAL_COUNTERS_AVAILABLE
  counter_sample_buffer_ = nil;
  counter_resolve_buffer_ = nil;
#endif
}

void MetalBackend::setup_profiling() {
#if PIXEL_METAL_COUNTERS_AVAILABLE
  profiling_enabled_ = false;
  counter_sample_buffer_ = nil;
  counter_resolve_buffer_ = nil;
  counter_sample_range_ = NSMakeRange(0, 0);

  if (!device_) {
    return;
  }

  if (@available(macOS 11.0, iOS 14.0, *)) {
    NSArray<id<MTLCounterSet>> *counterSets = device_.counterSets;
    if (!counterSets || counterSets.count == 0) {
      return;
    }

#ifdef MTLCounterSetStatistic
    NSString *statisticName = MTLCounterSetStatistic;
#else
    NSString *statisticName = @"Statistic";
#endif

    id<MTLCounterSet> statisticSet = nil;
    for (id<MTLCounterSet> set in counterSets) {
      if ([[set name] isEqualToString:statisticName]) {
        statisticSet = set;
        break;
      }
    }

    if (!statisticSet) {
      std::cerr << "Metal GPU statistics counter set not available" << std::endl;
      return;
    }

    MTLCounterSampleBufferDescriptor *descriptor =
        [[MTLCounterSampleBufferDescriptor alloc] init];
    descriptor.counterSet = statisticSet;
    descriptor.sampleCount = kCounterSampleCount;
    descriptor.storageMode = MTLStorageModeShared;

    NSError *error = nil;
    counter_sample_buffer_ =
        [device_ newCounterSampleBufferWithDescriptor:descriptor error:&error];
    if (!counter_sample_buffer_) {
      std::cerr << "Failed to create Metal counter sample buffer" << std::endl;
      if (error) {
        std::cerr << "  Reason: "
                  << to_std_string([error localizedDescription]) << std::endl;
      }
      return;
    }

    counter_resolve_buffer_ =
        [device_ newBufferWithLength:sizeof(MTLCounterResultStatistic) *
                                      kCounterSampleCount
                                options:MTLResourceStorageModeShared];
    if (!counter_resolve_buffer_) {
      std::cerr << "Failed to allocate Metal counter resolve buffer" << std::endl;
      counter_sample_buffer_ = nil;
      return;
    }

    counter_sample_range_ = NSMakeRange(0, kCounterSampleCount);
    profiling_enabled_ = true;
  }
#else
  profiling_enabled_ = false;
#endif
}

void MetalBackend::resolve_profiler_data(id<MTLCommandBuffer> command_buffer) {
  latest_stats_ = {};
#if PIXEL_METAL_COUNTERS_AVAILABLE
  latest_stats_.valid = false;

  if (!profiling_enabled_ || !counter_resolve_buffer_ || !counter_sample_buffer_) {
    return;
  }

  if (!command_buffer) {
    return;
  }

  if (@available(macOS 11.0, iOS 14.0, *)) {
    void *contents = [counter_resolve_buffer_ contents];
    if (!contents) {
      return;
    }

    if (counter_sample_range_.length < 2) {
      return;
    }

    const auto *results =
        static_cast<const MTLCounterResultStatistic *>(contents);
    NSUInteger startIndex = counter_sample_range_.location;
    NSUInteger endIndex = startIndex + 1;
    const MTLCounterResultStatistic &start = results[startIndex];
    const MTLCounterResultStatistic &end = results[endIndex];

    GPUFrameStats stats{};
    stats.vertexInvocations =
        end.vertexInvocations - start.vertexInvocations;
    stats.fragmentInvocations =
        end.fragmentInvocations - start.fragmentInvocations;
    stats.totalCycles = end.totalCycles - start.totalCycles;
    stats.renderTargetWriteCycles =
        end.renderTargetWriteCycles - start.renderTargetWriteCycles;

    if (command_buffer.GPUEndTime > command_buffer.GPUStartTime) {
      double delta = command_buffer.GPUEndTime - command_buffer.GPUStartTime;
      stats.shaderTimeMS = delta * 1000.0;
      if (delta > 0.0) {
        // Approximate bandwidth assuming each render-target write cycle commits
        // 16 bytes of data. This provides a coarse estimate suitable for
        // real-time HUDs during GPU capture.
        const double estimatedBytes =
            static_cast<double>(stats.renderTargetWriteCycles) * 16.0;
        stats.estimatedBandwidthGBs =
            (estimatedBytes / delta) / (1024.0 * 1024.0 * 1024.0);
      }
    }

    stats.valid = true;
    latest_stats_ = stats;
  }
#endif
}

void MetalBackend::begin_frame(const Color &clear_color) {
  if (!command_queue_ || !metal_layer_) {
    std::cerr << "Metal backend is not initialized before begin_frame" << std::endl;
    return;
  }

  if (!inflight_semaphore_) {
    std::cerr << "Metal inflight semaphore not initialized" << std::endl;
    return;
  }

  latest_stats_ = {};

  dispatch_semaphore_wait(inflight_semaphore_, DISPATCH_TIME_FOREVER);

  current_frame_id_ = frame_counter_++;
  size_t frame_index = current_frame_id_ % kMaxFramesInFlight;
  FrameContext &frame_context = frame_contexts_[frame_index];

  dispatch_semaphore_wait(frame_context.fence, DISPATCH_TIME_FOREVER);

  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    frame_context.frame_id = current_frame_id_;
    frame_context.active = true;
    frame_context.completion_callbacks.clear();
  }

  current_frame_context_ = &frame_context;

  current_drawable_ = [metal_layer_ nextDrawable];

  if (!current_drawable_) {
    std::cerr << "Failed to acquire CAMetalDrawable for frame" << std::endl;
    abort_frame(frame_context);
    return;
  }

  command_buffer_ = [command_queue_ commandBuffer];
  if (!command_buffer_) {
    std::cerr << "Failed to create Metal command buffer" << std::endl;
    current_drawable_ = nil;
    abort_frame(frame_context);
    return;
  }

  NSString *frameLabel =
      [NSString stringWithFormat:@"Frame_%llu", current_frame_id_];
  command_buffer_.label = frameLabel;

#if PIXEL_METAL_COUNTERS_AVAILABLE
  if (profiling_enabled_ && counter_sample_buffer_) {
    if (@available(macOS 11.0, iOS 14.0, *)) {
      [counter_sample_buffer_ resetWithRange:counter_sample_range_];
      [command_buffer_ sampleCountersInBuffer:counter_sample_buffer_
                                atSampleIndex:0
                                   withBarrier:YES];
    }
  }
#endif

  [command_buffer_ pushDebugGroup:frameLabel];

  FrameContext *captured_context = current_frame_context_;
  MetalBackend *backend = this;
  [command_buffer_ addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
    backend->on_command_buffer_complete(buffer, captured_context);
  }];

  if (!setup_render_pass_descriptor()) {
    std::cerr << "Failed to configure Metal render pass descriptor" << std::endl;
    [command_buffer_ popDebugGroup];
    command_buffer_ = nil;
    current_drawable_ = nil;
    abort_frame(frame_context);
    return;
  }

  render_pass_descriptor_.colorAttachments[0].clearColor = MTLClearColorMake(
      clear_color.r, clear_color.g, clear_color.b, clear_color.a);

  render_encoder_ = [command_buffer_
      renderCommandEncoderWithDescriptor:render_pass_descriptor_];

  if (!render_encoder_) {
    std::cerr << "Failed to create Metal render command encoder" << std::endl;
    [command_buffer_ popDebugGroup];
    command_buffer_ = nil;
    current_drawable_ = nil;
    abort_frame(frame_context);
    return;
  }

  render_encoder_.label = @"MainPass";
  [render_encoder_ pushDebugGroup:@"MainRenderPass"];
}

void MetalBackend::end_frame() {
  if (!command_buffer_ || !render_encoder_ || !current_drawable_) {
    std::cerr << "Attempted to end Metal frame without active encoder" << std::endl;
    if (current_frame_context_) {
      abort_frame(*current_frame_context_);
    } else if (inflight_semaphore_) {
      dispatch_semaphore_signal(inflight_semaphore_);
    }
    render_encoder_ = nil;
    command_buffer_ = nil;
    current_drawable_ = nil;
    return;
  }

  [render_encoder_ popDebugGroup];
  [render_encoder_ endEncoding];

#if PIXEL_METAL_COUNTERS_AVAILABLE
  if (profiling_enabled_ && counter_sample_buffer_ && counter_resolve_buffer_) {
    if (@available(macOS 11.0, iOS 14.0, *)) {
      [command_buffer_ sampleCountersInBuffer:counter_sample_buffer_
                                atSampleIndex:1
                                   withBarrier:YES];
      [command_buffer_ resolveCounters:counter_sample_buffer_
                                inRange:counter_sample_range_
                       destinationBuffer:counter_resolve_buffer_
                      destinationOffset:0];
    }
  }
#endif

  [command_buffer_ presentDrawable:current_drawable_];

  if (current_frame_context_) {
    id<CAMetalDrawable> drawable = current_drawable_;
    enqueue_completion_task([drawable]() { (void)drawable; });
  }

  track_inflight_command_buffer(command_buffer_);

  [command_buffer_ popDebugGroup];
  [command_buffer_ commit];

  render_encoder_ = nil;
  command_buffer_ = nil;
  current_drawable_ = nil;
  current_frame_context_ = nullptr;

  if (render_pass_descriptor_) {
    render_pass_descriptor_.colorAttachments[0].texture = nil;
  }
}

void MetalBackend::track_inflight_command_buffer(
    id<MTLCommandBuffer> command_buffer) {
  if (!command_buffer || !inflight_command_buffers_) {
    return;
  }

  std::lock_guard<std::mutex> lock(inflight_mutex_);
  [inflight_command_buffers_ addObject:command_buffer];
}

void MetalBackend::enqueue_completion_task(std::function<void()> callback) {
  if (!callback) {
    return;
  }

  if (!current_frame_context_) {
    callback();
    return;
  }

  std::lock_guard<std::mutex> lock(frame_mutex_);
  current_frame_context_->completion_callbacks.emplace_back(std::move(callback));
}

void MetalBackend::mark_resource_in_use(const MetalResource *resource) {
  if (!resource) {
    return;
  }

  resource->mark_used(current_frame_id_);

  std::lock_guard<std::mutex> lock(resource_mutex_);
  resource_last_usage_[resource] = current_frame_id_;
}

void MetalBackend::wait_for_resource(const MetalResource *resource) {
  if (!resource) {
    return;
  }

  std::unique_lock<std::mutex> lock(resource_mutex_);
  auto it = resource_last_usage_.find(resource);
  if (it == resource_last_usage_.end()) {
    return;
  }

  uint64_t target_frame = it->second;
  frame_completion_cv_.wait(lock, [&]() {
    return last_completed_frame_id_ >= target_frame;
  });
}

void MetalBackend::defer_resource_destruction(
    std::unique_ptr<MetalResource> resource) {
  if (!resource) {
    return;
  }

  const MetalResource *key = resource.get();
  uint64_t safe_frame = std::max(resource->last_used_frame(), current_frame_id_);

  resource->mark_pending_destruction();

  bool enqueued = false;

  {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    resource_last_usage_.erase(key);

    if (last_completed_frame_id_ < safe_frame) {
      deferred_destruction_queue_.push_back(
          DeferredResource{safe_frame, std::move(resource)});
      enqueued = true;
    }
  }

  if (enqueued) {
    return;
  }
  // Resource will be destroyed when going out of scope if already safe.
}

void MetalBackend::abort_frame(FrameContext &context) {
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    context.active = false;
    context.completion_callbacks.clear();
  }

  {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    last_completed_frame_id_ = std::max(last_completed_frame_id_, context.frame_id);
  }
  frame_completion_cv_.notify_all();

  if (context.fence) {
    dispatch_semaphore_signal(context.fence);
  }

  if (inflight_semaphore_) {
    dispatch_semaphore_signal(inflight_semaphore_);
  }

  current_frame_context_ = nullptr;
  command_buffer_ = nil;
  render_encoder_ = nil;
  current_drawable_ = nil;
}

void MetalBackend::log_nserror(const std::string &context, NSError *error) const {
  if (!error) {
    std::cerr << "[Metal] " << context << " failed with unknown error" << std::endl;
    return;
  }

  std::string domain = to_std_string([error domain]);
  std::string description = to_std_string([error localizedDescription]);

  std::cerr << "[Metal] " << context << " failed (" << domain << ":"
            << error.code << ") - " << description << std::endl;

  NSDictionary *userInfo = [error userInfo];
  if (userInfo) {
    NSString *infoDescription = [userInfo description];
    std::string details = to_std_string(infoDescription);
    if (!details.empty()) {
      std::cerr << "[Metal]   userInfo: " << details << std::endl;
    }

    if (@available(macOS 11.0, *)) {
      id encoderInfosObj = userInfo[MTLCommandBufferEncoderInfoErrorKey];
      if ([encoderInfosObj isKindOfClass:[NSArray class]]) {
        NSArray *encoderInfos = (NSArray *)encoderInfosObj;
        for (id entry in encoderInfos) {
          NSString *label = nil;
          if ([entry respondsToSelector:@selector(label)]) {
            label = [entry label];
          }

          NSString *errorText = nil;
          if ([entry respondsToSelector:@selector(valueForKey:)]) {
            id entryError = [entry valueForKey:@"error"];
            if ([entryError isKindOfClass:[NSError class]]) {
              errorText = [entryError localizedDescription];
            }
          }

          if (!errorText && [entry respondsToSelector:@selector(description)]) {
            errorText = [entry description];
          }

          std::string labelStr = to_std_string(label);
          std::string errorStr = to_std_string(errorText);
          if (!labelStr.empty() || !errorStr.empty()) {
            std::cerr << "[Metal]   Encoder '"
                      << (labelStr.empty() ? "<unnamed>" : labelStr)
                      << "': "
                      << (errorStr.empty() ? "<no description>" : errorStr)
                      << std::endl;
          }
        }
      }
    }
  }
}

std::string MetalBackend::describe_command_buffer(
    id<MTLCommandBuffer> command_buffer) const {
  if (!command_buffer) {
    return "CommandBuffer<null>";
  }

  std::ostringstream oss;
  std::string label = to_std_string([command_buffer label]);
  if (!label.empty()) {
    oss << label;
  } else {
    oss << "CommandBuffer@" << command_buffer;
  }

  return oss.str();
}

void MetalBackend::on_command_buffer_complete(
    id<MTLCommandBuffer> command_buffer, FrameContext *frame_context) {
  resolve_profiler_data(command_buffer);

  if (command_buffer && command_buffer.status == MTLCommandBufferStatusError) {
    std::string context = "command buffer " + describe_command_buffer(command_buffer);
    log_nserror(context, command_buffer.error);
  }

  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    if (command_buffer && inflight_command_buffers_) {
      [inflight_command_buffers_ removeObjectIdenticalTo:command_buffer];
    }
  }

  if (frame_context) {
    std::vector<std::function<void()>> callbacks;
    std::vector<std::unique_ptr<MetalResource>> ready_for_destruction;

    {
      std::lock_guard<std::mutex> lock(frame_mutex_);
      callbacks = std::move(frame_context->completion_callbacks);
      frame_context->active = false;
    }

    for (auto &callback : callbacks) {
      if (callback) {
        callback();
      }
    }

    {
      std::lock_guard<std::mutex> lock(resource_mutex_);
      last_completed_frame_id_ =
          std::max(last_completed_frame_id_, frame_context->frame_id);

      auto it = deferred_destruction_queue_.begin();
      while (it != deferred_destruction_queue_.end()) {
        if (it->frame_id <= last_completed_frame_id_) {
          ready_for_destruction.push_back(std::move(it->resource));
          it = deferred_destruction_queue_.erase(it);
        } else {
          ++it;
        }
      }
    }
    frame_completion_cv_.notify_all();

    ready_for_destruction.clear();

    if (frame_context->fence) {
      dispatch_semaphore_signal(frame_context->fence);
    }
  }

  if (inflight_semaphore_) {
    dispatch_semaphore_signal(inflight_semaphore_);
  }
}

bool MetalBackend::setup_render_pass_descriptor() {
  if (!current_drawable_) {
    std::cerr << "No drawable available for render pass" << std::endl;
    return false;
  }

  if (!render_pass_descriptor_) {
    render_pass_descriptor_ = [[MTLRenderPassDescriptor alloc] init];
  }

  if (!render_pass_descriptor_) {
    std::cerr << "Failed to allocate Metal render pass descriptor" << std::endl;
    return false;
  }

  id<MTLTexture> drawableTexture = current_drawable_.texture;
  if (!drawableTexture) {
    std::cerr << "CAMetalDrawable returned without texture" << std::endl;
    return false;
  }

  if (!depth_texture_) {
    std::cerr << "Depth texture is not initialized" << std::endl;
    return false;
  }

  // Color attachment
  render_pass_descriptor_.colorAttachments[0].texture = drawableTexture;
  render_pass_descriptor_.colorAttachments[0].loadAction = MTLLoadActionClear;
  render_pass_descriptor_.colorAttachments[0].storeAction = MTLStoreActionStore;

  // Depth attachment
  render_pass_descriptor_.depthAttachment.texture = depth_texture_;
  render_pass_descriptor_.depthAttachment.loadAction = MTLLoadActionClear;
  render_pass_descriptor_.depthAttachment.storeAction = MTLStoreActionDontCare;
  render_pass_descriptor_.depthAttachment.clearDepth = 1.0;

  // Stencil attachment shares depth texture
  render_pass_descriptor_.stencilAttachment.texture = depth_texture_;
  render_pass_descriptor_.stencilAttachment.loadAction = MTLLoadActionClear;
  render_pass_descriptor_.stencilAttachment.storeAction =
      MTLStoreActionDontCare;
  render_pass_descriptor_.stencilAttachment.clearStencil = 0;

  return true;
}

void MetalBackend::create_default_shaders() {
  // Stub - shaders loaded from library
}

std::unique_ptr<MetalShader>
MetalBackend::create_shader(const std::string &vertex_src,
                            const std::string &fragment_src) {
  return MetalShader::create(device_, vertex_src, fragment_src);
}

std::unique_ptr<MetalMesh>
MetalBackend::create_mesh(const std::vector<Vertex> &vertices,
                          const std::vector<uint32_t> &indices) {
  return MetalMesh::create(device_, command_queue_, vertices, indices);
}

  std::unique_ptr<MetalTexture>
  MetalBackend::create_texture(int width, int height, const uint8_t *data) {
    return MetalTexture::create(device_, width, height, data);
  }

  MetalBackend::DepthStencilStateKey
  MetalBackend::make_depth_stencil_key(const Material &material) const {
    DepthStencilStateKey key;
    key.depth_test = material.depth_test;
    key.depth_write = material.depth_write;
    key.depth_compare = material.depth_compare;
    key.stencil_enable = material.stencil_enable;
    key.stencil_compare = material.stencil_compare;
    key.stencil_fail_op = material.stencil_fail_op;
    key.stencil_depth_fail_op = material.stencil_depth_fail_op;
    key.stencil_pass_op = material.stencil_pass_op;
    key.stencil_read_mask = material.stencil_read_mask;
    key.stencil_write_mask = material.stencil_write_mask;
    return key;
  }

  id<MTLDepthStencilState>
  MetalBackend::get_depth_stencil_state(const DepthStencilStateKey &key) {
    auto it = depth_stencil_states_.find(key);
    if (it != depth_stencil_states_.end()) {
      return it->second;
    }

    MTLDepthStencilDescriptor *descriptor =
        [[MTLDepthStencilDescriptor alloc] init];

    descriptor.depthCompareFunction =
        key.depth_test ? to_mtl_compare(key.depth_compare)
                        : MTLCompareFunctionAlways;
    descriptor.depthWriteEnabled = key.depth_write ? YES : NO;

    if (key.stencil_enable) {
    MTLStencilDescriptor *front = [[MTLStencilDescriptor alloc] init];
    front.stencilCompareFunction = to_mtl_compare(key.stencil_compare);
    front.stencilFailureOperation = to_mtl_stencil(key.stencil_fail_op);
    front.depthFailureOperation =
        to_mtl_stencil(key.stencil_depth_fail_op);
    front.depthStencilPassOperation = to_mtl_stencil(key.stencil_pass_op);
    front.readMask = key.stencil_read_mask;
    front.writeMask = key.stencil_write_mask;
    descriptor.frontFaceStencil = front;

    MTLStencilDescriptor *back = [[MTLStencilDescriptor alloc] init];
    back.stencilCompareFunction = to_mtl_compare(key.stencil_compare);
    back.stencilFailureOperation = to_mtl_stencil(key.stencil_fail_op);
    back.depthFailureOperation =
        to_mtl_stencil(key.stencil_depth_fail_op);
    back.depthStencilPassOperation = to_mtl_stencil(key.stencil_pass_op);
    back.readMask = key.stencil_read_mask;
    back.writeMask = key.stencil_write_mask;
    descriptor.backFaceStencil = back;
  }

  id<MTLDepthStencilState> state =
      [device_ newDepthStencilStateWithDescriptor:descriptor];

  if (state) {
    depth_stencil_states_.emplace(key, state);
  } else {
    std::cerr << "Failed to create Metal depth-stencil state" << std::endl;
  }
  return state;
}

void MetalBackend::draw_mesh(const MetalMesh &mesh, const Vec3 &position,
                             const Vec3 &rotation, const Vec3 &scale,
                             MetalShader *shader, const Material &material) {
  (void)position;
  (void)rotation;
  (void)scale;

  if (!shader || !render_encoder_) {
    return;
  }

  mark_resource_in_use(&mesh);
  mark_resource_in_use(shader);

  [render_encoder_ pushDebugGroup:@"DrawMesh"];

  DepthStencilStateKey key = make_depth_stencil_key(material);
  id<MTLDepthStencilState> depth_state = get_depth_stencil_state(key);

  if (!depth_state && material.depth_test) {
    std::cerr << "Failed to retrieve Metal depth state for material" << std::endl;
  }

  shader->bind(render_encoder_, depth_state);

  if (material.stencil_enable) {
    [render_encoder_ setStencilReferenceValue:material.stencil_reference];
  }

  mesh.draw(render_encoder_);

  [render_encoder_ popDebugGroup];
}

} // namespace pixel::renderer3d::metal

#endif // __APPLE__
