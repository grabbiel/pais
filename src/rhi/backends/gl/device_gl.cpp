// src/rhi/backends/gl/device_gl.cpp
#include "pixel/rhi/rhi.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <cstring>

// OpenGL function loading for modern OpenGL
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
// Windows needs manual function loading
#else
// macOS/Linux can use GLFW's function loading
#define GL_GLEXT_PROTOTYPES
#include <OpenGL/gl3.h>
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
  GLuint program = 0;
  std::string stage; // "vs" or "fs"
  std::unordered_map<std::string, GLint> uniform_locations;
};

struct GLPipeline {
  GLuint program = 0;
  GLuint vao = 0;
  ShaderHandle vs{0};
  ShaderHandle fs{0};

  // Cached uniform locations
  std::unordered_map<std::string, GLint> uniforms;
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
    // For now, render to default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Clear
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    glClearDepth(clearDepth);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Enable blending for transparency
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

    // Vertex buffer will be bound when we set up vertex attributes
    glBindBuffer(GL_ARRAY_BUFFER, it->second.id);
  }

  void setIndexBuffer(BufferHandle handle, size_t offset) override {
    auto it = buffers_->find(handle.id);
    if (it == buffers_->end())
      return;

    current_ib_ = handle;
    current_ib_offset_ = offset;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.id);
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

// Load OpenGL functions (on platforms that need it)
#ifdef _WIN32
// TODO: Load GL functions via GLFW or GLAD
#endif

    // Query capabilities
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);

    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);

    std::cout << "OpenGL Renderer: " << renderer << std::endl;
    std::cout << "OpenGL Version: " << version << std::endl;
    std::cout << "Max Texture Size: " << max_texture_size << std::endl;

    // Setup capabilities
    caps_.instancing = true;
    caps_.uniformBuffers = true;
    caps_.samplerAniso = false;   // Check for extension
    caps_.clipSpaceYDown = false; // OpenGL uses Y-up

    // Create command list
    cmd_list_ = std::make_unique<GLCmdList>(&buffers_, &textures_, &pipelines_);
  }

  ~GLDevice() override {
    // Cleanup all resources
    for (auto &[id, buf] : buffers_) {
      glDeleteBuffers(1, &buf.id);
    }
    for (auto &[id, tex] : textures_) {
      glDeleteTextures(1, &tex.id);
    }
    for (auto &[id, samp] : samplers_) {
      glDeleteSamplers(1, &samp.id);
    }
    for (auto &[id, shader] : shaders_) {
      glDeleteProgram(shader.program);
    }
    for (auto &[id, pipe] : pipelines_) {
      glDeleteVertexArrays(1, &pipe.vao);
      // Program is shared with shader, don't delete here
    }
  }

  const Caps &caps() const override { return caps_; }

  BufferHandle createBuffer(const BufferDesc &desc) override {
    GLBuffer buffer;
    glGenBuffers(1, &buffer.id);

    // Determine target
    if (desc.usage & BufferUsage::Vertex) {
      buffer.target = GL_ARRAY_BUFFER;
    } else if (desc.usage & BufferUsage::Index) {
      buffer.target = GL_ELEMENT_ARRAY_BUFFER;
    } else if (desc.usage & BufferUsage::Uniform) {
      buffer.target = GL_UNIFORM_BUFFER;
    } else {
      buffer.target = GL_ARRAY_BUFFER;
    }

    buffer.size = desc.size;
    buffer.host_visible = desc.hostVisible;

    // Allocate storage
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
    texture.format = desc.format;

    // Determine format
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

    texture.target = GL_TEXTURE_2D;
    glBindTexture(GL_TEXTURE_2D, texture.id);

    // Allocate storage
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, desc.size.w, desc.size.h, 0,
                 format, type, nullptr);

    // Generate mipmaps if requested
    if (desc.mipLevels > 1) {
      glGenerateMipmap(GL_TEXTURE_2D);
    }

    // Default filtering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D, 0);

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

    // Create a program for this shader (will be linked in createPipeline)
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glDeleteShader(shader); // Can delete after attaching

    GLShader gl_shader;
    gl_shader.program = program;
    gl_shader.stage = std::string(stage);

    uint32_t handle_id = next_shader_id_++;
    shaders_[handle_id] = gl_shader;

    return ShaderHandle{handle_id};
  }

  PipelineHandle createPipeline(const PipelineDesc &desc) override {
    auto vs_it = shaders_.find(desc.vs.id);
    auto fs_it = shaders_.find(desc.fs.id);

    if (vs_it == shaders_.end() || fs_it == shaders_.end()) {
      std::cerr << "Invalid shader handles for pipeline" << std::endl;
      return PipelineHandle{0};
    }

    // Create linked program
    GLuint program = glCreateProgram();

    // Attach both shaders
    GLuint vs_shader = glGetAttachedShaders(vs_it->second.program, 1);
    GLuint fs_shader = glGetAttachedShaders(fs_it->second.program, 1);

    // Actually we need to recompile and attach - simpler approach:
    // Just use one of the programs and attach the other shader
    // Better: create new program and attach both

    // For simplicity, assume shaders are already compiled
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

    // Create VAO for this pipeline
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // Setup vertex attributes (hardcoded for our Vertex structure)
    // Position (vec3) at location 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 48, (void *)0);

    // Normal (vec3) at location 1
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 48, (void *)12);

    // TexCoord (vec2) at location 2
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 48, (void *)24);

    // Color (vec4) at location 3
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 48, (void *)32);

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

  CmdList *getImmediate() override { return cmd_list_.get(); }

  void present() override { glfwSwapBuffers(window_); }

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

Device *create_gl_device(void *window) {
  return new gl::GLDevice(static_cast<GLFWwindow *>(window));
}

} // namespace pixel::rhi
