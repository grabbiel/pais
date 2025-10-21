// src/rhi/backends/gl/device_gl.cpp
#include "pixel/rhi/rhi.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <cstring>

struct GLFWwindow;

// OpenGL function loading for modern OpenGL
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#else
#define GL_GLEXT_PROTOTYPES
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#endif
namespace pixel::rhi::gl {

// ============================================================================
// OpenGL Resource Storage
// ============================================================================

struct GLBuffer {
  GLuint id = 0;
  GLenum target = GL_ARRAY_BUFFER;
  size_t size = 0;
  bool host_visible = false;
};

struct GLTexture {
  GLuint id = 0;
  GLenum target = GL_TEXTURE_2D;
  int width = 0;
  int height = 0;
  int layers = 1;
  Format format = Format::Unknown;
};

struct GLSampler {
  GLuint id = 0;
};

struct GLShader {
  GLuint shader_id = 0; // Individual shader object
  GLenum shader_type = 0;
  std::string stage;
};

struct GLPipeline {
  GLuint program = 0;
  GLuint vao = 0;
  ShaderHandle vs{0};
  ShaderHandle fs{0};
  ShaderHandle cs{0};
  std::unordered_map<std::string, GLint> uniform_locations;
};

// ============================================================================
// OpenGL Command List
// ============================================================================

class GLCmdList : public CmdList {
public:
  GLCmdList(std::unordered_map<uint32_t, GLBuffer> *buffers,
            std::unordered_map<uint32_t, GLTexture> *textures,
            std::unordered_map<uint32_t, GLPipeline> *pipelines)
      : buffers_(buffers), textures_(textures), pipelines_(pipelines) {}

  void begin() override { recording_ = true; }

  void beginRender(TextureHandle rtColor, TextureHandle rtDepth, float clear[4],
                   float clearDepth, uint8_t clearStencil) override {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    glClearDepth(clearDepth);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  void setPipeline(PipelineHandle handle) override {
    auto it = pipelines_->find(handle.id);
    if (it == pipelines_->end())
      return;

    current_pipeline_ = handle;
    const GLPipeline &pipeline = it->second;

    glUseProgram(pipeline.program);
    glBindVertexArray(pipeline.vao);
  }

  void setVertexBuffer(BufferHandle handle, size_t offset) override {
    auto it = buffers_->find(handle.id);
    if (it == buffers_->end())
      return;

    current_vb_ = handle;
    current_vb_offset_ = offset;

    auto pit = pipelines_->find(current_pipeline_.id);
    if (pit == pipelines_->end())
      return;

    glBindVertexArray(pit->second.vao);
    glBindBuffer(GL_ARRAY_BUFFER, it->second.id);

    // Vertex layout for renderer3d::Vertex (48 bytes total)
    const GLsizei stride = 48;

    // Position (vec3, offset 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);

    // Normal (vec3, offset 12)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)12);

    // TexCoord (vec2, offset 24)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)24);

    // Color (vec4, offset 32)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)32);
  }

  void setIndexBuffer(BufferHandle handle, size_t offset) override {
    auto it = buffers_->find(handle.id);
    if (it == buffers_->end())
      return;

    current_ib_ = handle;
    current_ib_offset_ = offset;

    auto pit = pipelines_->find(current_pipeline_.id);
    if (pit != pipelines_->end()) {
      glBindVertexArray(pit->second.vao);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.id);
  }

  void setInstanceBuffer(BufferHandle handle, size_t stride,
                        size_t offset) override {
    auto it = buffers_->find(handle.id);
    if (it == buffers_->end())
      return;

    auto pit = pipelines_->find(current_pipeline_.id);
    if (pit == pipelines_->end())
      return;

    glBindVertexArray(pit->second.vao);
    glBindBuffer(GL_ARRAY_BUFFER, it->second.id);

    // InstanceData layout: Vec3 position, Vec3 rotation, Vec3 scale, Color color,
    // float texture_index, float culling_radius, float lod_transition_alpha, float padding
    // Total stride should match sizeof(InstanceData)
    const GLsizei instance_stride = stride;

    // iPosition (vec3, location 4)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, instance_stride,
                         (void *)(offset + 0));
    glVertexAttribDivisor(4, 1);

    // iRotation (vec3, location 5)
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, instance_stride,
                         (void *)(offset + 12));
    glVertexAttribDivisor(5, 1);

    // iScale (vec3, location 6)
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, instance_stride,
                         (void *)(offset + 24));
    glVertexAttribDivisor(6, 1);

    // iColor (vec4, location 7)
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, instance_stride,
                         (void *)(offset + 36));
    glVertexAttribDivisor(7, 1);

    // iTextureIndex (float, location 8)
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, instance_stride,
                         (void *)(offset + 52));
    glVertexAttribDivisor(8, 1);

    // iLODAlpha (float, location 9) - skip culling_radius at offset 56
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, instance_stride,
                         (void *)(offset + 60));
    glVertexAttribDivisor(9, 1);
  }

  void setUniformMat4(const char *name, const float *mat4x4) override {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) {
      glUniformMatrix4fv(loc, 1, GL_FALSE, mat4x4);
    }
  }

  void setUniformVec3(const char *name, const float *vec3) override {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) {
      glUniform3fv(loc, 1, vec3);
    }
  }

  void setUniformVec4(const char *name, const float *vec4) override {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) {
      glUniform4fv(loc, 1, vec4);
    }
  }

  void setUniformInt(const char *name, int value) override {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) {
      glUniform1i(loc, value);
    }
  }

  void setUniformFloat(const char *name, float value) override {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) {
      glUniform1f(loc, value);
    }
  }

  void setUniformBuffer(uint32_t binding, BufferHandle buffer, size_t offset,
                        size_t size) override {
    auto it = buffers_->find(buffer.id);
    if (it == buffers_->end())
      return;

    const GLBuffer &buf = it->second;

    // Bind uniform buffer to binding point
    if (size > 0) {
      // Bind a range of the buffer
      glBindBufferRange(GL_UNIFORM_BUFFER, binding, buf.id, offset, size);
    } else {
      // Bind the entire buffer
      glBindBufferBase(GL_UNIFORM_BUFFER, binding, buf.id);
    }
  }

  void setTexture(const char *name, TextureHandle texture,
                  uint32_t slot) override {
    auto it = textures_->find(texture.id);
    if (it == textures_->end())
      return;

    const GLTexture &tex = it->second;

    // Activate texture unit
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(tex.target, tex.id);

    // Set sampler uniform
    GLint loc = getUniformLocation(name);
    if (loc >= 0) {
      glUniform1i(loc, slot);
    }
  }

  void copyToTexture(TextureHandle texture, uint32_t mipLevel,
                     std::span<const std::byte> data) override {
    auto it = textures_->find(texture.id);
    if (it == textures_->end())
      return;

    GLTexture &tex = it->second;

    GLenum format, type;
    switch (tex.format) {
    case Format::RGBA8:
      format = GL_RGBA;
      type = GL_UNSIGNED_BYTE;
      break;
    case Format::BGRA8:
      format = GL_BGRA;
      type = GL_UNSIGNED_BYTE;
      break;
    case Format::R8:
      format = GL_RED;
      type = GL_UNSIGNED_BYTE;
      break;
    default:
      format = GL_RGBA;
      type = GL_UNSIGNED_BYTE;
    }

    glBindTexture(tex.target, tex.id);
    glTexSubImage2D(tex.target, mipLevel, 0, 0, tex.width, tex.height, format,
                    type, data.data());
    glBindTexture(tex.target, 0);
  }

  void copyToTextureLayer(TextureHandle texture, uint32_t layer,
                          uint32_t mipLevel,
                          std::span<const std::byte> data) override {
    auto it = textures_->find(texture.id);
    if (it == textures_->end())
      return;

    GLTexture &tex = it->second;

    // Validate layer index
    if (layer >= static_cast<uint32_t>(tex.layers)) {
      std::cerr << "Invalid layer index: " << layer << " (max: " << tex.layers - 1
                << ")" << std::endl;
      return;
    }

    GLenum format, type;
    switch (tex.format) {
    case Format::RGBA8:
      format = GL_RGBA;
      type = GL_UNSIGNED_BYTE;
      break;
    case Format::BGRA8:
      format = GL_BGRA;
      type = GL_UNSIGNED_BYTE;
      break;
    case Format::R8:
      format = GL_RED;
      type = GL_UNSIGNED_BYTE;
      break;
    default:
      format = GL_RGBA;
      type = GL_UNSIGNED_BYTE;
    }

    glBindTexture(tex.target, tex.id);

    if (tex.target == GL_TEXTURE_2D_ARRAY) {
      // For texture arrays, use glTexSubImage3D with z offset = layer
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, mipLevel, 0, 0, layer, tex.width,
                      tex.height, 1, format, type, data.data());
    } else {
      // For regular 2D textures, layer must be 0
      if (layer == 0) {
        glTexSubImage2D(tex.target, mipLevel, 0, 0, tex.width, tex.height,
                        format, type, data.data());
      } else {
        std::cerr << "Cannot upload to layer " << layer
                  << " of a non-array texture" << std::endl;
      }
    }

    glBindTexture(tex.target, 0);
  }

  // Compute shader support
  void setComputePipeline(PipelineHandle handle) override {
    auto it = pipelines_->find(handle.id);
    if (it == pipelines_->end())
      return;

    current_pipeline_ = handle;
    const GLPipeline &pipeline = it->second;

    glUseProgram(pipeline.program);
  }

  void setStorageBuffer(uint32_t binding, BufferHandle buffer, size_t offset,
                       size_t size) override {
    auto it = buffers_->find(buffer.id);
    if (it == buffers_->end())
      return;

    const GLBuffer &buf = it->second;

    // Bind shader storage buffer to binding point
    if (size > 0) {
      glBindBufferRange(GL_SHADER_STORAGE_BUFFER, binding, buf.id, offset, size);
    } else {
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buf.id);
    }
  }

  void dispatch(uint32_t groupCountX, uint32_t groupCountY,
               uint32_t groupCountZ) override {
    glDispatchCompute(groupCountX, groupCountY, groupCountZ);
  }

  void memoryBarrier() override {
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  }

  void drawIndexed(uint32_t indexCount, uint32_t firstIndex,
                   uint32_t instanceCount) override {
    if (current_pipeline_.id == 0)
      return;

    size_t index_offset = current_ib_offset_ + firstIndex * sizeof(uint32_t);

    if (instanceCount > 1) {
      glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT,
                              (void *)index_offset, instanceCount);
    } else {
      glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT,
                     (void *)index_offset);
    }
  }

  void endRender() override {
    glBindVertexArray(0);
    glUseProgram(0);
  }

  void copyToBuffer(BufferHandle handle, size_t dstOff,
                    std::span<const std::byte> src) override {
    auto it = buffers_->find(handle.id);
    if (it == buffers_->end())
      return;

    GLBuffer &buffer = it->second;
    glBindBuffer(buffer.target, buffer.id);
    glBufferSubData(buffer.target, dstOff, src.size(), src.data());
    glBindBuffer(buffer.target, 0);
  }

  void end() override { recording_ = false; }

private:
  GLint getUniformLocation(const char *name) {
    auto pit = pipelines_->find(current_pipeline_.id);
    if (pit == pipelines_->end())
      return -1;

    GLPipeline &pipeline = pit->second;

    // Cache uniform locations
    std::string name_str(name);
    auto it = pipeline.uniform_locations.find(name_str);
    if (it != pipeline.uniform_locations.end()) {
      return it->second;
    }

    GLint loc = glGetUniformLocation(pipeline.program, name);
    pipeline.uniform_locations[name_str] = loc;
    return loc;
  }
  std::unordered_map<uint32_t, GLBuffer> *buffers_;
  std::unordered_map<uint32_t, GLTexture> *textures_;
  std::unordered_map<uint32_t, GLPipeline> *pipelines_;

  bool recording_ = false;
  PipelineHandle current_pipeline_{0};
  BufferHandle current_vb_{0};
  BufferHandle current_ib_{0};
  size_t current_vb_offset_ = 0;
  size_t current_ib_offset_ = 0;
};

// ============================================================================
// OpenGL Device
// ============================================================================

class GLDevice : public Device {
public:
  GLDevice(GLFWwindow *window) : window_(window) {
    glfwMakeContextCurrent(window);

    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);

    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);

    std::cout << "OpenGL Renderer: " << renderer << std::endl;
    std::cout << "OpenGL Version: " << version << std::endl;

    caps_.instancing = true;
    caps_.uniformBuffers = true;
    caps_.samplerAniso = false;
    caps_.clipSpaceYDown = false;

    cmd_list_ = std::make_unique<GLCmdList>(&buffers_, &textures_, &pipelines_);
  }

  ~GLDevice() override {
    for (auto &[id, buf] : buffers_)
      glDeleteBuffers(1, &buf.id);
    for (auto &[id, tex] : textures_)
      glDeleteTextures(1, &tex.id);
    for (auto &[id, samp] : samplers_)
      glDeleteSamplers(1, &samp.id);
    for (auto &[id, shader] : shaders_) {
      if (shader.shader_id != 0)
        glDeleteShader(shader.shader_id);
    }
    for (auto &[id, pipe] : pipelines_) {
      glDeleteVertexArrays(1, &pipe.vao);
      glDeleteProgram(pipe.program);
    }
  }

  const Caps &caps() const override { return caps_; }

  BufferHandle createBuffer(const BufferDesc &desc) override {
    GLBuffer buffer;
    glGenBuffers(1, &buffer.id);

    if ((desc.usage & BufferUsage::Vertex) == BufferUsage::Vertex) {
      buffer.target = GL_ARRAY_BUFFER;
    } else if ((desc.usage & BufferUsage::Index) == BufferUsage::Index) {
      buffer.target = GL_ELEMENT_ARRAY_BUFFER;
    } else if ((desc.usage & BufferUsage::Uniform) == BufferUsage::Uniform) {
      buffer.target = GL_UNIFORM_BUFFER;
    } else if ((desc.usage & BufferUsage::Storage) == BufferUsage::Storage) {
      buffer.target = GL_SHADER_STORAGE_BUFFER;
    } else {
      buffer.target = GL_ARRAY_BUFFER;
    }

    buffer.size = desc.size;
    buffer.host_visible = desc.hostVisible;

    glBindBuffer(buffer.target, buffer.id);
    GLenum usage = desc.hostVisible ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    glBufferData(buffer.target, desc.size, nullptr, usage);
    glBindBuffer(buffer.target, 0);

    uint32_t handle_id = next_buffer_id_++;
    buffers_[handle_id] = buffer;
    return BufferHandle{handle_id};
  }

  TextureHandle createTexture(const TextureDesc &desc) override {
    GLTexture texture;
    glGenTextures(1, &texture.id);

    texture.width = desc.size.w;
    texture.height = desc.size.h;
    texture.layers = desc.layers;
    texture.format = desc.format;

    GLenum internal_format, format, type;
    switch (desc.format) {
    case Format::RGBA8:
      internal_format = GL_RGBA8;
      format = GL_RGBA;
      type = GL_UNSIGNED_BYTE;
      break;
    case Format::BGRA8:
      internal_format = GL_RGBA8;
      format = GL_BGRA;
      type = GL_UNSIGNED_BYTE;
      break;
    case Format::R8:
      internal_format = GL_R8;
      format = GL_RED;
      type = GL_UNSIGNED_BYTE;
      break;
    case Format::D24S8:
      internal_format = GL_DEPTH24_STENCIL8;
      format = GL_DEPTH_STENCIL;
      type = GL_UNSIGNED_INT_24_8;
      break;
    default:
      internal_format = GL_RGBA8;
      format = GL_RGBA;
      type = GL_UNSIGNED_BYTE;
    }

    // Choose texture target based on layer count
    if (desc.layers > 1) {
      texture.target = GL_TEXTURE_2D_ARRAY;
      glBindTexture(GL_TEXTURE_2D_ARRAY, texture.id);
      glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internal_format, desc.size.w,
                   desc.size.h, desc.layers, 0, format, type, nullptr);

      if (desc.mipLevels > 1) {
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
      }

      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

      glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    } else {
      texture.target = GL_TEXTURE_2D;
      glBindTexture(GL_TEXTURE_2D, texture.id);
      glTexImage2D(GL_TEXTURE_2D, 0, internal_format, desc.size.w, desc.size.h, 0,
                   format, type, nullptr);

      if (desc.mipLevels > 1) {
        glGenerateMipmap(GL_TEXTURE_2D);
      }

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

      glBindTexture(GL_TEXTURE_2D, 0);
    }

    uint32_t handle_id = next_texture_id_++;
    textures_[handle_id] = texture;
    return TextureHandle{handle_id};
  }

  SamplerHandle createSampler(const SamplerDesc &desc) override {
    GLSampler sampler;
    glGenSamplers(1, &sampler.id);

    GLint min_filter = desc.linear ? GL_LINEAR : GL_NEAREST;
    GLint mag_filter = desc.linear ? GL_LINEAR : GL_NEAREST;
    GLint wrap = desc.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;

    glSamplerParameteri(sampler.id, GL_TEXTURE_MIN_FILTER, min_filter);
    glSamplerParameteri(sampler.id, GL_TEXTURE_MAG_FILTER, mag_filter);
    glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_S, wrap);
    glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_T, wrap);

    uint32_t handle_id = next_sampler_id_++;
    samplers_[handle_id] = sampler;

    return SamplerHandle{handle_id};
  }

  ShaderHandle createShader(std::string_view stage,
                            std::span<const uint8_t> bytes) override {
    // Convert bytes to string (assuming it's GLSL source)
    std::string source(reinterpret_cast<const char *>(bytes.data()),
                       bytes.size());

    GLenum shader_type;
    if (stage == "vs") {
      shader_type = GL_VERTEX_SHADER;
    } else if (stage == "fs") {
      shader_type = GL_FRAGMENT_SHADER;
    } else if (stage == "cs") {
      shader_type = GL_COMPUTE_SHADER;
    } else {
      std::cerr << "Unknown shader stage: " << stage << std::endl;
      return ShaderHandle{0};
    }

    GLuint shader = glCreateShader(shader_type);
    const char *src_ptr = source.c_str();
    glShaderSource(shader, 1, &src_ptr, nullptr);
    glCompileShader(shader);

    // Check compilation
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      char log[512];
      glGetShaderInfoLog(shader, 512, nullptr, log);
      std::cerr << "Shader compilation failed (" << stage << "):\n"
                << log << std::endl;
      glDeleteShader(shader);
      return ShaderHandle{0};
    }

    GLShader gl_shader;
    gl_shader.shader_id = shader;
    gl_shader.shader_type = shader_type;
    gl_shader.stage = std::string(stage);

    uint32_t handle_id = next_shader_id_++;
    shaders_[handle_id] = gl_shader;

    return ShaderHandle{handle_id};
  }

  PipelineHandle createPipeline(const PipelineDesc &desc) override {
    // Create linked program
    GLuint program = glCreateProgram();

    // Check if this is a compute pipeline
    if (desc.cs.id != 0) {
      auto cs_it = shaders_.find(desc.cs.id);
      if (cs_it == shaders_.end()) {
        std::cerr << "Invalid compute shader handle for pipeline" << std::endl;
        return PipelineHandle{0};
      }

      // Attach compute shader
      glAttachShader(program, cs_it->second.shader_id);

      // Link the program
      glLinkProgram(program);

      GLint success;
      glGetProgramiv(program, GL_LINK_STATUS, &success);
      if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "Compute pipeline linking failed:\n" << log << std::endl;
        glDeleteProgram(program);
        return PipelineHandle{0};
      }

      // Detach shader after linking
      glDetachShader(program, cs_it->second.shader_id);

      GLPipeline pipeline;
      pipeline.program = program;
      pipeline.vao = 0;  // No VAO for compute pipelines
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

    // Attach both shader objects
    glAttachShader(program, vs_it->second.shader_id);
    glAttachShader(program, fs_it->second.shader_id);

    // Link the program
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
      char log[512];
      glGetProgramInfoLog(program, 512, nullptr, log);
      std::cerr << "Pipeline linking failed:\n" << log << std::endl;
      glDeleteProgram(program);
      return PipelineHandle{0};
    }

    // Detach shaders after linking (they're still valid, just not attached)
    glDetachShader(program, vs_it->second.shader_id);
    glDetachShader(program, fs_it->second.shader_id);

    // Create VAO for this pipeline
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // Setup vertex attributes (hardcoded for our Vertex structure)
    // Position (vec3) at location 0
    glEnableVertexAttribArray(0);

    // Normal (vec3) at location 1
    glEnableVertexAttribArray(1);

    // TexCoord (vec2) at location 2
    glEnableVertexAttribArray(2);

    // Color (vec4) at location 3
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    GLPipeline pipeline;
    pipeline.program = program;
    pipeline.vao = vao;
    pipeline.vs = desc.vs;
    pipeline.fs = desc.fs;

    uint32_t handle_id = next_pipeline_id_++;
    pipelines_[handle_id] = pipeline;

    return PipelineHandle{handle_id};
  }

  void readBuffer(BufferHandle handle, void *dst, size_t size,
                  size_t offset = 0) override {
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) {
      std::cerr << "Attempted to read from invalid buffer handle" << std::endl;
      return;
    }

    const GLBuffer &buffer = it->second;
    if (offset + size > buffer.size) {
      std::cerr << "Read range exceeds buffer bounds" << std::endl;
      return;
    }

    glBindBuffer(buffer.target, buffer.id);
    glGetBufferSubData(buffer.target, offset, size, dst);
    glBindBuffer(buffer.target, 0);
  }

  CmdList *getImmediate() override { return cmd_list_.get(); }

  void present() override {
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window_, &fbw, &fbh);
    if (fbw > 0 && fbh > 0) {
      glViewport(0, 0, fbw, fbh);
    }
    glfwSwapBuffers(window_);
  }

private:
  GLFWwindow *window_;
  Caps caps_;

  std::unique_ptr<GLCmdList> cmd_list_;

  std::unordered_map<uint32_t, GLBuffer> buffers_;
  std::unordered_map<uint32_t, GLTexture> textures_;
  std::unordered_map<uint32_t, GLSampler> samplers_;
  std::unordered_map<uint32_t, GLShader> shaders_;
  std::unordered_map<uint32_t, GLPipeline> pipelines_;

  uint32_t next_buffer_id_ = 1;
  uint32_t next_texture_id_ = 1;
  uint32_t next_sampler_id_ = 1;
  uint32_t next_shader_id_ = 1;
  uint32_t next_pipeline_id_ = 1;
};

} // namespace pixel::rhi::gl

// ============================================================================
// Factory Function
// ============================================================================

namespace pixel::rhi {

Device *create_gl_device(GLFWwindow *window) {
  return new gl::GLDevice(window);
}

} // namespace pixel::rhi
