// src/rhi/backends/gl/resources_gl.cpp
#include "pixel/rhi/backends/gl/device_gl.hpp"

#include <GLFW/glfw3.h>
#include <iostream>
#include <string>
#include <vector>
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

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

BufferHandle GLDevice::createBuffer(const BufferDesc &desc) {
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

TextureHandle GLDevice::createTexture(const TextureDesc &desc) {
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
  case Format::D32F:
    internal_format = GL_DEPTH_COMPONENT32F;
    format = GL_DEPTH_COMPONENT;
    type = GL_FLOAT;
    break;
  default:
    internal_format = GL_RGBA8;
    format = GL_RGBA;
    type = GL_UNSIGNED_BYTE;
  }

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

    if (desc.format == Format::D24S8 || desc.format == Format::D32F) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
      float border_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);
    } else {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
  }

  uint32_t handle_id = next_texture_id_++;
  textures_[handle_id] = texture;
  return TextureHandle{handle_id};
}

SamplerHandle GLDevice::createSampler(const SamplerDesc &desc) {
  GLSampler sampler;
  glGenSamplers(1, &sampler.id);

  auto to_gl_filter = [](FilterMode mode) {
    return mode == FilterMode::Linear ? GL_LINEAR : GL_NEAREST;
  };

  auto to_gl_wrap = [](AddressMode mode) {
    switch (mode) {
    case AddressMode::Repeat:
      return GL_REPEAT;
    case AddressMode::ClampToEdge:
      return GL_CLAMP_TO_EDGE;
    case AddressMode::ClampToBorder:
    default:
      return GL_CLAMP_TO_BORDER;
    }
  };

  glSamplerParameteri(sampler.id, GL_TEXTURE_MIN_FILTER,
                      to_gl_filter(desc.minFilter));
  glSamplerParameteri(sampler.id, GL_TEXTURE_MAG_FILTER,
                      to_gl_filter(desc.magFilter));
  glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_S,
                      to_gl_wrap(desc.addressU));
  glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_T,
                      to_gl_wrap(desc.addressV));
  glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_R,
                      to_gl_wrap(desc.addressW));

  if (caps_.samplerAniso && (desc.aniso || desc.maxAnisotropy > 1.0f)) {
    float aniso = desc.maxAnisotropy;
    if (aniso < 1.0f)
      aniso = 1.0f;
    glSamplerParameterf(sampler.id, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
  }

  glSamplerParameterfv(sampler.id, GL_TEXTURE_BORDER_COLOR, desc.borderColor);

  if (desc.mipLodBias != 0.0f) {
    glSamplerParameterf(sampler.id, GL_TEXTURE_LOD_BIAS, desc.mipLodBias);
  }

  if (caps_.samplerCompare) {
    if (desc.compareEnable) {
      glSamplerParameteri(sampler.id, GL_TEXTURE_COMPARE_MODE,
                          GL_COMPARE_REF_TO_TEXTURE);
      glSamplerParameteri(sampler.id, GL_TEXTURE_COMPARE_FUNC,
                          to_gl_compare(desc.compareOp));
    } else {
      glSamplerParameteri(sampler.id, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    }
  }

  uint32_t handle_id = next_sampler_id_++;
  samplers_[handle_id] = sampler;

  return SamplerHandle{handle_id};
}

ShaderHandle GLDevice::createShader(std::string_view stage,
                                    std::span<const uint8_t> bytes) {
  std::string source(reinterpret_cast<const char *>(bytes.data()),
                     bytes.size());

  GLenum shader_type;
  if (stage == "vs" || stage.rfind("vs_", 0) == 0) {
    shader_type = GL_VERTEX_SHADER;
  } else if (stage == "fs" || stage.rfind("fs_", 0) == 0) {
    shader_type = GL_FRAGMENT_SHADER;
  } else if (stage == "cs" || stage.rfind("cs_", 0) == 0) {
    shader_type = GL_COMPUTE_SHADER;
  } else {
    std::cerr << "Unknown shader stage: " << stage << std::endl;
    return ShaderHandle{0};
  }

  GLuint shader = glCreateShader(shader_type);
  const char *src_ptr = source.c_str();
  glShaderSource(shader, 1, &src_ptr, nullptr);
  glCompileShader(shader);

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

PipelineHandle GLDevice::createPipeline(const PipelineDesc &desc) {
  GLuint program = glCreateProgram();

  if (desc.cs.id != 0) {
    auto cs_it = shaders_.find(desc.cs.id);
    if (cs_it == shaders_.end()) {
      std::cerr << "Invalid compute shader handle for pipeline" << std::endl;
      return PipelineHandle{0};
    }

    glAttachShader(program, cs_it->second.shader_id);
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

    glDetachShader(program, cs_it->second.shader_id);

    GLPipeline pipeline;
    pipeline.program = program;
    pipeline.vao = 0;
    pipeline.cs = desc.cs;

    uint32_t handle_id = next_pipeline_id_++;
    pipelines_[handle_id] = pipeline;

    return PipelineHandle{handle_id};
  }

  auto vs_it = shaders_.find(desc.vs.id);
  auto fs_it = shaders_.find(desc.fs.id);

  if (vs_it == shaders_.end() || fs_it == shaders_.end()) {
    std::cerr << "Invalid shader handles for pipeline" << std::endl;
    return PipelineHandle{0};
  }

  glAttachShader(program, vs_it->second.shader_id);
  glAttachShader(program, fs_it->second.shader_id);

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

  glDetachShader(program, vs_it->second.shader_id);
  glDetachShader(program, fs_it->second.shader_id);

  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
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

FramebufferHandle GLDevice::createFramebuffer(const FramebufferDesc &desc) {
  if (desc.colorAttachmentCount > kMaxColorAttachments) {
    std::cerr << "OpenGL framebuffer creation exceeded attachment limit"
              << std::endl;
    return FramebufferHandle{0};
  }

  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);

  std::vector<GLenum> draw_buffers;
  draw_buffers.reserve(desc.colorAttachmentCount);

  int width = 0;
  int height = 0;

  for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
    const auto &attachment = desc.colorAttachments[i];
    if (attachment.texture.id == 0) {
      std::cerr << "Framebuffer color attachment " << i
                << " references swapchain texture" << std::endl;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDeleteFramebuffers(1, &fbo);
      return FramebufferHandle{0};
    }

    auto tex_it = textures_.find(attachment.texture.id);
    if (tex_it == textures_.end()) {
      std::cerr << "Framebuffer color attachment " << i
                << " uses invalid texture handle" << std::endl;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDeleteFramebuffers(1, &fbo);
      return FramebufferHandle{0};
    }

    const GLTexture &tex = tex_it->second;
    GLenum attachment_point = GL_COLOR_ATTACHMENT0 + i;

    if (tex.target == GL_TEXTURE_2D) {
      glFramebufferTexture2D(GL_FRAMEBUFFER, attachment_point, tex.target,
                             tex.id, attachment.mipLevel);
    } else if (tex.target == GL_TEXTURE_2D_ARRAY) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment_point, tex.id,
                                attachment.mipLevel, attachment.arraySlice);
    } else {
      glFramebufferTexture(GL_FRAMEBUFFER, attachment_point, tex.id,
                           attachment.mipLevel);
    }

    draw_buffers.push_back(attachment_point);

    if (width == 0 && height == 0) {
      width = tex.width;
      height = tex.height;
    } else if (width != tex.width || height != tex.height) {
      std::cerr << "Framebuffer color attachments must have matching dimensions"
                << std::endl;
    }
  }

  if (desc.colorAttachmentCount == 0) {
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
  } else if (!draw_buffers.empty()) {
    glDrawBuffers(static_cast<GLsizei>(draw_buffers.size()),
                  draw_buffers.data());
  } else {
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
  }

  if (desc.hasDepthAttachment) {
    const auto &depth = desc.depthAttachment;
    if (depth.texture.id == 0) {
      std::cerr << "Framebuffer depth attachment references swapchain texture"
                << std::endl;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDeleteFramebuffers(1, &fbo);
      return FramebufferHandle{0};
    }

    auto tex_it = textures_.find(depth.texture.id);
    if (tex_it == textures_.end()) {
      std::cerr << "Framebuffer depth attachment uses invalid texture handle"
                << std::endl;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDeleteFramebuffers(1, &fbo);
      return FramebufferHandle{0};
    }

    const GLTexture &tex = tex_it->second;
    GLenum attachment_point =
        depth.hasStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;

    if (tex.target == GL_TEXTURE_2D) {
      glFramebufferTexture2D(GL_FRAMEBUFFER, attachment_point, tex.target,
                             tex.id, depth.mipLevel);
    } else if (tex.target == GL_TEXTURE_2D_ARRAY) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment_point, tex.id,
                                depth.mipLevel, depth.arraySlice);
    } else {
      glFramebufferTexture(GL_FRAMEBUFFER, attachment_point, tex.id,
                           depth.mipLevel);
    }

    if (width == 0 && height == 0) {
      width = tex.width;
      height = tex.height;
    }
  }

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "OpenGL framebuffer incomplete: 0x" << std::hex << status
              << std::dec << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return FramebufferHandle{0};
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  GLFramebuffer framebuffer;
  framebuffer.id = fbo;
  framebuffer.desc = desc;
  framebuffer.width = width;
  framebuffer.height = height;
  framebuffer.draw_buffers = std::move(draw_buffers);

  uint32_t handle_id = next_framebuffer_id_++;
  framebuffers_[handle_id] = std::move(framebuffer);
  return FramebufferHandle{handle_id};
}

QueryHandle GLDevice::createQuery(QueryType type) {
  GLQueryObject query;
  query.type = type;
  query.query = std::make_unique<GLQuery>();
  if (!query.query)
    return QueryHandle{0};
  uint32_t id = next_query_id_++;
  queries_[id] = std::move(query);
  return QueryHandle{id};
}

void GLDevice::destroyQuery(QueryHandle handle) {
  auto it = queries_.find(handle.id);
  if (it == queries_.end())
    return;
  it->second.query.reset();
  queries_.erase(it);
}

bool GLDevice::getQueryResult(QueryHandle handle, uint64_t &result, bool wait) {
  auto it = queries_.find(handle.id);
  if (it == queries_.end() || !it->second.query)
    return false;
  if (!wait && !it->second.query->is_result_available())
    return false;
  result = it->second.query->get_result();
  return true;
}

FenceHandle GLDevice::createFence(bool signaled) {
  GLFence fence;
  fence.signaled = signaled;
  fence.sync = nullptr;
  uint32_t id = next_fence_id_++;
  fences_[id] = fence;
  return FenceHandle{id};
}

void GLDevice::destroyFence(FenceHandle handle) {
  auto it = fences_.find(handle.id);
  if (it == fences_.end())
    return;
  if (it->second.sync) {
    glDeleteSync(it->second.sync);
    it->second.sync = nullptr;
  }
  fences_.erase(it);
}

void GLDevice::waitFence(FenceHandle handle, uint64_t timeout_ns) {
  auto it = fences_.find(handle.id);
  if (it == fences_.end())
    return;
  if (it->second.signaled)
    return;
  if (!it->second.sync)
    return;
  GLuint64 timeout = timeout_ns == ~0ull ? GL_TIMEOUT_IGNORED : timeout_ns;
  GLenum status =
      glClientWaitSync(it->second.sync, GL_SYNC_FLUSH_COMMANDS_BIT, timeout);
  if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
    glDeleteSync(it->second.sync);
    it->second.sync = nullptr;
    it->second.signaled = true;
  }
}

void GLDevice::resetFence(FenceHandle handle) {
  auto it = fences_.find(handle.id);
  if (it == fences_.end())
    return;
  if (it->second.sync) {
    glDeleteSync(it->second.sync);
    it->second.sync = nullptr;
  }
  it->second.signaled = false;
}

void GLDevice::readBuffer(BufferHandle handle, void *dst, size_t size,
                          size_t offset) {
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

} // namespace pixel::rhi::gl
