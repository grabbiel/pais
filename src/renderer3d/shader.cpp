#include "pixel/renderer3d/renderer.hpp"
// ============================================================================
// Shader Implementation
// ============================================================================

namespace pixel::renderer3d {
std::unique_ptr<Shader> Shader::create(const std::string &vert_src,
                                       const std::string &frag_src) {
  auto shader = std::unique_ptr<Shader>(new Shader());

  uint32_t vert = glCreateShader(GL_VERTEX_SHADER);
  const char *v_src = vert_src.c_str();
  glShaderSource(vert, 1, &v_src, nullptr);
  glCompileShader(vert);

  int success;
  glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[512];
    glGetShaderInfoLog(vert, 512, nullptr, log);
    throw std::runtime_error(std::string("Vertex shader error: ") + log);
  }

  uint32_t frag = glCreateShader(GL_FRAGMENT_SHADER);
  const char *f_src = frag_src.c_str();
  glShaderSource(frag, 1, &f_src, nullptr);
  glCompileShader(frag);

  glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[512];
    glGetShaderInfoLog(frag, 512, nullptr, log);
    throw std::runtime_error(std::string("Fragment shader error: ") + log);
  }

  shader->program_ = glCreateProgram();
  glAttachShader(shader->program_, vert);
  glAttachShader(shader->program_, frag);
  glLinkProgram(shader->program_);

  glGetProgramiv(shader->program_, GL_LINK_STATUS, &success);
  if (!success) {
    char log[512];
    glGetProgramInfoLog(shader->program_, 512, nullptr, log);
    throw std::runtime_error(std::string("Shader link error: ") + log);
  }

  glDeleteShader(vert);
  glDeleteShader(frag);

  return shader;
}

Shader::~Shader() {
  if (program_)
    glDeleteProgram(program_);
}

void Shader::bind() const { glUseProgram(program_); }

void Shader::unbind() const { glUseProgram(0); }

void Shader::set_int(const std::string &name, int value) {
  glUniform1i(glGetUniformLocation(program_, name.c_str()), value);
}

void Shader::set_float(const std::string &name, float value) {
  glUniform1f(glGetUniformLocation(program_, name.c_str()), value);
}

void Shader::set_vec2(const std::string &name, const Vec2 &value) {
  glUniform2f(glGetUniformLocation(program_, name.c_str()), value.x, value.y);
}

void Shader::set_vec3(const std::string &name, const Vec3 &value) {
  glUniform3f(glGetUniformLocation(program_, name.c_str()), value.x, value.y,
              value.z);
}

void Shader::set_vec4(const std::string &name, const Vec4 &value) {
  glUniform4f(glGetUniformLocation(program_, name.c_str()), value.x, value.y,
              value.z, value.w);
}

void Shader::set_mat4(const std::string &name, const float *value) {
  glUniformMatrix4fv(glGetUniformLocation(program_, name.c_str()), 1, GL_FALSE,
                     value);
}
} // namespace pixel::renderer3d
