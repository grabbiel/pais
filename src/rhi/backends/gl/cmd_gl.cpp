// src/rhi/backends/gl/cmd_gl.cpp
#include "device_gl.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <vector>

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

GLCmdList::GLCmdList(std::unordered_map<uint32_t, GLBuffer> *buffers,
                     std::unordered_map<uint32_t, GLTexture> *textures,
                     std::unordered_map<uint32_t, GLPipeline> *pipelines,
                     std::unordered_map<uint32_t, GLFramebuffer> *framebuffers,
                     std::unordered_map<uint32_t, GLQueryObject> *queries,
                     std::unordered_map<uint32_t, GLFence> *fences,
                     GLFWwindow *window)
    : buffers_(buffers), textures_(textures), pipelines_(pipelines),
      framebuffers_(framebuffers), queries_(queries), fences_(fences),
      window_(window) {}

void GLCmdList::begin() {
  recording_ = true;
  depth_stencil_state_initialized_ = false;
  depth_bias_initialized_ = false;
}

void GLCmdList::beginRender(const RenderPassDesc &desc) {
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
      std::cerr << "OpenGL render pass cannot mix swapchain and offscreen attachments"
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

      for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
        const auto &attachment = desc.colorAttachments[i];
        if (attachment.texture.id == 0)
          continue;

        auto tex_it = textures_->find(attachment.texture.id);
        if (tex_it == textures_->end())
          continue;

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
        attachment_bound[i] = true;
      }

      if (!draw_buffers.empty()) {
        glDrawBuffers(static_cast<GLsizei>(draw_buffers.size()), draw_buffers.data());
      } else {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
      }

      if (desc.hasDepthAttachment && desc.depthAttachment.texture.id != 0) {
        auto tex_it = textures_->find(desc.depthAttachment.texture.id);
        if (tex_it != textures_->end()) {
          const GLTexture &tex = tex_it->second;
          GLenum attachment_point = desc.depthAttachment.hasStencil
                                        ? GL_DEPTH_STENCIL_ATTACHMENT
                                        : GL_DEPTH_ATTACHMENT;

          if (tex.target == GL_TEXTURE_2D) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment_point, tex.target,
                                   tex.id, desc.depthAttachment.mipLevel);
          } else if (tex.target == GL_TEXTURE_2D_ARRAY) {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment_point, tex.id,
                                      desc.depthAttachment.mipLevel,
                                      desc.depthAttachment.arraySlice);
          } else {
            glFramebufferTexture(GL_FRAMEBUFFER, attachment_point, tex.id,
                                 desc.depthAttachment.mipLevel);
          }
        }
      }

      GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Temporary framebuffer incomplete: 0x" << std::hex << status
                  << std::dec << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &current_fbo_);
        current_fbo_ = 0;
        current_fbo_owned_ = false;
        return;
      }

      if (desc.colorAttachmentCount > 0 && desc.colorAttachments[0].texture.id != 0) {
        auto tex_it = textures_->find(desc.colorAttachments[0].texture.id);
        if (tex_it != textures_->end()) {
          glViewport(0, 0, tex_it->second.width, tex_it->second.height);
        }
      }
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      int fbw = 0, fbh = 0;
      glfwGetFramebufferSize(window_, &fbw, &fbh);
      glViewport(0, 0, fbw, fbh);

      if (desc.colorAttachmentCount > 0) {
        const auto &attachment = desc.colorAttachments[0];
        if (attachment.loadOp == LoadOp::Clear) {
          glClearColor(attachment.clearColor[0], attachment.clearColor[1],
                       attachment.clearColor[2], attachment.clearColor[3]);
          glClear(GL_COLOR_BUFFER_BIT);
        }
      }

      if (desc.hasDepthAttachment) {
        const auto &depth = desc.depthAttachment;
        if (depth.depthLoadOp == LoadOp::Clear ||
            (depth.hasStencil && depth.stencilLoadOp == LoadOp::Clear)) {
          GLbitfield clear_bits = 0;
          if (depth.depthLoadOp == LoadOp::Clear)
            clear_bits |= GL_DEPTH_BUFFER_BIT;
          if (depth.hasStencil && depth.stencilLoadOp == LoadOp::Clear)
            clear_bits |= GL_STENCIL_BUFFER_BIT;
          glClearDepth(depth.clearDepth);
          glClearStencil(static_cast<GLint>(depth.clearStencil));
          glClear(clear_bits);
        }
      }
    }
  }
}

void GLCmdList::setPipeline(PipelineHandle handle) {
  auto it = pipelines_->find(handle.id);
  if (it == pipelines_->end())
    return;

  current_pipeline_ = handle;
  const GLPipeline &pipeline = it->second;

  glUseProgram(pipeline.program);
  glBindVertexArray(pipeline.vao);
}

void GLCmdList::setVertexBuffer(BufferHandle handle, size_t offset) {
  if (current_vb_.id == handle.id && current_vb_offset_ == offset)
    return;

  auto it = buffers_->find(handle.id);
  if (it == buffers_->end())
    return;

  glBindBuffer(GL_ARRAY_BUFFER, it->second.id);
  current_vb_ = handle;
  current_vb_offset_ = offset;
}

void GLCmdList::setIndexBuffer(BufferHandle handle, size_t offset) {
  if (current_ib_.id == handle.id && current_ib_offset_ == offset)
    return;

  auto it = buffers_->find(handle.id);
  if (it == buffers_->end())
    return;

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.id);
  current_ib_ = handle;
  current_ib_offset_ = offset;
}

void GLCmdList::setInstanceBuffer(BufferHandle handle, size_t stride, size_t offset) {
  auto pit = pipelines_->find(current_pipeline_.id);
  if (pit == pipelines_->end())
    return;

  auto it = buffers_->find(handle.id);
  if (it == buffers_->end())
    return;

  glBindVertexArray(pit->second.vao);
  glBindBuffer(GL_ARRAY_BUFFER, it->second.id);

  const GLsizei instance_stride = stride;

  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, instance_stride,
                        reinterpret_cast<void *>(offset + 0));
  glVertexAttribDivisor(4, 1);

  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, instance_stride,
                        reinterpret_cast<void *>(offset + 12));
  glVertexAttribDivisor(5, 1);

  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, instance_stride,
                        reinterpret_cast<void *>(offset + 24));
  glVertexAttribDivisor(6, 1);

  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, instance_stride,
                        reinterpret_cast<void *>(offset + 36));
  glVertexAttribDivisor(7, 1);

  glEnableVertexAttribArray(8);
  glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, instance_stride,
                        reinterpret_cast<void *>(offset + 52));
  glVertexAttribDivisor(8, 1);

  glEnableVertexAttribArray(9);
  glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, instance_stride,
                        reinterpret_cast<void *>(offset + 60));
  glVertexAttribDivisor(9, 1);
}

void GLCmdList::setUniformMat4(const char *name, const float *mat4x4) {
  GLint loc = getUniformLocation(name);
  if (loc >= 0) {
    glUniformMatrix4fv(loc, 1, GL_FALSE, mat4x4);
  }
}

void GLCmdList::setUniformVec3(const char *name, const float *vec3) {
  GLint loc = getUniformLocation(name);
  if (loc >= 0) {
    glUniform3fv(loc, 1, vec3);
  }
}

void GLCmdList::setUniformVec4(const char *name, const float *vec4) {
  GLint loc = getUniformLocation(name);
  if (loc >= 0) {
    glUniform4fv(loc, 1, vec4);
  }
}

void GLCmdList::setUniformInt(const char *name, int value) {
  GLint loc = getUniformLocation(name);
  if (loc >= 0) {
    glUniform1i(loc, value);
  }
}

void GLCmdList::setUniformFloat(const char *name, float value) {
  GLint loc = getUniformLocation(name);
  if (loc >= 0) {
    glUniform1f(loc, value);
  }
}

void GLCmdList::setUniformBuffer(uint32_t binding, BufferHandle buffer,
                                 size_t offset, size_t size) {
  auto it = buffers_->find(buffer.id);
  if (it == buffers_->end())
    return;

  const GLBuffer &buf = it->second;
  if (size > 0) {
    glBindBufferRange(GL_UNIFORM_BUFFER, binding, buf.id, offset, size);
  } else {
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, buf.id);
  }
}

void GLCmdList::setTexture(const char *name, TextureHandle texture,
                           uint32_t slot) {
  auto it = textures_->find(texture.id);
  if (it == textures_->end())
    return;

  const GLTexture &tex = it->second;
  glActiveTexture(GL_TEXTURE0 + slot);
  glBindTexture(tex.target, tex.id);

  GLint loc = getUniformLocation(name);
  if (loc >= 0) {
    glUniform1i(loc, slot);
  }
}

void GLCmdList::copyToTexture(TextureHandle texture, uint32_t mipLevel,
                              std::span<const std::byte> data) {
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
  glTexSubImage2D(tex.target, mipLevel, 0, 0, tex.width, tex.height, format, type,
                  data.data());
  glBindTexture(tex.target, 0);
}

void GLCmdList::copyToTextureLayer(TextureHandle texture, uint32_t layer,
                                   uint32_t mipLevel,
                                   std::span<const std::byte> data) {
  auto it = textures_->find(texture.id);
  if (it == textures_->end())
    return;

  GLTexture &tex = it->second;

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
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, mipLevel, 0, 0, layer, tex.width,
                    tex.height, 1, format, type, data.data());
  } else {
    if (layer == 0) {
      glTexSubImage2D(tex.target, mipLevel, 0, 0, tex.width, tex.height, format,
                      type, data.data());
    } else {
      std::cerr << "Cannot upload to layer " << layer
                << " of a non-array texture" << std::endl;
    }
  }

  glBindTexture(tex.target, 0);
}

void GLCmdList::setComputePipeline(PipelineHandle handle) {
  auto it = pipelines_->find(handle.id);
  if (it == pipelines_->end())
    return;

  current_pipeline_ = handle;
  const GLPipeline &pipeline = it->second;

  glUseProgram(pipeline.program);
}

void GLCmdList::setStorageBuffer(uint32_t binding, BufferHandle buffer,
                                 size_t offset, size_t size) {
  auto it = buffers_->find(buffer.id);
  if (it == buffers_->end())
    return;

  const GLBuffer &buf = it->second;
  if (size > 0) {
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, binding, buf.id, offset, size);
  } else {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buf.id);
  }
}

void GLCmdList::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                         uint32_t groupCountZ) {
  glDispatchCompute(groupCountX, groupCountY, groupCountZ);
}

void GLCmdList::memoryBarrier() { glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); }

void GLCmdList::resourceBarrier(std::span<const ResourceBarrierDesc> barriers) {
  if (barriers.empty())
    return;
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

void GLCmdList::beginQuery(QueryHandle handle, QueryType type) {
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

void GLCmdList::endQuery(QueryHandle handle, QueryType type) {
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

void GLCmdList::signalFence(FenceHandle handle) {
  auto it = fences_->find(handle.id);
  if (it == fences_->end())
    return;

  if (it->second.sync) {
    glDeleteSync(it->second.sync);
    it->second.sync = nullptr;
  }

  it->second.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  it->second.signaled = false;
}

void GLCmdList::drawIndexed(uint32_t indexCount, uint32_t firstIndex,
                            uint32_t instanceCount) {
  if (instanceCount > 1) {
    glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT,
                            reinterpret_cast<void *>(firstIndex * sizeof(uint32_t)),
                            instanceCount);
  } else {
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT,
                   reinterpret_cast<void *>(firstIndex * sizeof(uint32_t)));
  }
}

void GLCmdList::endRender() {
  if (current_fbo_owned_ && current_fbo_ != 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &current_fbo_);
  } else if (using_offscreen_fbo_) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  current_fbo_ = 0;
  current_fbo_owned_ = false;
  using_offscreen_fbo_ = false;
}

void GLCmdList::copyToBuffer(BufferHandle handle, size_t dstOff,
                             std::span<const std::byte> src) {
  auto it = buffers_->find(handle.id);
  if (it == buffers_->end())
    return;

  glBindBuffer(it->second.target, it->second.id);
  glBufferSubData(it->second.target, dstOff, src.size(), src.data());
  glBindBuffer(it->second.target, 0);
}

void GLCmdList::end() { recording_ = false; }

GLint GLCmdList::getUniformLocation(const char *name) {
  if (current_pipeline_.id == 0)
    return -1;

  auto pit = pipelines_->find(current_pipeline_.id);
  if (pit == pipelines_->end())
    return -1;

  GLPipeline &pipeline = pit->second;

  auto it = pipeline.uniform_locations.find(name);
  if (it != pipeline.uniform_locations.end()) {
    return it->second;
  }

  GLint loc = glGetUniformLocation(pipeline.program, name);
  pipeline.uniform_locations[name] = loc;
  return loc;
}

} // namespace pixel::rhi::gl
