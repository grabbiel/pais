// src/rhi/backends/gl/device_gl.cpp
#include "pixel/rhi/backends/gl/device_gl.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
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

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

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

GLDevice::GLDevice(GLFWwindow *window) : window_(window) {
  glfwMakeContextCurrent(window);

  GLint max_texture_size;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);

  const GLubyte *renderer = glGetString(GL_RENDERER);
  const GLubyte *version = glGetString(GL_VERSION);

  const char *renderer_cstr =
      renderer ? reinterpret_cast<const char *>(renderer) : nullptr;
  const char *version_cstr =
      version ? reinterpret_cast<const char *>(version) : nullptr;

  std::cout << "OpenGL Renderer: "
            << (renderer_cstr ? renderer_cstr : "Unknown Renderer")
            << std::endl;
  std::cout << "OpenGL Version: "
            << (version_cstr ? version_cstr : "Unknown Version")
            << std::endl;

  backend_name_ = "OpenGL";
  if (version_cstr && std::strlen(version_cstr) > 0) {
    backend_name_ += " ";
    backend_name_ += version_cstr;
  } else {
    backend_name_ += " (Unknown Version)";
  }
  if (renderer_cstr && std::strlen(renderer_cstr) > 0) {
    backend_name_ += " - ";
    backend_name_ += renderer_cstr;
  }

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

  cmd_list_ =
      std::make_unique<GLCmdList>(&buffers_, &textures_, &pipelines_,
                                  &samplers_, &framebuffers_, &queries_,
                                  &fences_, window_);
}

GLDevice::~GLDevice() {
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

const Caps &GLDevice::caps() const { return caps_; }

const char *GLDevice::backend_name() const { return backend_name_.c_str(); }

CmdList *GLDevice::getImmediate() { return cmd_list_.get(); }

void GLDevice::present() {
  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(window_, &fbw, &fbh);
  if (fbw > 0 && fbh > 0) {
    glViewport(0, 0, fbw, fbh);
  }
  glfwSwapBuffers(window_);
}

} // namespace pixel::rhi::gl

namespace pixel::rhi {

Device *create_gl_device(GLFWwindow *window) {
  return new gl::GLDevice(window);
}

} // namespace pixel::rhi
