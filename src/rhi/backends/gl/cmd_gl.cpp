// src/rhi/backends/gl/cmd_gl.cpp
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#endif

namespace pixel::rhi::gl {

// ============================================================================
// Command Recording and Execution
// ============================================================================

struct DrawCommand {
  enum Type {
    DRAW_INDEXED,
    DRAW_INDEXED_INSTANCED,
    CLEAR,
    BIND_PIPELINE,
    BIND_BUFFER,
    BIND_TEXTURE,
    SET_VIEWPORT,
    SET_SCISSOR
  };

  Type type;

  union {
    struct {
      uint32_t index_count;
      uint32_t first_index;
      uint32_t instance_count;
    } draw;

    struct {
      float color[4];
      float depth;
      uint8_t stencil;
    } clear;

    struct {
      uint32_t program;
      uint32_t vao;
    } pipeline;

    struct {
      uint32_t buffer;
      uint32_t target;
    } buffer;

    struct {
      uint32_t texture;
      int slot;
    } texture;

    struct {
      int x, y, width, height;
    } viewport;
  } params;
};

// ============================================================================
// Batch Rendering Optimization
// ============================================================================

class CommandBatcher {
public:
  void begin_batch() {
    commands_.clear();
    current_program_ = 0;
    current_vao_ = 0;
  }

  void add_draw(uint32_t index_count, uint32_t first_index,
                uint32_t instance_count) {
    DrawCommand cmd;
    cmd.type = instance_count > 1 ? DrawCommand::DRAW_INDEXED_INSTANCED
                                  : DrawCommand::DRAW_INDEXED;
    cmd.params.draw.index_count = index_count;
    cmd.params.draw.first_index = first_index;
    cmd.params.draw.instance_count = instance_count;
    commands_.push_back(cmd);
  }

  void execute_batch() {
    for (const auto &cmd : commands_) {
      switch (cmd.type) {
      case DrawCommand::DRAW_INDEXED:
        glDrawElements(
            GL_TRIANGLES, cmd.params.draw.index_count, GL_UNSIGNED_INT,
            (void *)(cmd.params.draw.first_index * sizeof(uint32_t)));
        break;

      case DrawCommand::DRAW_INDEXED_INSTANCED:
        glDrawElementsInstanced(
            GL_TRIANGLES, cmd.params.draw.index_count, GL_UNSIGNED_INT,
            (void *)(cmd.params.draw.first_index * sizeof(uint32_t)),
            cmd.params.draw.instance_count);
        break;

      default:
        break;
      }
    }
  }

private:
  std::vector<DrawCommand> commands_;
  uint32_t current_program_ = 0;
  uint32_t current_vao_ = 0;
};

// ============================================================================
// OpenGL Debug Callback
// ============================================================================

#ifndef __APPLE__
void GLAPIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id,
                                  GLenum severity, GLsizei length,
                                  const GLchar *message,
                                  const void *userParam) {
  // Ignore non-significant error/warning codes
  if (id == 131169 || id == 131185 || id == 131218 || id == 131204)
    return;

  std::cerr << "OpenGL Debug Message (" << id << "): " << message << std::endl;

  switch (source) {
  case GL_DEBUG_SOURCE_API:
    std::cerr << "Source: API";
    break;
  case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
    std::cerr << "Source: Window System";
    break;
  case GL_DEBUG_SOURCE_SHADER_COMPILER:
    std::cerr << "Source: Shader Compiler";
    break;
  case GL_DEBUG_SOURCE_THIRD_PARTY:
    std::cerr << "Source: Third Party";
    break;
  case GL_DEBUG_SOURCE_APPLICATION:
    std::cerr << "Source: Application";
    break;
  case GL_DEBUG_SOURCE_OTHER:
    std::cerr << "Source: Other";
    break;
  }
  std::cerr << std::endl;

  switch (type) {
  case GL_DEBUG_TYPE_ERROR:
    std::cerr << "Type: Error";
    break;
  case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
    std::cerr << "Type: Deprecated";
    break;
  case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
    std::cerr << "Type: Undefined";
    break;
  case GL_DEBUG_TYPE_PORTABILITY:
    std::cerr << "Type: Portability";
    break;
  case GL_DEBUG_TYPE_PERFORMANCE:
    std::cerr << "Type: Performance";
    break;
  case GL_DEBUG_TYPE_MARKER:
    std::cerr << "Type: Marker";
    break;
  case GL_DEBUG_TYPE_PUSH_GROUP:
    std::cerr << "Type: Push Group";
    break;
  case GL_DEBUG_TYPE_POP_GROUP:
    std::cerr << "Type: Pop Group";
    break;
  case GL_DEBUG_TYPE_OTHER:
    std::cerr << "Type: Other";
    break;
  }
  std::cerr << std::endl;

  switch (severity) {
  case GL_DEBUG_SEVERITY_HIGH:
    std::cerr << "Severity: high";
    break;
  case GL_DEBUG_SEVERITY_MEDIUM:
    std::cerr << "Severity: medium";
    break;
  case GL_DEBUG_SEVERITY_LOW:
    std::cerr << "Severity: low";
    break;
  case GL_DEBUG_SEVERITY_NOTIFICATION:
    std::cerr << "Severity: notification";
    break;
  }
  std::cerr << std::endl << std::endl;
}
#endif

void enable_gl_debug_output() {
#ifndef __APPLE__
  GLint flags;
  glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
  if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(gl_debug_callback, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                          GL_TRUE);
  }
#endif
}

// ============================================================================
// Render State Management
// ============================================================================

struct RenderState {
  bool depth_test_enabled = true;
  bool blend_enabled = true;
  GLenum blend_src = GL_SRC_ALPHA;
  GLenum blend_dst = GL_ONE_MINUS_SRC_ALPHA;
  GLenum depth_func = GL_LESS;
  bool cull_face_enabled = false;
  GLenum cull_mode = GL_BACK;

  void apply() {
    if (depth_test_enabled) {
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(depth_func);
    } else {
      glDisable(GL_DEPTH_TEST);
    }

    if (blend_enabled) {
      glEnable(GL_BLEND);
      glBlendFunc(blend_src, blend_dst);
    } else {
      glDisable(GL_BLEND);
    }

    if (cull_face_enabled) {
      glEnable(GL_CULL_FACE);
      glCullFace(cull_mode);
    } else {
      glDisable(GL_CULL_FACE);
    }
  }
};

static RenderState g_render_state;

void set_default_render_state() {
  g_render_state.depth_test_enabled = true;
  g_render_state.blend_enabled = true;
  g_render_state.blend_src = GL_SRC_ALPHA;
  g_render_state.blend_dst = GL_ONE_MINUS_SRC_ALPHA;
  g_render_state.depth_func = GL_LESS;
  g_render_state.cull_face_enabled = false;
  g_render_state.apply();
}

// ============================================================================
// Texture Management Helpers
// ============================================================================

void upload_texture_data(GLuint texture, GLenum target, int width, int height,
                         GLenum format, GLenum type, const void *data,
                         int level = 0) {
  glBindTexture(target, texture);

  if (target == GL_TEXTURE_2D) {
    glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, format, type,
                    data);
  } else if (target == GL_TEXTURE_2D_ARRAY) {
    // For texture arrays, use glTexSubImage3D
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, 0, width, height, 1,
                    format, type, data);
  }

  glBindTexture(target, 0);
}

void generate_mipmaps(GLuint texture, GLenum target) {
  glBindTexture(target, texture);
  glGenerateMipmap(target);
  glBindTexture(target, 0);
}

// ============================================================================
// Buffer Management Helpers
// ============================================================================

void update_buffer_data(GLuint buffer, GLenum target, size_t offset,
                        size_t size, const void *data) {
  glBindBuffer(target, buffer);
  glBufferSubData(target, offset, size, data);
  glBindBuffer(target, 0);
}

void *map_buffer(GLuint buffer, GLenum target, GLenum access = GL_READ_WRITE) {
  glBindBuffer(target, buffer);
  void *ptr = glMapBuffer(target, access);
  return ptr;
}

void unmap_buffer(GLuint buffer, GLenum target) {
  glBindBuffer(target, buffer);
  glUnmapBuffer(target);
  glBindBuffer(target, 0);
}

// ============================================================================
// Query Performance Helpers
// ============================================================================

class GLQuery {
public:
  GLQuery() { glGenQueries(1, &query_id_); }

  ~GLQuery() { glDeleteQueries(1, &query_id_); }

  void begin_time_elapsed() { glBeginQuery(GL_TIME_ELAPSED, query_id_); }

  void end_time_elapsed() { glEndQuery(GL_TIME_ELAPSED); }

  bool is_result_available() {
    GLint available;
    glGetQueryObjectiv(query_id_, GL_QUERY_RESULT_AVAILABLE, &available);
    return available != 0;
  }

  uint64_t get_result() {
    uint64_t elapsed_time;
    glGetQueryObjectui64v(query_id_, GL_QUERY_RESULT, &elapsed_time);
    return elapsed_time;
  }

private:
  GLuint query_id_;
};

} // namespace pixel::rhi::gl
