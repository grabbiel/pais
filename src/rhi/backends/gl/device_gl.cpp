// src/rhi/backends/gl/device_gl.cpp
#include "pixel/rhi/rhi.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <memory>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif

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

namespace {

class GLQuery {
public:
  GLQuery() { glGenQueries(1, &query_id_); }

  ~GLQuery() { glDeleteQueries(1, &query_id_); }

  void begin_time_elapsed() { glBeginQuery(GL_TIME_ELAPSED, query_id_); }

  void end_time_elapsed() { glEndQuery(GL_TIME_ELAPSED); }

  bool is_result_available() {
    GLint available = 0;
    glGetQueryObjectiv(query_id_, GL_QUERY_RESULT_AVAILABLE, &available);
    return available != 0;
  }

  uint64_t get_result() {
    uint64_t elapsed_time = 0;
    glGetQueryObjectui64v(query_id_, GL_QUERY_RESULT, &elapsed_time);
    return elapsed_time;
  }

  void timestamp() { glQueryCounter(query_id_, GL_TIMESTAMP); }

private:
  GLuint query_id_{};
};

GLenum to_gl_compare(CompareOp op) {
  switch (op) {
  case CompareOp::Never:
    return GL_NEVER;
  case CompareOp::Less:
    return GL_LESS;
  case CompareOp::Equal:
    return GL_EQUAL;
  case CompareOp::LessEqual:
    return GL_LEQUAL;
  case CompareOp::Greater:
    return GL_GREATER;
  case CompareOp::NotEqual:
    return GL_NOTEQUAL;
  case CompareOp::GreaterEqual:
    return GL_GEQUAL;
  case CompareOp::Always:
  default:
    return GL_ALWAYS;
  }
}

GLenum to_gl_stencil_op(StencilOp op) {
  switch (op) {
  case StencilOp::Keep:
    return GL_KEEP;
  case StencilOp::Zero:
    return GL_ZERO;
  case StencilOp::Replace:
    return GL_REPLACE;
  case StencilOp::IncrementClamp:
    return GL_INCR;
  case StencilOp::DecrementClamp:
    return GL_DECR;
  case StencilOp::Invert:
    return GL_INVERT;
  case StencilOp::IncrementWrap:
    return GL_INCR_WRAP;
  case StencilOp::DecrementWrap:
    return GL_DECR_WRAP;
  default:
    return GL_KEEP;
  }
}

bool has_extension(const char *extension) {
  int major = 0;
  if (const char *version =
          reinterpret_cast<const char *>(glGetString(GL_VERSION))) {
    major = std::atoi(version);
  }

  if (major >= 3) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
      const char *name =
          reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
      if (name && std::strcmp(name, extension) == 0)
        return true;
    }
  } else {
    const char *extensions =
        reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (extensions && std::strstr(extensions, extension))
      return true;
  }
  return false;
}

} // namespace

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

struct GLFramebuffer {
  GLuint id = 0;
  FramebufferDesc desc{};
  int width = 0;
  int height = 0;
  std::vector<GLenum> draw_buffers;
};

struct GLQueryObject {
  QueryType type{QueryType::TimeElapsed};
  std::unique_ptr<GLQuery> query;
};

struct GLFence {
  GLsync sync{nullptr};
  bool signaled{false};
};

// ============================================================================
// OpenGL Command List
// ============================================================================


class GLCmdList : public CmdList {
public:
  GLCmdList(std::unordered_map<uint32_t, GLBuffer> *buffers,
            std::unordered_map<uint32_t, GLTexture> *textures,
            std::unordered_map<uint32_t, GLPipeline> *pipelines,
            std::unordered_map<uint32_t, GLFramebuffer> *framebuffers,
            std::unordered_map<uint32_t, GLQueryObject> *queries,
            std::unordered_map<uint32_t, GLFence> *fences,
            GLFWwindow *window)
      : buffers_(buffers), textures_(textures), pipelines_(pipelines),
        framebuffers_(framebuffers), queries_(queries), fences_(fences),
        window_(window) {}

  void begin() override {
    recording_ = true;
    depth_stencil_state_initialized_ = false;
    depth_bias_initialized_ = false;
  }

  void beginRender(const RenderPassDesc &desc) override {
    using_offscreen_fbo_ = false;
    current_fbo_owned_ = false;
    current_fbo_ = 0;

    if (desc.framebuffer.id != 0) {
      auto fb_it = framebuffers_->find(desc.framebuffer.id);
      if (fb_it == framebuffers_->end()) {
        std::cerr << "OpenGL beginRender called with invalid framebuffer handle"
                  << std::endl;
        return;
      }

      const GLFramebuffer &framebuffer = fb_it->second;
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer.id);

      if (!framebuffer.draw_buffers.empty()) {
        glDrawBuffers(static_cast<GLsizei>(framebuffer.draw_buffers.size()),
                      framebuffer.draw_buffers.data());
      } else {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
      }

      if (framebuffer.width > 0 && framebuffer.height > 0) {
        glViewport(0, 0, framebuffer.width, framebuffer.height);
      }

      using_offscreen_fbo_ = true;
      current_fbo_ = framebuffer.id;

      uint32_t attachment_count =
          std::min(desc.colorAttachmentCount, framebuffer.desc.colorAttachmentCount);
      for (uint32_t i = 0; i < attachment_count; ++i) {
        const auto &attachment = desc.colorAttachments[i];
        if (attachment.loadOp == LoadOp::Clear) {
          glClearBufferfv(GL_COLOR, i, attachment.clearColor);
        }
      }

      if (framebuffer.desc.hasDepthAttachment && desc.hasDepthAttachment) {
        const auto &depth = desc.depthAttachment;
        bool clearDepth = depth.depthLoadOp == LoadOp::Clear;
        bool clearStencil = depth.hasStencil && depth.stencilLoadOp == LoadOp::Clear;
        if (clearDepth && clearStencil) {
          glClearBufferfi(GL_DEPTH_STENCIL, 0, depth.clearDepth,
                          static_cast<GLint>(depth.clearStencil));
        } else {
          if (clearDepth) {
            glClearBufferfv(GL_DEPTH, 0, &depth.clearDepth);
          }
          if (clearStencil) {
            GLint value = static_cast<GLint>(depth.clearStencil);
            glClearBufferiv(GL_STENCIL, 0, &value);
          }
        }
      }
    } else {
      bool has_swapchain_attachment = false;
      bool has_offscreen_attachment = false;

      for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
        if (desc.colorAttachments[i].texture.id == 0) {
          has_swapchain_attachment = true;
        } else {
          has_offscreen_attachment = true;
        }
      }

      if (desc.hasDepthAttachment) {
        if (desc.depthAttachment.texture.id == 0) {
          has_swapchain_attachment = true;
        } else {
          has_offscreen_attachment = true;
        }
      }

      if (!has_swapchain_attachment && !has_offscreen_attachment) {
        has_swapchain_attachment = true;
      }

      if (has_swapchain_attachment && has_offscreen_attachment) {
        std::cerr
            << "OpenGL render pass cannot mix swapchain and offscreen attachments"
            << std::endl;
        return;
      }

      using_offscreen_fbo_ = has_offscreen_attachment;

      if (has_offscreen_attachment) {
        glGenFramebuffers(1, &current_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, current_fbo_);
        current_fbo_owned_ = true;

        std::vector<GLenum> draw_buffers;
        draw_buffers.reserve(desc.colorAttachmentCount);
        std::vector<bool> attachment_bound(desc.colorAttachmentCount, false);

        int viewport_width = 0;
        int viewport_height = 0;

        for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
          const auto &attachment = desc.colorAttachments[i];
          auto tex_it = textures_->find(attachment.texture.id);
          if (tex_it == textures_->end()) {
            std::cerr << "Invalid color attachment texture at index " << i
                      << std::endl;
            continue;
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

          attachment_bound[i] = true;
          draw_buffers.push_back(attachment_point);

          if (viewport_width == 0 && viewport_height == 0) {
            viewport_width = tex.width;
            viewport_height = tex.height;
          } else if (viewport_width != tex.width || viewport_height != tex.height) {
            std::cerr
                << "All color attachments must have matching dimensions in OpenGL"
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

        if (desc.hasDepthAttachment && desc.depthAttachment.texture.id != 0) {
          const auto &depth = desc.depthAttachment;
          auto tex_it = textures_->find(depth.texture.id);
          if (tex_it == textures_->end()) {
            std::cerr << "Invalid depth attachment texture" << std::endl;
          } else {
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

            if (viewport_width == 0 && viewport_height == 0) {
              viewport_width = tex.width;
              viewport_height = tex.height;
            }
          }
        }

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
          std::cerr << "OpenGL framebuffer incomplete: 0x" << std::hex << status
                    << std::dec << std::endl;
          glBindFramebuffer(GL_FRAMEBUFFER, 0);
          glDeleteFramebuffers(1, &current_fbo_);
          current_fbo_ = 0;
          using_offscreen_fbo_ = false;
          current_fbo_owned_ = false;
          return;
        }

        if (viewport_width > 0 && viewport_height > 0) {
          glViewport(0, 0, viewport_width, viewport_height);
        }

        for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
          const auto &attachment = desc.colorAttachments[i];
          if (attachment.loadOp == LoadOp::Clear && i < attachment_bound.size() &&
              attachment_bound[i]) {
            glClearBufferfv(GL_COLOR, i, attachment.clearColor);
          }
        }

        if (desc.hasDepthAttachment) {
          const auto &depth = desc.depthAttachment;
          bool clearDepth = depth.depthLoadOp == LoadOp::Clear;
          bool clearStencil =
              depth.hasStencil && depth.stencilLoadOp == LoadOp::Clear;
          if (clearDepth && clearStencil) {
            glClearBufferfi(GL_DEPTH_STENCIL, 0, depth.clearDepth,
                            static_cast<GLint>(depth.clearStencil));
          } else {
            if (clearDepth) {
              glClearBufferfv(GL_DEPTH, 0, &depth.clearDepth);
            }
            if (clearStencil) {
              GLint value = static_cast<GLint>(depth.clearStencil);
              glClearBufferiv(GL_STENCIL, 0, &value);
            }
          }
        }
      } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (window_) {
          int fbw = 0;
          int fbh = 0;
          glfwGetFramebufferSize(window_, &fbw, &fbh);
          if (fbw > 0 && fbh > 0) {
            glViewport(0, 0, fbw, fbh);
          }
        }

        GLbitfield clearMask = 0;

        if (desc.colorAttachmentCount > 0) {
          const auto &color = desc.colorAttachments[0];
          if (color.loadOp == LoadOp::Clear) {
            glClearColor(color.clearColor[0], color.clearColor[1],
                         color.clearColor[2], color.clearColor[3]);
            clearMask |= GL_COLOR_BUFFER_BIT;
          }
        }

        if (desc.hasDepthAttachment) {
          const auto &depth = desc.depthAttachment;
          if (depth.depthLoadOp == LoadOp::Clear) {
            glClearDepth(depth.clearDepth);
            clearMask |= GL_DEPTH_BUFFER_BIT;
          }
          if (depth.hasStencil && depth.stencilLoadOp == LoadOp::Clear) {
            glClearStencil(depth.clearStencil);
            clearMask |= GL_STENCIL_BUFFER_BIT;
          }
        }

        if (clearMask != 0) {
          glClear(clearMask);
        }
      }
    }
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

  void setDepthStencilState(const DepthStencilState &state) override {
    if (depth_stencil_state_initialized_ &&
        state == current_depth_stencil_state_)
      return;

    depth_stencil_state_initialized_ = true;
    current_depth_stencil_state_ = state;

    if (state.depthTestEnable) {
      glEnable(GL_DEPTH_TEST);
    } else {
      glDisable(GL_DEPTH_TEST);
    }

    glDepthMask(state.depthWriteEnable ? GL_TRUE : GL_FALSE);
    glDepthFunc(to_gl_compare(state.depthCompare));

    if (state.stencilEnable) {
      glEnable(GL_STENCIL_TEST);
      glStencilMask(state.stencilWriteMask);
      glStencilFuncSeparate(GL_FRONT_AND_BACK, to_gl_compare(state.stencilCompare),
                            static_cast<GLint>(state.stencilReference),
                            state.stencilReadMask);
      const GLenum fail = to_gl_stencil_op(state.stencilFailOp);
      const GLenum depth_fail = to_gl_stencil_op(state.stencilDepthFailOp);
      const GLenum pass = to_gl_stencil_op(state.stencilPassOp);
      glStencilOpSeparate(GL_FRONT_AND_BACK, fail, depth_fail, pass);
    } else {
      glDisable(GL_STENCIL_TEST);
      glStencilMask(0xFF);
    }
  }

  void setDepthBias(const DepthBiasState &state) override {
    if (depth_bias_initialized_ && state.enable == current_depth_bias_state_.enable &&
        state.constantFactor == current_depth_bias_state_.constantFactor &&
        state.slopeFactor == current_depth_bias_state_.slopeFactor) {
      return;
    }

    depth_bias_initialized_ = true;
    current_depth_bias_state_ = state;

    if (state.enable) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(state.slopeFactor, state.constantFactor);
    } else {
      glDisable(GL_POLYGON_OFFSET_FILL);
    }
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

  void resourceBarrier(std::span<const ResourceBarrierDesc> barriers) override {
    if (barriers.empty())
      return;
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
  }

  void beginQuery(QueryHandle handle, QueryType type) override {
    if (!queries_)
      return;
    auto it = queries_->find(handle.id);
    if (it == queries_->end() || !it->second.query)
      return;
    if (it->second.type != type)
      return;
    switch (type) {
    case QueryType::TimeElapsed:
      it->second.query->begin_time_elapsed();
      break;
    case QueryType::Timestamp:
      break;
    }
  }

  void endQuery(QueryHandle handle, QueryType type) override {
    if (!queries_)
      return;
    auto it = queries_->find(handle.id);
    if (it == queries_->end() || !it->second.query)
      return;
    if (it->second.type != type)
      return;
    switch (type) {
    case QueryType::TimeElapsed:
      it->second.query->end_time_elapsed();
      break;
    case QueryType::Timestamp:
      it->second.query->timestamp();
      break;
    }
  }

  void signalFence(FenceHandle handle) override {
    if (!fences_)
      return;
    auto it = fences_->find(handle.id);
    if (it == fences_->end())
      return;
    if (it->second.sync) {
      glDeleteSync(it->second.sync);
      it->second.sync = nullptr;
    }
    it->second.signaled = false;
    glFlush();
    it->second.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
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
    if (using_offscreen_fbo_ && current_fbo_ != 0) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (current_fbo_owned_) {
        glDeleteFramebuffers(1, &current_fbo_);
      }
      current_fbo_ = 0;
      using_offscreen_fbo_ = false;
      current_fbo_owned_ = false;
    }
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
  std::unordered_map<uint32_t, GLFramebuffer> *framebuffers_;
  std::unordered_map<uint32_t, GLQueryObject> *queries_;
  std::unordered_map<uint32_t, GLFence> *fences_;
  GLFWwindow *window_ = nullptr;

  bool recording_ = false;
  PipelineHandle current_pipeline_{0};
  BufferHandle current_vb_{0};
  BufferHandle current_ib_{0};
  size_t current_vb_offset_ = 0;
  size_t current_ib_offset_ = 0;
  GLuint current_fbo_ = 0;
  bool using_offscreen_fbo_ = false;
  bool current_fbo_owned_ = false;
  DepthStencilState current_depth_stencil_state_{};
  DepthBiasState current_depth_bias_state_{};
  bool depth_stencil_state_initialized_ = false;
  bool depth_bias_initialized_ = false;
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
    caps_.clipSpaceYDown = false;
    caps_.samplerAniso = false;
    caps_.maxSamplerAnisotropy = 1.0f;
    if (has_extension("GL_EXT_texture_filter_anisotropic")) {
      caps_.samplerAniso = true;
      GLfloat max_aniso = 1.0f;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
      if (max_aniso < 1.0f)
        max_aniso = 1.0f;
      caps_.maxSamplerAnisotropy = max_aniso;
    }
    caps_.samplerCompare =
        has_extension("GL_ARB_shadow") || has_extension("GL_EXT_shadow");

    cmd_list_ = std::make_unique<GLCmdList>(
        &buffers_, &textures_, &pipelines_, &framebuffers_, &queries_, &fences_,
        window_);
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
    for (auto &[id, fb] : framebuffers_) {
      if (fb.id != 0) {
        glDeleteFramebuffers(1, &fb.id);
      }
    }
    for (auto &[id, query] : queries_) {
      query.query.reset();
    }
    for (auto &[id, fence] : fences_) {
      if (fence.sync) {
        glDeleteSync(fence.sync);
        fence.sync = nullptr;
      }
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
    glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_R, wrap);

    if ((desc.aniso || desc.maxAnisotropy > 1.0f) && caps_.samplerAniso) {
      GLfloat requested = desc.maxAnisotropy > 1.0f
                              ? static_cast<GLfloat>(desc.maxAnisotropy)
                              : caps_.maxSamplerAnisotropy;
      requested = std::min(requested,
                           static_cast<GLfloat>(caps_.maxSamplerAnisotropy));
      glSamplerParameterf(sampler.id, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                          requested);
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

  ShaderHandle createShader(std::string_view stage,
                            std::span<const uint8_t> bytes) override {
    // Convert bytes to string (assuming it's GLSL source)
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

  FramebufferHandle createFramebuffer(const FramebufferDesc &desc) override {
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
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment_point, tex.target, tex.id,
                               attachment.mipLevel);
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
      glDrawBuffers(static_cast<GLsizei>(draw_buffers.size()), draw_buffers.data());
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
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment_point, tex.target, tex.id,
                               depth.mipLevel);
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

  QueryHandle createQuery(QueryType type) override {
    GLQueryObject query;
    query.type = type;
    query.query = std::make_unique<GLQuery>();
    if (!query.query)
      return QueryHandle{0};
    uint32_t id = next_query_id_++;
    queries_[id] = std::move(query);
    return QueryHandle{id};
  }

  void destroyQuery(QueryHandle handle) override {
    auto it = queries_.find(handle.id);
    if (it == queries_.end())
      return;
    it->second.query.reset();
    queries_.erase(it);
  }

  bool getQueryResult(QueryHandle handle, uint64_t &result,
                      bool wait) override {
    auto it = queries_.find(handle.id);
    if (it == queries_.end() || !it->second.query)
      return false;
    if (!wait && !it->second.query->is_result_available())
      return false;
    result = it->second.query->get_result();
    return true;
  }

  FenceHandle createFence(bool signaled = false) override {
    GLFence fence;
    fence.signaled = signaled;
    fence.sync = nullptr;
    uint32_t id = next_fence_id_++;
    fences_[id] = fence;
    return FenceHandle{id};
  }

  void destroyFence(FenceHandle handle) override {
    auto it = fences_.find(handle.id);
    if (it == fences_.end())
      return;
    if (it->second.sync) {
      glDeleteSync(it->second.sync);
      it->second.sync = nullptr;
    }
    fences_.erase(it);
  }

  void waitFence(FenceHandle handle, uint64_t timeout_ns) override {
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

  void resetFence(FenceHandle handle) override {
    auto it = fences_.find(handle.id);
    if (it == fences_.end())
      return;
    if (it->second.sync) {
      glDeleteSync(it->second.sync);
      it->second.sync = nullptr;
    }
    it->second.signaled = false;
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
  std::unordered_map<uint32_t, GLFramebuffer> framebuffers_;
  std::unordered_map<uint32_t, GLQueryObject> queries_;
  std::unordered_map<uint32_t, GLFence> fences_;

  uint32_t next_buffer_id_ = 1;
  uint32_t next_texture_id_ = 1;
  uint32_t next_sampler_id_ = 1;
  uint32_t next_shader_id_ = 1;
  uint32_t next_pipeline_id_ = 1;
  uint32_t next_framebuffer_id_ = 1;
  uint32_t next_query_id_ = 1;
  uint32_t next_fence_id_ = 1;
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
