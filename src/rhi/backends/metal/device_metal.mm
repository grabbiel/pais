// src/rhi/backends/metal/device_metal.mm
#ifdef __APPLE__

#include "pixel/rhi/rhi.hpp"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <iostream>
#include <unordered_map>
#include <vector>
#include <cstring>

namespace pixel::rhi::metal {

// ============================================================================
// Helper Functions
// ============================================================================

static MTLPixelFormat toMTLPixelFormat(Format format) {
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
    return 4;
  case Format::D32F:
    return 4;
  default:
    return 4;
  }
}

// ============================================================================
// Metal Resource Storage
// ============================================================================

struct MTLBufferResource {
  id<MTLBuffer> buffer = nil;
  size_t size = 0;
  BufferUsage usage = BufferUsage::None;
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
  ShaderHandle vs{0};
  ShaderHandle fs{0};
  ShaderHandle cs{0};
  std::unordered_map<std::string, uint32_t> argument_indices;
};

// ============================================================================
// Uniform Structure (matching shaders.metal)
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
};

// ============================================================================
// Metal Command List
// ============================================================================

class MetalCmdList : public CmdList {
public:
  MetalCmdList(id<MTLDevice> device, id<MTLCommandQueue> command_queue,
               CAMetalLayer *layer, id<MTLTexture> depth_texture,
               std::unordered_map<uint32_t, MTLBufferResource> *buffers,
               std::unordered_map<uint32_t, MTLTextureResource> *textures,
               std::unordered_map<uint32_t, MTLPipelineResource> *pipelines)
      : device_(device), command_queue_(command_queue), layer_(layer),
        depth_texture_(depth_texture), buffers_(buffers), textures_(textures),
        pipelines_(pipelines) {

    // Create uniform buffer for per-frame uniforms
    uniform_buffer_ = [device newBufferWithLength:sizeof(Uniforms)
                                          options:MTLResourceCPUCacheModeWriteCombined];
  }

  void begin() override {
    recording_ = true;
    command_buffer_ = [command_queue_ commandBuffer];
  }

  void beginRender(TextureHandle rtColor, TextureHandle rtDepth, float clear[4],
                   float clearDepth, uint8_t clearStencil) override {
    @autoreleasepool {
      // Get next drawable
      current_drawable_ = [layer_ nextDrawable];

      if (!current_drawable_) {
        std::cerr << "Failed to get next drawable" << std::endl;
        return;
      }

      // Setup render pass descriptor
      MTLRenderPassDescriptor *renderPassDesc = [MTLRenderPassDescriptor renderPassDescriptor];

      // Color attachment
      renderPassDesc.colorAttachments[0].texture = current_drawable_.texture;
      renderPassDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
      renderPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
      renderPassDesc.colorAttachments[0].clearColor = MTLClearColorMake(
          clear[0], clear[1], clear[2], clear[3]);

      // Depth attachment
      renderPassDesc.depthAttachment.texture = depth_texture_;
      renderPassDesc.depthAttachment.loadAction = MTLLoadActionClear;
      renderPassDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
      renderPassDesc.depthAttachment.clearDepth = clearDepth;

      // Create render encoder
      render_encoder_ = [command_buffer_ renderCommandEncoderWithDescriptor:renderPassDesc];
    }
  }

  void setPipeline(PipelineHandle handle) override {
    auto it = pipelines_->find(handle.id);
    if (it == pipelines_->end())
      return;

    current_pipeline_ = handle;
    const MTLPipelineResource &pipeline = it->second;

    [render_encoder_ setRenderPipelineState:pipeline.pipeline_state];
    [render_encoder_ setDepthStencilState:pipeline.depth_stencil_state];

    // Set default cull mode and winding order
    [render_encoder_ setCullMode:MTLCullModeBack];
    [render_encoder_ setFrontFacingWinding:MTLWindingCounterClockwise];
  }

  void setVertexBuffer(BufferHandle handle, size_t offset) override {
    auto it = buffers_->find(handle.id);
    if (it == buffers_->end())
      return;

    current_vb_ = handle;
    current_vb_offset_ = offset;

    [render_encoder_ setVertexBuffer:it->second.buffer offset:offset atIndex:0];
  }

  void setIndexBuffer(BufferHandle handle, size_t offset) override {
    auto it = buffers_->find(handle.id);
    if (it == buffers_->end())
      return;

    current_ib_ = handle;
    current_ib_offset_ = offset;
  }

  void setInstanceBuffer(BufferHandle handle, size_t stride,
                        size_t offset) override {
    auto it = buffers_->find(handle.id);
    if (it == buffers_->end())
      return;

    // Set instance buffer at index 2 (matches shaders.metal)
    [render_encoder_ setVertexBuffer:it->second.buffer offset:offset atIndex:2];
  }

  void setUniformMat4(const char *name, const float *mat4x4) override {
    Uniforms *uniforms = (Uniforms *)uniform_buffer_.contents;
    std::string name_str(name);

    if (name_str == "model") {
      memcpy(uniforms->model, mat4x4, sizeof(float) * 16);
    } else if (name_str == "view") {
      memcpy(uniforms->view, mat4x4, sizeof(float) * 16);
    } else if (name_str == "projection") {
      memcpy(uniforms->projection, mat4x4, sizeof(float) * 16);
    }

    // Bind uniform buffer to both vertex and fragment shaders (index 1)
    [render_encoder_ setVertexBuffer:uniform_buffer_ offset:0 atIndex:1];
    [render_encoder_ setFragmentBuffer:uniform_buffer_ offset:0 atIndex:1];
  }

  void setUniformVec3(const char *name, const float *vec3) override {
    Uniforms *uniforms = (Uniforms *)uniform_buffer_.contents;
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

    // Bind uniform buffer
    [render_encoder_ setVertexBuffer:uniform_buffer_ offset:0 atIndex:1];
    [render_encoder_ setFragmentBuffer:uniform_buffer_ offset:0 atIndex:1];
  }

  void setUniformVec4(const char *name, const float *vec4) override {
    // Currently not used in shaders, but available for future use
  }

  void setUniformInt(const char *name, int value) override {
    Uniforms *uniforms = (Uniforms *)uniform_buffer_.contents;
    std::string name_str(name);

    if (name_str == "useTexture") {
      uniforms->useTexture = value;
    } else if (name_str == "useTextureArray") {
      uniforms->useTextureArray = value;
    } else if (name_str == "ditherEnabled") {
      uniforms->ditherEnabled = value;
    }

    // Bind uniform buffer
    [render_encoder_ setVertexBuffer:uniform_buffer_ offset:0 atIndex:1];
    [render_encoder_ setFragmentBuffer:uniform_buffer_ offset:0 atIndex:1];
  }

  void setUniformFloat(const char *name, float value) override {
    Uniforms *uniforms = (Uniforms *)uniform_buffer_.contents;
    std::string name_str(name);

    if (name_str == "time") {
      uniforms->time = value;
    }

    // Bind uniform buffer
    [render_encoder_ setVertexBuffer:uniform_buffer_ offset:0 atIndex:1];
    [render_encoder_ setFragmentBuffer:uniform_buffer_ offset:0 atIndex:1];
  }

  void setUniformBuffer(uint32_t binding, BufferHandle buffer, size_t offset,
                        size_t size) override {
    auto it = buffers_->find(buffer.id);
    if (it == buffers_->end())
      return;

    // Bind to both vertex and fragment shaders at the specified binding point
    [render_encoder_ setVertexBuffer:it->second.buffer offset:offset atIndex:binding];
    [render_encoder_ setFragmentBuffer:it->second.buffer offset:offset atIndex:binding];
  }

  void setTexture(const char *name, TextureHandle texture,
                  uint32_t slot) override {
    auto it = textures_->find(texture.id);
    if (it == textures_->end())
      return;

    // Set texture at the specified slot for fragment shader
    [render_encoder_ setFragmentTexture:it->second.texture atIndex:slot];

    // Create a default sampler if needed
    if (!default_sampler_) {
      MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
      samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
      samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
      samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
      samplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
      samplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;
      default_sampler_ = [device_ newSamplerStateWithDescriptor:samplerDesc];
    }

    [render_encoder_ setFragmentSamplerState:default_sampler_ atIndex:slot];
  }

  void copyToTexture(TextureHandle texture, uint32_t mipLevel,
                     std::span<const std::byte> data) override {
    auto it = textures_->find(texture.id);
    if (it == textures_->end())
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

  void copyToTextureLayer(TextureHandle texture, uint32_t layer,
                          uint32_t mipLevel,
                          std::span<const std::byte> data) override {
    auto it = textures_->find(texture.id);
    if (it == textures_->end())
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

  void drawIndexed(uint32_t indexCount, uint32_t firstIndex,
                   uint32_t instanceCount) override {
    if (current_pipeline_.id == 0 || current_ib_.id == 0)
      return;

    auto ib_it = buffers_->find(current_ib_.id);
    if (ib_it == buffers_->end())
      return;

    size_t indexOffset = current_ib_offset_ + firstIndex * sizeof(uint32_t);

    [render_encoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:indexCount
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:ib_it->second.buffer
                         indexBufferOffset:indexOffset
                             instanceCount:instanceCount];
  }

  // Compute shader support
  void setComputePipeline(PipelineHandle handle) override {
    auto it = pipelines_->find(handle.id);
    if (it == pipelines_->end())
      return;

    current_compute_pipeline_ = handle;
    const MTLPipelineResource &pipeline = it->second;

    // End render encoder if active
    if (render_encoder_) {
      [render_encoder_ endEncoding];
      render_encoder_ = nil;
    }

    // Create compute encoder if needed
    if (!compute_encoder_) {
      compute_encoder_ = [command_buffer_ computeCommandEncoder];
    }

    [compute_encoder_ setComputePipelineState:pipeline.compute_pipeline_state];
  }

  void setStorageBuffer(uint32_t binding, BufferHandle buffer, size_t offset,
                       size_t size) override {
    auto it = buffers_->find(buffer.id);
    if (it == buffers_->end())
      return;

    const MTLBufferResource &buf = it->second;

    if (compute_encoder_) {
      [compute_encoder_ setBuffer:buf.buffer offset:offset atIndex:binding];
    }
  }

  void dispatch(uint32_t groupCountX, uint32_t groupCountY,
               uint32_t groupCountZ) override {
    if (!compute_encoder_)
      return;

    auto it = pipelines_->find(current_compute_pipeline_.id);
    if (it == pipelines_->end())
      return;

    // Get the thread group size from the pipeline
    NSUInteger threadExecutionWidth = it->second.compute_pipeline_state.threadExecutionWidth;
    NSUInteger maxThreads = it->second.compute_pipeline_state.maxTotalThreadsPerThreadgroup;

    // Use a reasonable thread group size (e.g., 256 threads per group)
    MTLSize threadsPerGroup = MTLSizeMake(MIN(256, maxThreads), 1, 1);
    MTLSize threadgroups = MTLSizeMake(groupCountX, groupCountY, groupCountZ);

    [compute_encoder_ dispatchThreadgroups:threadgroups
                     threadsPerThreadgroup:threadsPerGroup];
  }

  void memoryBarrier() override {
    if (compute_encoder_) {
      // Metal handles memory barriers automatically
      // But we can end and restart the encoder if needed for explicit barriers
    }
  }

  void endRender() override {
    if (render_encoder_) {
      [render_encoder_ endEncoding];
      render_encoder_ = nil;
    }
    if (compute_encoder_) {
      [compute_encoder_ endEncoding];
      compute_encoder_ = nil;
    }
  }

  void copyToBuffer(BufferHandle handle, size_t dstOff,
                    std::span<const std::byte> src) override {
    auto it = buffers_->find(handle.id);
    if (it == buffers_->end())
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

  void end() override {
    if (current_drawable_ && command_buffer_) {
      [command_buffer_ presentDrawable:current_drawable_];
      [command_buffer_ commit];
    }

    command_buffer_ = nil;
    current_drawable_ = nil;
    recording_ = false;
  }

private:
  id<MTLDevice> device_;
  id<MTLCommandQueue> command_queue_;
  CAMetalLayer *layer_;
  id<MTLTexture> depth_texture_;

  std::unordered_map<uint32_t, MTLBufferResource> *buffers_;
  std::unordered_map<uint32_t, MTLTextureResource> *textures_;
  std::unordered_map<uint32_t, MTLPipelineResource> *pipelines_;

  id<MTLCommandBuffer> command_buffer_ = nil;
  id<MTLRenderCommandEncoder> render_encoder_ = nil;
  id<MTLComputeCommandEncoder> compute_encoder_ = nil;
  id<CAMetalDrawable> current_drawable_ = nil;

  id<MTLBuffer> uniform_buffer_ = nil;
  id<MTLSamplerState> default_sampler_ = nil;

  bool recording_ = false;
  PipelineHandle current_pipeline_{0};
  PipelineHandle current_compute_pipeline_{0};
  BufferHandle current_vb_{0};
  BufferHandle current_ib_{0};
  size_t current_vb_offset_ = 0;
  size_t current_ib_offset_ = 0;
};

// ============================================================================
// Metal Device
// ============================================================================

class MetalDevice : public Device {
public:
  MetalDevice(GLFWwindow *glfw_window) {
    // Get the native Cocoa window from GLFW
    NSWindow *nsWindow = glfwGetCocoaWindow(glfw_window);

    // Create Metal device
    device_ = MTLCreateSystemDefaultDevice();
    if (!device_) {
      std::cerr << "Metal is not supported on this device" << std::endl;
      return;
    }

    std::cout << "Metal device: " << [device_.name UTF8String] << std::endl;

    // Create command queue
    command_queue_ = [device_ newCommandQueue];

    // Create Metal layer
    metal_layer_ = [CAMetalLayer layer];
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

    // Create depth texture
    updateDepthTexture();

    // Set capabilities
    caps_.instancing = true;
    caps_.uniformBuffers = true;
    caps_.samplerAniso = true;
    caps_.clipSpaceYDown = true; // Metal has Y-down clip space

    // Create command list
    cmd_list_ = std::make_unique<MetalCmdList>(
        device_, command_queue_, metal_layer_, depth_texture_,
        &buffers_, &textures_, &pipelines_);
  }

  ~MetalDevice() override {
    // ARC handles cleanup of Objective-C objects
  }

  const Caps &caps() const override { return caps_; }

  BufferHandle createBuffer(const BufferDesc &desc) override {
    MTLBufferResource buffer;

    MTLResourceOptions options = MTLResourceCPUCacheModeWriteCombined;
    if (desc.hostVisible) {
      options |= MTLResourceStorageModeShared;
    } else {
      options |= MTLResourceStorageModePrivate;
    }

    buffer.buffer = [device_ newBufferWithLength:desc.size options:options];
    buffer.size = desc.size;
    buffer.usage = desc.usage;
    buffer.host_visible = desc.hostVisible;

    if (!buffer.buffer) {
      std::cerr << "Failed to create Metal buffer" << std::endl;
      return BufferHandle{0};
    }

    uint32_t handle_id = next_buffer_id_++;
    buffers_[handle_id] = buffer;
    return BufferHandle{handle_id};
  }

  TextureHandle createTexture(const TextureDesc &desc) override {
    MTLTextureResource texture;

    MTLTextureDescriptor *texDesc;

    if (desc.layers > 1) {
      texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:toMTLPixelFormat(desc.format)
                                                                   width:desc.size.w
                                                                  height:desc.size.h
                                                               mipmapped:desc.mipLevels > 1];
      texDesc.textureType = MTLTextureType2DArray;
      texDesc.arrayLength = desc.layers;
    } else {
      texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:toMTLPixelFormat(desc.format)
                                                                   width:desc.size.w
                                                                  height:desc.size.h
                                                               mipmapped:desc.mipLevels > 1];
    }

    texDesc.usage = MTLTextureUsageShaderRead;
    texDesc.storageMode = MTLStorageModeShared; // Shared mode allows CPU access for uploads

    texture.texture = [device_ newTextureWithDescriptor:texDesc];
    texture.width = desc.size.w;
    texture.height = desc.size.h;
    texture.layers = desc.layers;
    texture.format = desc.format;

    if (!texture.texture) {
      std::cerr << "Failed to create Metal texture" << std::endl;
      return TextureHandle{0};
    }

    uint32_t handle_id = next_texture_id_++;
    textures_[handle_id] = texture;
    return TextureHandle{handle_id};
  }

  SamplerHandle createSampler(const SamplerDesc &desc) override {
    MTLSamplerResource sampler;

    MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
    samplerDesc.minFilter = desc.linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    samplerDesc.magFilter = desc.linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
    samplerDesc.sAddressMode = desc.repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
    samplerDesc.tAddressMode = desc.repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;

    sampler.sampler = [device_ newSamplerStateWithDescriptor:samplerDesc];

    if (!sampler.sampler) {
      std::cerr << "Failed to create Metal sampler" << std::endl;
      return SamplerHandle{0};
    }

    uint32_t handle_id = next_sampler_id_++;
    samplers_[handle_id] = sampler;
    return SamplerHandle{handle_id};
  }

  ShaderHandle createShader(std::string_view stage,
                            std::span<const uint8_t> bytes) override {
    MTLShaderResource shader;

    // Load default library (contains pre-compiled shaders from shaders.metal)
    if (!library_) {
      library_ = [device_ newDefaultLibrary];
      if (!library_) {
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

    shader.function = [library_ newFunctionWithName:functionName];
    shader.stage = std::string(stage);

    if (!shader.function) {
      std::cerr << "Failed to load shader function: " << [functionName UTF8String] << std::endl;
      return ShaderHandle{0};
    }

    uint32_t handle_id = next_shader_id_++;
    shaders_[handle_id] = shader;
    return ShaderHandle{handle_id};
  }

  PipelineHandle createPipeline(const PipelineDesc &desc) override {
    MTLPipelineResource pipeline;

    // Check if this is a compute pipeline
    if (desc.cs.id != 0) {
      auto cs_it = shaders_.find(desc.cs.id);
      if (cs_it == shaders_.end()) {
        std::cerr << "Invalid compute shader handle for pipeline" << std::endl;
        return PipelineHandle{0};
      }

      NSError *error = nil;
      pipeline.compute_pipeline_state = [device_ newComputePipelineStateWithFunction:cs_it->second.function
                                                                                error:&error];

      if (!pipeline.compute_pipeline_state) {
        std::cerr << "Failed to create compute pipeline state: "
                  << [[error localizedDescription] UTF8String] << std::endl;
        return PipelineHandle{0};
      }

      pipeline.cs = desc.cs;

      uint32_t handle_id = next_pipeline_id_++;
      pipelines_[handle_id] = pipeline;

      return PipelineHandle{handle_id};
    }

    // Graphics pipeline
    auto vs_it = shaders_.find(desc.vs.id);
    auto fs_it = shaders_.find(desc.fs.id);

    if (vs_it == shaders_.end() || fs_it == shaders_.end()) {
      std::cerr << "Invalid shader handles for pipeline" << std::endl;
      return PipelineHandle{0};
    }

    MTLRenderPipelineDescriptor *pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDesc.vertexFunction = vs_it->second.function;
    pipelineDesc.fragmentFunction = fs_it->second.function;
    pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    // Configure blending
    pipelineDesc.colorAttachments[0].blendingEnabled = YES;
    pipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    // Setup vertex descriptor to match renderer3d::Vertex
    MTLVertexDescriptor *vertexDesc = [[MTLVertexDescriptor alloc] init];

    // Position (vec3, offset 0)
    vertexDesc.attributes[0].format = MTLVertexFormatFloat3;
    vertexDesc.attributes[0].offset = 0;
    vertexDesc.attributes[0].bufferIndex = 0;

    // Normal (vec3, offset 12)
    vertexDesc.attributes[1].format = MTLVertexFormatFloat3;
    vertexDesc.attributes[1].offset = 12;
    vertexDesc.attributes[1].bufferIndex = 0;

    // TexCoord (vec2, offset 24)
    vertexDesc.attributes[2].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[2].offset = 24;
    vertexDesc.attributes[2].bufferIndex = 0;

    // Color (vec4, offset 32)
    vertexDesc.attributes[3].format = MTLVertexFormatFloat4;
    vertexDesc.attributes[3].offset = 32;
    vertexDesc.attributes[3].bufferIndex = 0;

    // Vertex buffer layout (stride 48 bytes)
    vertexDesc.layouts[0].stride = 48;
    vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    vertexDesc.layouts[0].stepRate = 1;

    pipelineDesc.vertexDescriptor = vertexDesc;

    NSError *error = nil;
    pipeline.pipeline_state = [device_ newRenderPipelineStateWithDescriptor:pipelineDesc
                                                                       error:&error];

    if (!pipeline.pipeline_state) {
      std::cerr << "Failed to create pipeline state: "
                << [[error localizedDescription] UTF8String] << std::endl;
      return PipelineHandle{0};
    }

    // Create depth stencil state
    MTLDepthStencilDescriptor *depthDesc = [[MTLDepthStencilDescriptor alloc] init];
    depthDesc.depthCompareFunction = MTLCompareFunctionLess;
    depthDesc.depthWriteEnabled = YES;

    pipeline.depth_stencil_state = [device_ newDepthStencilStateWithDescriptor:depthDesc];
    pipeline.vs = desc.vs;
    pipeline.fs = desc.fs;

    uint32_t handle_id = next_pipeline_id_++;
    pipelines_[handle_id] = pipeline;

    return PipelineHandle{handle_id};
  }

  CmdList *getImmediate() override { return cmd_list_.get(); }

  void present() override {
    // Update depth texture if window size changed
    CGSize viewSize = metal_layer_.bounds.size;
    if (viewSize.width != viewport_width_ || viewSize.height != viewport_height_) {
      viewport_width_ = viewSize.width;
      viewport_height_ = viewSize.height;
      metal_layer_.drawableSize = viewSize;
      updateDepthTexture();

      // Update command list's depth texture reference
      cmd_list_ = std::make_unique<MetalCmdList>(
          device_, command_queue_, metal_layer_, depth_texture_,
          &buffers_, &textures_, &pipelines_);
    }
  }

private:
  void updateDepthTexture() {
    MTLTextureDescriptor *depthDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:viewport_width_
                                    height:viewport_height_
                                 mipmapped:NO];
    depthDesc.usage = MTLTextureUsageRenderTarget;
    depthDesc.storageMode = MTLStorageModePrivate;

    depth_texture_ = [device_ newTextureWithDescriptor:depthDesc];
  }

  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> command_queue_ = nil;
  id<MTLLibrary> library_ = nil;
  CAMetalLayer *metal_layer_ = nil;
  id<MTLTexture> depth_texture_ = nil;

  int viewport_width_ = 0;
  int viewport_height_ = 0;

  Caps caps_;
  std::unique_ptr<MetalCmdList> cmd_list_;

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
};

} // namespace pixel::rhi::metal

// ============================================================================
// Factory Function
// ============================================================================

namespace pixel::rhi {

Device *create_metal_device(void *window) {
  return new metal::MetalDevice(static_cast<GLFWwindow*>(window));
}

} // namespace pixel::rhi

#endif // __APPLE__
