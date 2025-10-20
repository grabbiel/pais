// src/renderer3d/metal/metal_backend.mm
#ifdef __APPLE__

#include "pixel/renderer3d/metal/metal_renderer.hpp"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#include <iostream>
#include <fstream>
#include <sstream>

namespace pixel::renderer3d::metal {

// ============================================================================
// Metal Shader Implementation
// ============================================================================

std::unique_ptr<MetalShader>
MetalShader::create(id<MTLDevice> device, const std::string &vertex_src,
                    const std::string &fragment_src) {
  auto shader = std::unique_ptr<MetalShader>(new MetalShader());
  shader->device_ = device;

  // Compile shader library from source
  NSError *error = nil;

  // For now, we'll use the default library with pre-compiled shaders
  // In production, you'd compile from source strings
  shader->library_ = [device newDefaultLibrary];

  if (!shader->library_) {
    std::cerr << "Failed to create shader library" << std::endl;
    return nullptr;
  }

  // Get vertex and fragment functions
  shader->vertex_function_ =
      [shader->library_ newFunctionWithName:@"vertex_main"];
  shader->fragment_function_ =
      [shader->library_ newFunctionWithName:@"fragment_main"];

  if (!shader->vertex_function_ || !shader->fragment_function_) {
    // Try instanced shaders
    shader->vertex_function_ =
        [shader->library_ newFunctionWithName:@"vertex_instanced"];
    shader->fragment_function_ =
        [shader->library_ newFunctionWithName:@"fragment_instanced"];
  }

  if (!shader->vertex_function_ || !shader->fragment_function_) {
    std::cerr << "Failed to load shader functions" << std::endl;
    return nullptr;
  }

  // Create pipeline state
  MTLRenderPipelineDescriptor *pipelineDesc =
      [[MTLRenderPipelineDescriptor alloc] init];
  pipelineDesc.vertexFunction = shader->vertex_function_;
  pipelineDesc.fragmentFunction = shader->fragment_function_;
  pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  pipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

  // Configure blending
  pipelineDesc.colorAttachments[0].blendingEnabled = YES;
  pipelineDesc.colorAttachments[0].sourceRGBBlendFactor =
      MTLBlendFactorSourceAlpha;
  pipelineDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;

  // Create vertex descriptor
  MTLVertexDescriptor *vertexDesc = [[MTLVertexDescriptor alloc] init];

  // Position
  vertexDesc.attributes[0].format = MTLVertexFormatFloat3;
  vertexDesc.attributes[0].offset = offsetof(Vertex, position);
  vertexDesc.attributes[0].bufferIndex = 0;

  // Normal
  vertexDesc.attributes[1].format = MTLVertexFormatFloat3;
  vertexDesc.attributes[1].offset = offsetof(Vertex, normal);
  vertexDesc.attributes[1].bufferIndex = 0;

  // TexCoord
  vertexDesc.attributes[2].format = MTLVertexFormatFloat2;
  vertexDesc.attributes[2].offset = offsetof(Vertex, texcoord);
  vertexDesc.attributes[2].bufferIndex = 0;

  // Color
  vertexDesc.attributes[3].format = MTLVertexFormatFloat4;
  vertexDesc.attributes[3].offset = offsetof(Vertex, color);
  vertexDesc.attributes[3].bufferIndex = 0;

  vertexDesc.layouts[0].stride = sizeof(Vertex);
  vertexDesc.layouts[0].stepRate = 1;
  vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

  pipelineDesc.vertexDescriptor = vertexDesc;

  shader->pipeline_state_ =
      [device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];

  if (!shader->pipeline_state_) {
    std::cerr << "Failed to create pipeline state: " <<
        [[error localizedDescription] UTF8String] << std::endl;
    return nullptr;
  }

  // Create depth stencil state
  MTLDepthStencilDescriptor *depthDesc =
      [[MTLDepthStencilDescriptor alloc] init];
  depthDesc.depthCompareFunction = MTLCompareFunctionLess;
  depthDesc.depthWriteEnabled = YES;

  shader->depth_stencil_state_ =
      [device newDepthStencilStateWithDescriptor:depthDesc];

  // Create uniform buffers
  shader->uniform_buffers_["uniforms"].buffer =
      [device newBufferWithLength:sizeof(Uniforms)
                          options:MTLResourceCPUCacheModeWriteCombined];
  shader->uniform_buffers_["uniforms"].size = sizeof(Uniforms);

  return shader;
}

MetalShader::~MetalShader() {
  // ARC will handle cleanup
}

void MetalShader::bind(id<MTLRenderCommandEncoder> encoder) {
  [encoder setRenderPipelineState:pipeline_state_];
  [encoder setDepthStencilState:depth_stencil_state_];

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
      memcpy(&uniforms->model, value, sizeof(float) * 16);
    } else if (name == "view") {
      memcpy(&uniforms->view, value, sizeof(float) * 16);
    } else if (name == "projection") {
      memcpy(&uniforms->projection, value, sizeof(float) * 16);
    }
  }
}

void MetalShader::set_vec3(const std::string &name, const Vec3 &value) {
  auto it = uniform_buffers_.find("uniforms");
  if (it != uniform_buffers_.end()) {
    Uniforms *uniforms = (Uniforms *)it->second.buffer.contents;
    if (name == "lightPos") {
      uniforms->lightPos = simd_make_float3(value.x, value.y, value.z);
    } else if (name == "viewPos") {
      uniforms->viewPos = simd_make_float3(value.x, value.y, value.z);
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
// Metal Mesh Implementation
// ============================================================================

std::unique_ptr<MetalMesh>
MetalMesh::create(id<MTLDevice> device, const std::vector<Vertex> &vertices,
                  const std::vector<uint32_t> &indices) {
  auto mesh = std::unique_ptr<MetalMesh>(new MetalMesh());
  mesh->device_ = device;
  mesh->vertex_count_ = vertices.size();
  mesh->index_count_ = indices.size();

  // Create vertex buffer
  mesh->vertex_buffer_ =
      [device newBufferWithBytes:vertices.data()
                          length:vertices.size() * sizeof(Vertex)
                         options:MTLResourceCPUCacheModeWriteCombined];

  // Create index buffer
  mesh->index_buffer_ =
      [device newBufferWithBytes:indices.data()
                          length:indices.size() * sizeof(uint32_t)
                         options:MTLResourceCPUCacheModeWriteCombined];

  return mesh;
}

MetalMesh::~MetalMesh() {
  // ARC will handle cleanup
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
// Metal Texture Implementation
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

  // Upload texture data
  if (data) {
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture->texture_ replaceRegion:region
                         mipmapLevel:0
                           withBytes:data
                         bytesPerRow:width * 4];

    // Generate mipmaps
    id<MTLCommandQueue> commandQueue = [device newCommandQueue];
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
    [blitEncoder generateMipmapsForTexture:texture->texture_];
    [blitEncoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
  }

  // Create sampler
  MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
  samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
  samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
  samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
  samplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
  samplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;

  texture->sampler_ = [device newSamplerStateWithDescriptor:samplerDesc];

  return texture;
}

MetalTexture::~MetalTexture() {
  // ARC will handle cleanup
}

void MetalTexture::bind(id<MTLRenderCommandEncoder> encoder, int slot) {
  [encoder setFragmentTexture:texture_ atIndex:slot];
  [encoder setFragmentSamplerState:sampler_ atIndex:slot];
}

// ============================================================================
// Metal Texture Array Implementation
// ============================================================================

std::unique_ptr<MetalTextureArray>
MetalTextureArray::create(id<MTLDevice> device, int width, int height,
                          int layers) {
  auto textureArray =
      std::unique_ptr<MetalTextureArray>(new MetalTextureArray());
  textureArray->device_ = device;
  textureArray->width_ = width;
  textureArray->height_ = height;
  textureArray->layers_ = layers;

  // Create texture array descriptor
  MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
  desc.textureType = MTLTextureType2DArray;
  desc.pixelFormat = MTLPixelFormatRGBA8Unorm;
  desc.width = width;
  desc.height = height;
  desc.arrayLength = layers;
  desc.mipmapLevelCount = 1;

  textureArray->texture_ = [device newTextureWithDescriptor:desc];

  // Create sampler
  MTLSamplerDescriptor *samplerDesc = [[MTLSamplerDescriptor alloc] init];
  samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
  samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
  samplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
  samplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;

  textureArray->sampler_ = [device newSamplerStateWithDescriptor:samplerDesc];

  return textureArray;
}

void MetalTextureArray::set_layer(int layer, const uint8_t *data) {
  if (layer < 0 || layer >= layers_ || !data)
    return;

  MTLRegion region = MTLRegionMake2D(0, 0, width_, height_);
  [texture_ replaceRegion:region
              mipmapLevel:0
                    slice:layer
                withBytes:data
              bytesPerRow:width_ * 4
            bytesPerImage:0];
}

void MetalTextureArray::bind(id<MTLRenderCommandEncoder> encoder, int slot) {
  [encoder setFragmentTexture:texture_ atIndex:slot];
  [encoder setFragmentSamplerState:sampler_ atIndex:slot];
}

// ============================================================================
// Metal Backend Implementation
// ============================================================================

std::unique_ptr<MetalBackend> MetalBackend::create(GLFWwindow *window) {
  auto backend = std::unique_ptr<MetalBackend>(new MetalBackend());

  if (!backend->initialize(window)) {
    return nullptr;
  }

  return backend;
}

bool MetalBackend::initialize(GLFWwindow *window) {
  // Get the native window
  NSWindow *nsWindow = glfwGetCocoaWindow(window);
  if (!nsWindow) {
    std::cerr << "Failed to get Cocoa window" << std::endl;
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
  MTLTextureDescriptor *depthDesc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                   width:viewport_width_
                                  height:viewport_height_
                               mipmapped:NO];
  depthDesc.usage = MTLTextureUsageRenderTarget;
  depthDesc.storageMode = MTLStorageModePrivate;

  depth_texture_ = [device_ newTextureWithDescriptor:depthDesc];

  // Create default shaders
  create_default_shaders();

  return true;
}

void MetalBackend::create_default_shaders() {
  // These would be created from the Metal shader library
  default_shader_ = create_shader("vertex_main", "fragment_main");
  instanced_shader_ = create_shader("vertex_instanced", "fragment_instanced");
}

MetalBackend::~MetalBackend() {
  // ARC will handle cleanup
}

void MetalBackend::begin_frame(const Color &clear_color) {
  // Get next drawable
  current_drawable_ = [metal_layer_ nextDrawable];

  // Create command buffer
  command_buffer_ = [command_queue_ commandBuffer];

  // Setup render pass descriptor
  setup_render_pass_descriptor();

  // Set clear color
  render_pass_descriptor_.colorAttachments[0].clearColor = MTLClearColorMake(
      clear_color.r, clear_color.g, clear_color.b, clear_color.a);

  // Create render encoder
  render_encoder_ = [command_buffer_
      renderCommandEncoderWithDescriptor:render_pass_descriptor_];

  // Set viewport
  MTLViewport viewport;
  viewport.originX = 0;
  viewport.originY = 0;
  viewport.width = viewport_width_;
  viewport.height = viewport_height_;
  viewport.znear = 0.0;
  viewport.zfar = 1.0;

  [render_encoder_ setViewport:viewport];
}

void MetalBackend::end_frame() {
  // End encoding
  [render_encoder_ endEncoding];

  // Present drawable
  [command_buffer_ presentDrawable:current_drawable_];

  // Commit command buffer
  [command_buffer_ commit];

  // Reset for next frame
  render_encoder_ = nil;
  command_buffer_ = nil;
  current_drawable_ = nil;
}

void MetalBackend::setup_render_pass_descriptor() {
  if (!render_pass_descriptor_) {
    render_pass_descriptor_ = [[MTLRenderPassDescriptor alloc] init];
  }

  // Color attachment
  render_pass_descriptor_.colorAttachments[0].texture =
      current_drawable_.texture;
  render_pass_descriptor_.colorAttachments[0].loadAction = MTLLoadActionClear;
  render_pass_descriptor_.colorAttachments[0].storeAction = MTLStoreActionStore;

  // Depth attachment
  render_pass_descriptor_.depthAttachment.texture = depth_texture_;
  render_pass_descriptor_.depthAttachment.loadAction = MTLLoadActionClear;
  render_pass_descriptor_.depthAttachment.storeAction = MTLStoreActionDontCare;
  render_pass_descriptor_.depthAttachment.clearDepth = 1.0;
}

std::unique_ptr<MetalShader>
MetalBackend::create_shader(const std::string &vertex_src,
                            const std::string &fragment_src) {
  return MetalShader::create(device_, vertex_src, fragment_src);
}

std::unique_ptr<MetalMesh>
MetalBackend::create_mesh(const std::vector<Vertex> &vertices,
                          const std::vector<uint32_t> &indices) {
  return MetalMesh::create(device_, vertices, indices);
}

std::unique_ptr<MetalTexture>
MetalBackend::create_texture(int width, int height, const uint8_t *data) {
  return MetalTexture::create(device_, width, height, data);
}

std::unique_ptr<MetalTextureArray>
MetalBackend::create_texture_array(int width, int height, int layers) {
  return MetalTextureArray::create(device_, width, height, layers);
}

void MetalBackend::draw_mesh(const MetalMesh &mesh, const Vec3 &position,
                             const Vec3 &rotation, const Vec3 &scale,
                             MetalShader *shader, const Material &material) {
  if (!shader || !render_encoder_)
    return;

  // Bind shader
  shader->bind(render_encoder_);

  // Set transform matrices
  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, glm::vec3(position.x, position.y, position.z));
  model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
  model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
  model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));
  model = glm::scale(model, glm::vec3(scale.x, scale.y, scale.z));

  shader->set_mat4("model", glm::value_ptr(model));

  // Draw mesh
  mesh.draw(render_encoder_);
}

} // namespace pixel::renderer3d::metal

#endif // __APPLE__
