// src/rhi/backends/gl/pipeline_gl.cpp
#include <GLFW/glfw3.h>
#include <iostream>
#include <string>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#endif

namespace pixel::rhi::gl {

// ============================================================================
// Pipeline State Management
// ============================================================================

struct PipelineState {
  GLuint current_program = 0;
  GLuint current_vao = 0;
  GLuint current_vbo = 0;
  GLuint current_ibo = 0;
  GLuint current_textures[16] = {0};

  void reset() {
    current_program = 0;
    current_vao = 0;
    current_vbo = 0;
    current_ibo = 0;
    for (int i = 0; i < 16; ++i) {
      current_textures[i] = 0;
    }
  }
};

static PipelineState g_state;

void bind_program(GLuint program) {
  if (g_state.current_program != program) {
    glUseProgram(program);
    g_state.current_program = program;
  }
}

void bind_vao(GLuint vao) {
  if (g_state.current_vao != vao) {
    glBindVertexArray(vao);
    g_state.current_vao = vao;
  }
}

void bind_vbo(GLuint vbo) {
  if (g_state.current_vbo != vbo) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    g_state.current_vbo = vbo;
  }
}

void bind_ibo(GLuint ibo) {
  if (g_state.current_ibo != ibo) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    g_state.current_ibo = ibo;
  }
}

void bind_texture(GLuint texture, int slot) {
  if (slot >= 0 && slot < 16 && g_state.current_textures[slot] != texture) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texture);
    g_state.current_textures[slot] = texture;
  }
}

// ============================================================================
// Shader Compilation Helpers
// ============================================================================

GLuint compile_shader(const char *source, GLenum type) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[1024];
    glGetShaderInfoLog(shader, 1024, nullptr, log);
    std::cerr << "Shader compilation failed:\n" << log << std::endl;
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

GLuint link_program(GLuint vs, GLuint fs) {
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);

  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    char log[1024];
    glGetProgramInfoLog(program, 1024, nullptr, log);
    std::cerr << "Program linking failed:\n" << log << std::endl;
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

// ============================================================================
// Vertex Attribute Setup
// ============================================================================

void setup_vertex_attributes() {
  // Our Vertex structure layout:
  // Vec3 position;   // offset 0,  size 12
  // Vec3 normal;     // offset 12, size 12
  // Vec2 texcoord;   // offset 24, size 8
  // Color color;     // offset 32, size 16 (4 floats)
  // Total: 48 bytes

  const GLsizei stride = 48;

  // Position (location 0)
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);

  // Normal (location 1)
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)12);

  // TexCoord (location 2)
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)24);

  // Color (location 3)
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)32);
}

void setup_instance_attributes(GLuint instance_buffer) {
  // InstanceData structure layout:
  // Vec3 position;              // offset 0,  size 12
  // Vec3 rotation;              // offset 12, size 12
  // Vec3 scale;                 // offset 24, size 12
  // Color color;                // offset 36, size 16
  // float texture_index;        // offset 52, size 4
  // float culling_radius;       // offset 56, size 4
  // float lod_transition_alpha; // offset 60, size 4
  // float _padding;             // offset 64, size 4
  // Total: 68 bytes

  const GLsizei stride = 68;

  glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);

  // Instance position (location 4)
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glVertexAttribDivisor(4, 1);

  // Instance rotation (location 5)
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, stride, (void *)12);
  glVertexAttribDivisor(5, 1);

  // Instance scale (location 6)
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, stride, (void *)24);
  glVertexAttribDivisor(6, 1);

  // Instance color (location 7)
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, stride, (void *)36);
  glVertexAttribDivisor(7, 1);

  // Instance texture_index (location 8)
  glEnableVertexAttribArray(8);
  glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, stride, (void *)52);
  glVertexAttribDivisor(8, 1);

  // Instance lod_transition_alpha (location 9)
  glEnableVertexAttribArray(9);
  glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, stride, (void *)60);
  glVertexAttribDivisor(9, 1);
}

} // namespace pixel::rhi::gl
