#include "pixel/renderer3d/renderer.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Platform-specific OpenGL function loading
#ifdef _WIN32
#include <windows.h>

// OpenGL 3.3 function pointers for Windows
typedef void(APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei, GLuint *);
typedef void(APIENTRYP PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void(APIENTRYP PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void *,
                                            GLenum);
typedef void(APIENTRYP PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint *);
typedef void(APIENTRYP PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void(APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void(APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum,
                                                     GLboolean, GLsizei,
                                                     const void *);
typedef GLuint(APIENTRYP PFNGLCREATESHADERPROC)(GLenum);
typedef void(APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar **,
                                              const GLint *);
typedef void(APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint);
typedef GLuint(APIENTRYP PFNGLCREATEPROGRAMPROC)();
typedef void(APIENTRYP PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void(APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint);
typedef void(APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint);
typedef void(APIENTRYP PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint *);
typedef void(APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei *,
                                                  GLchar *);
typedef void(APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint *);
typedef void(APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei *,
                                                   GLchar *);
typedef GLint(APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar *);
typedef void(APIENTRYP PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void(APIENTRYP PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void(APIENTRYP PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
typedef void(APIENTRYP PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRYP PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat,
                                           GLfloat);
typedef void(APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean,
                                                  const GLfloat *);
typedef void(APIENTRYP PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void(APIENTRYP PFNGLDELETESHADERPROC)(GLuint);
typedef void(APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint);
typedef void(APIENTRYP PFNGLDELETEVERTEXARRAYSPROC)(GLsizei, const GLuint *);
typedef void(APIENTRYP PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint *);

typedef void(APIENTRYP PFNGLTEXIMAGE3DPROC)(GLenum, GLint, GLint, GLsizei,
                                            GLsizei, GLsizei, GLint, GLenum,
                                            GLenum, const void *);
typedef void(APIENTRYP PFNGLTEXSUBIMAGE3DPROC)(GLenum, GLint, GLint, GLint,
                                               GLint, GLsizei, GLsizei, GLsizei,
                                               GLenum, GLenum, const void *);

// Global function pointers
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLUNIFORM2FPROC glUniform2f = nullptr;
PFNGLUNIFORM3FPROC glUniform3f = nullptr;
PFNGLUNIFORM4FPROC glUniform4f = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLTEXIMAGE3DPROC glTexImage3D = nullptr;
PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D = nullptr;

#define LOAD_GL_FUNC(name)                                                     \
  name = (decltype(name))glfwGetProcAddress(#name);                            \
  if (!name)                                                                   \
    throw std::runtime_error("Failed to load " #name);
#endif

namespace pixel::renderer3d {

static void glfw_error_callback(int error, const char *description) {
  std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

// ============================================================================
// Shader Implementation
// ============================================================================

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

// ============================================================================
// Mesh Implementation
// ============================================================================

std::unique_ptr<Mesh> Mesh::create(const std::vector<Vertex> &vertices,
                                   const std::vector<uint32_t> &indices) {
  auto mesh = std::unique_ptr<Mesh>(new Mesh());
  mesh->vertex_count_ = vertices.size();
  mesh->index_count_ = indices.size();

  mesh->vertices_ = vertices;
  mesh->indices_ = indices;

  glGenVertexArrays(1, &mesh->vao_);
  glGenBuffers(1, &mesh->vbo_);
  glGenBuffers(1, &mesh->ebo_);

  glBindVertexArray(mesh->vao_);

  glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
               indices.data(), GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, position));

  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, normal));

  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, texcoord));

  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, color));

  glBindVertexArray(0);

  return mesh;
}

Mesh::~Mesh() {
  if (vao_)
    glDeleteVertexArrays(1, &vao_);
  if (vbo_)
    glDeleteBuffers(1, &vbo_);
  if (ebo_)
    glDeleteBuffers(1, &ebo_);
}

void Mesh::draw() const {
  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
}

// ============================================================================
// Camera Implementation
// ============================================================================

void Camera::get_view_matrix(float *out_mat4) const {
  glm::vec3 pos(position.x, position.y, position.z);
  glm::vec3 tgt(target.x, target.y, target.z);
  glm::vec3 u(up.x, up.y, up.z);

  glm::mat4 view = glm::lookAt(pos, tgt, u);
  memcpy(out_mat4, glm::value_ptr(view), 16 * sizeof(float));
}

void Camera::get_projection_matrix(float *out_mat4, int w, int h) const {
  float aspect = static_cast<float>(w) / static_cast<float>(h);
  glm::mat4 proj;

  if (mode == ProjectionMode::Perspective) {
    proj = glm::perspective(glm::radians(fov), aspect, near_clip, far_clip);
  } else {
    float half_w = ortho_size * aspect;
    float half_h = ortho_size;
    proj = glm::ortho(-half_w, half_w, -half_h, half_h, near_clip, far_clip);
  }

  memcpy(out_mat4, glm::value_ptr(proj), 16 * sizeof(float));
}

void Camera::orbit(float dx, float dy) {
  glm::vec3 dir = glm::normalize(glm::vec3(
      position.x - target.x, position.y - target.y, position.z - target.z));
  float radius = glm::length(glm::vec3(
      position.x - target.x, position.y - target.y, position.z - target.z));

  float theta = atan2(dir.z, dir.x) + dx * 0.01f;
  float phi = asin(dir.y) + dy * 0.01f;
  phi = glm::clamp(phi, -1.5f, 1.5f);

  position.x = target.x + radius * cos(phi) * cos(theta);
  position.y = target.y + radius * sin(phi);
  position.z = target.z + radius * cos(phi) * sin(theta);
}

void Camera::zoom(float delta) {
  glm::vec3 dir = glm::normalize(glm::vec3(
      target.x - position.x, target.y - position.y, target.z - position.z));
  position.x += dir.x * delta;
  position.y += dir.y * delta;
  position.z += dir.z * delta;
}

// ============================================================================
// Renderer Implementation
// ============================================================================

void Renderer::load_gl_functions() {
#ifdef _WIN32
  // Manually load OpenGL 3.3 functions on Windows
  LOAD_GL_FUNC(glGenBuffers);
  LOAD_GL_FUNC(glBindBuffer);
  LOAD_GL_FUNC(glBufferData);
  LOAD_GL_FUNC(glGenVertexArrays);
  LOAD_GL_FUNC(glBindVertexArray);
  LOAD_GL_FUNC(glEnableVertexAttribArray);
  LOAD_GL_FUNC(glVertexAttribPointer);
  LOAD_GL_FUNC(glCreateShader);
  LOAD_GL_FUNC(glShaderSource);
  LOAD_GL_FUNC(glCompileShader);
  LOAD_GL_FUNC(glCreateProgram);
  LOAD_GL_FUNC(glAttachShader);
  LOAD_GL_FUNC(glLinkProgram);
  LOAD_GL_FUNC(glUseProgram);
  LOAD_GL_FUNC(glGetShaderiv);
  LOAD_GL_FUNC(glGetShaderInfoLog);
  LOAD_GL_FUNC(glGetProgramiv);
  LOAD_GL_FUNC(glGetProgramInfoLog);
  LOAD_GL_FUNC(glGetUniformLocation);
  LOAD_GL_FUNC(glUniform1i);
  LOAD_GL_FUNC(glUniform1f);
  LOAD_GL_FUNC(glUniform2f);
  LOAD_GL_FUNC(glUniform3f);
  LOAD_GL_FUNC(glUniform4f);
  LOAD_GL_FUNC(glUniformMatrix4fv);
  LOAD_GL_FUNC(glActiveTexture);
  LOAD_GL_FUNC(glDeleteShader);
  LOAD_GL_FUNC(glDeleteProgram);
  LOAD_GL_FUNC(glDeleteVertexArrays);
  LOAD_GL_FUNC(glDeleteBuffers);
  LOAD_GL_FUNC(glTexImage3D);
  LOAD_GL_FUNC(glTexSubImage3D);

  std::cout << "Windows: Loaded OpenGL 3.3 functions manually" << std::endl;
#endif
}

std::unique_ptr<Renderer>
Renderer::create(const pixel::platform::WindowSpec &spec) {
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit())
    throw std::runtime_error("Failed to initialize GLFW");

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  auto renderer = std::unique_ptr<Renderer>(new Renderer());

  renderer->window_ =
      glfwCreateWindow(spec.w, spec.h, spec.title.c_str(), nullptr, nullptr);
  if (!renderer->window_) {
    glfwTerminate();
    throw std::runtime_error("Failed to create GLFW window");
  }

  glfwMakeContextCurrent(renderer->window_);

  // Load OpenGL functions (platform-specific)
  renderer->load_gl_functions();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glfwSwapInterval(1);

  std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
  std::cout << "GLFW Version: " << glfwGetVersionString() << std::endl;
  std::cout << "No GLAD - using native OpenGL headers" << std::endl;

  renderer->setup_default_shaders();
  renderer->sprite_mesh_ = renderer->create_sprite_quad();

  return renderer;
}

Renderer::~Renderer() {
  if (window_)
    glfwDestroyWindow(window_);
  glfwTerminate();
}

void Renderer::begin_frame(const Color &clear_color) {
  glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::end_frame() { glfwSwapBuffers(window_); }

bool Renderer::process_events() {
  glfwPollEvents();
  update_input_state();
  return !glfwWindowShouldClose(window_);
}

void Renderer::update_input_state() {
  for (int key = 0; key < 512; ++key) {
    input_state_.keys[key] = (glfwGetKey(window_, key) == GLFW_PRESS);
  }

  for (int btn = 0; btn < 8; ++btn) {
    input_state_.mouse_buttons[btn] =
        (glfwGetMouseButton(window_, btn) == GLFW_PRESS);
  }

  double x, y;
  glfwGetCursorPos(window_, &x, &y);
  input_state_.mouse_delta_x = x - last_mouse_x_;
  input_state_.mouse_delta_y = y - last_mouse_y_;
  input_state_.mouse_x = x;
  input_state_.mouse_y = y;
  last_mouse_x_ = x;
  last_mouse_y_ = y;

  input_state_.scroll_delta = 0.0;
}

// ============================================================================
// Texture Array Implementation
// ============================================================================

TextureArrayID Renderer::create_texture_array(int width, int height,
                                              int layers) {
  if (layers <= 0 || width <= 0 || height <= 0) {
    throw std::runtime_error("Invalid texture array dimensions");
  }

  uint32_t gl_id;
  glGenTextures(1, &gl_id);
  glBindTexture(GL_TEXTURE_2D_ARRAY, gl_id);

  // Allocate storage for all layers
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, width, height, layers, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

  // Set texture parameters
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  TextureArrayID id = next_texture_array_id_++;
  TextureArrayInfo info;
  info.width = width;
  info.height = height;
  info.layers = layers;
  info.gl_id = gl_id;
  texture_arrays_[id] = info;

  std::cout << "Created texture array: " << width << "x" << height << " with "
            << layers << " layers" << std::endl;

  return id;
}

TextureArrayID
Renderer::load_texture_array(const std::vector<std::string> &paths) {
  if (paths.empty()) {
    throw std::runtime_error(
        "Cannot create texture array from empty path list");
  }

  // Load first texture to get dimensions
  int width, height, channels;
  stbi_set_flip_vertically_on_load(true);
  uint8_t *first_data =
      stbi_load(paths[0].c_str(), &width, &height, &channels, 4);

  if (!first_data) {
    throw std::runtime_error("Failed to load texture: " + paths[0]);
  }

  // Create texture array
  TextureArrayID array_id = create_texture_array(width, height, paths.size());

  // Upload first texture
  set_texture_array_layer(array_id, 0, first_data);
  stbi_image_free(first_data);

  // Load and upload remaining textures
  for (size_t i = 1; i < paths.size(); ++i) {
    int w, h, c;
    uint8_t *data = stbi_load(paths[i].c_str(), &w, &h, &c, 4);

    if (!data) {
      std::cerr << "Warning: Failed to load texture " << paths[i]
                << ", using placeholder" << std::endl;
      // Create a simple colored placeholder
      std::vector<uint8_t> placeholder(width * height * 4);
      for (size_t j = 0; j < placeholder.size(); j += 4) {
        placeholder[j] = (i * 50) % 256;      // R
        placeholder[j + 1] = (i * 100) % 256; // G
        placeholder[j + 2] = (i * 150) % 256; // B
        placeholder[j + 3] = 255;             // A
      }
      set_texture_array_layer(array_id, i, placeholder.data());
      continue;
    }

    if (w != width || h != height) {
      std::cerr << "Warning: Texture " << paths[i]
                << " has different dimensions (" << w << "x" << h
                << ") than expected (" << width << "x" << height
                << "), skipping" << std::endl;
      stbi_image_free(data);
      continue;
    }

    set_texture_array_layer(array_id, i, data);
    stbi_image_free(data);
  }

  // Generate mipmaps
  auto it = texture_arrays_.find(array_id);
  if (it != texture_arrays_.end()) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, it->second.gl_id);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  }

  std::cout << "Loaded texture array with " << paths.size() << " textures"
            << std::endl;

  return array_id;
}

void Renderer::set_texture_array_layer(TextureArrayID array_id, int layer,
                                       const uint8_t *data) {
  auto it = texture_arrays_.find(array_id);
  if (it == texture_arrays_.end()) {
    throw std::runtime_error("Invalid texture array ID");
  }

  const auto &info = it->second;

  if (layer < 0 || layer >= info.layers) {
    throw std::runtime_error("Layer index out of range");
  }

  glBindTexture(GL_TEXTURE_2D_ARRAY, info.gl_id);
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, info.width, info.height,
                  1, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Renderer::bind_texture_array(TextureArrayID id, int slot) {
  auto it = texture_arrays_.find(id);
  if (it != texture_arrays_.end()) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D_ARRAY, it->second.gl_id);
  }
}

TextureArrayInfo Renderer::get_texture_array_info(TextureArrayID id) const {
  auto it = texture_arrays_.find(id);
  return it != texture_arrays_.end() ? it->second : TextureArrayInfo{};
}

// ============================================================================
// Shader Management & Drawing - Same as before
// ============================================================================

void Renderer::setup_default_shaders() {
  std::string vert_3d = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aNormal;
    layout (location = 2) in vec2 aTexCoord;
    layout (location = 3) in vec4 aColor;
    
    out vec3 FragPos;
    out vec3 Normal;
    out vec2 TexCoord;
    out vec4 Color;
    
    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;
    
    void main() {
      FragPos = vec3(model * vec4(aPos, 1.0));
      Normal = mat3(transpose(inverse(model))) * aNormal;
      TexCoord = aTexCoord;
      Color = aColor;
      gl_Position = projection * view * vec4(FragPos, 1.0);
    }
  )";

  std::string frag_3d = R"(
    #version 330 core
    out vec4 FragColor;
    
    in vec3 FragPos;
    in vec3 Normal;
    in vec2 TexCoord;
    in vec4 Color;
    
    uniform sampler2D uTexture;
    uniform bool useTexture;
    uniform vec3 lightPos;
    uniform vec3 viewPos;
    
    void main() {
      vec3 lightColor = vec3(1.0, 1.0, 1.0);
      float ambientStrength = 0.3;
      vec3 ambient = ambientStrength * lightColor;
      vec3 norm = normalize(Normal);
      vec3 lightDir = normalize(lightPos - FragPos);
      float diff = max(dot(norm, lightDir), 0.0);
      vec3 diffuse = diff * lightColor;
      vec4 texColor = useTexture ? texture(uTexture, TexCoord) : vec4(1.0);
      vec3 result = (ambient + diffuse) * texColor.rgb * Color.rgb;
      FragColor = vec4(result, texColor.a * Color.a);
    }
  )";

  default_shader_ = create_shader_from_source(vert_3d, frag_3d);

  std::string vert_instanced = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aNormal;
    layout (location = 2) in vec2 aTexCoord;
    layout (location = 3) in vec4 aColor;
    
    // Instance attributes
    layout (location = 4) in vec3 instancePos;
    layout (location = 5) in vec3 instanceRot;
    layout (location = 6) in vec3 instanceScale;
    layout (location = 7) in vec4 instanceColor;
    layout (location = 8) in float instanceTexIndex;
    
    out vec3 FragPos;
    out vec3 Normal;
    out vec2 TexCoord;
    out vec4 Color;
    flat out int TexIndex;
    
    uniform mat4 view;
    uniform mat4 projection;
    
    mat4 rotationMatrix(vec3 euler) {
      float cx = cos(radians(euler.x));
      float sx = sin(radians(euler.x));
      float cy = cos(radians(euler.y));
      float sy = sin(radians(euler.y));
      float cz = cos(radians(euler.z));
      float sz = sin(radians(euler.z));
      
      mat4 rotX = mat4(1, 0, 0, 0, 0, cx, -sx, 0, 0, sx, cx, 0, 0, 0, 0, 1);
      mat4 rotY = mat4(cy, 0, sy, 0, 0, 1, 0, 0, -sy, 0, cy, 0, 0, 0, 0, 1);
      mat4 rotZ = mat4(cz, -sz, 0, 0, sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
      
      return rotZ * rotY * rotX;
    }
    
    void main() {
      mat4 model = mat4(1.0);
      model[3] = vec4(instancePos, 1.0);
      
      mat4 scale = mat4(instanceScale.x, 0, 0, 0,
                        0, instanceScale.y, 0, 0,
                        0, 0, instanceScale.z, 0,
                        0, 0, 0, 1);
      
      mat4 rotation = rotationMatrix(instanceRot);
      model = model * rotation * scale;
      
      FragPos = vec3(model * vec4(aPos, 1.0));
      Normal = mat3(transpose(inverse(model))) * aNormal;
      TexCoord = aTexCoord;
      Color = aColor * instanceColor;
      TexIndex = int(instanceTexIndex);
      
      gl_Position = projection * view * vec4(FragPos, 1.0);
    }
  )";

  std::string frag_instanced = R"(
    #version 330 core
    out vec4 FragColor;
    
    in vec3 FragPos;
    in vec3 Normal;
    in vec2 TexCoord;
    in vec4 Color;
    flat in int TexIndex;
    
    uniform sampler2DArray uTextureArray;
    uniform bool useTextureArray;
    uniform vec3 lightPos;
    uniform vec3 viewPos;
    
    void main() {
      vec3 lightColor = vec3(1.0, 1.0, 1.0);
      float ambientStrength = 0.3;
      vec3 ambient = ambientStrength * lightColor;
      
      vec3 norm = normalize(Normal);
      vec3 lightDir = normalize(lightPos - FragPos);
      float diff = max(dot(norm, lightDir), 0.0);
      vec3 diffuse = diff * lightColor;
      
      vec4 texColor = vec4(1.0);
      if (useTextureArray) {
        texColor = texture(uTextureArray, vec3(TexCoord, float(TexIndex)));
      }
      
      vec3 result = (ambient + diffuse) * texColor.rgb * Color.rgb;
      FragColor = vec4(result, texColor.a * Color.a);
    }
  )";

  instanced_shader_ = create_shader_from_source(vert_instanced, frag_instanced);

  std::string vert_sprite = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 2) in vec2 aTexCoord;
    out vec2 TexCoord;
    uniform mat4 view;
    uniform mat4 projection;
    uniform vec3 spritePos;
    uniform vec2 spriteSize;
    void main() {
      mat4 modelView = view;
      modelView[0] = vec4(spriteSize.x, 0, 0, 0);
      modelView[1] = vec4(0, spriteSize.y, 0, 0);
      modelView[2] = vec4(0, 0, 1, 0);
      modelView[3] = view * vec4(spritePos, 1.0);
      TexCoord = aTexCoord;
      gl_Position = projection * modelView * vec4(aPos, 1.0);
    }
  )";

  std::string frag_sprite = R"(
    #version 330 core
    out vec4 FragColor;
    in vec2 TexCoord;
    uniform sampler2D uTexture;
    uniform vec4 tint;
    void main() {
      vec4 texColor = texture(uTexture, TexCoord);
      FragColor = texColor * tint;
      if (FragColor.a < 0.1) discard;
    }
  )";

  sprite_shader_ = create_shader_from_source(vert_sprite, frag_sprite);
}

ShaderID Renderer::create_shader_from_source(const std::string &vert_src,
                                             const std::string &frag_src) {
  ShaderID id = next_shader_id_++;
  shaders_[id] = Shader::create(vert_src, frag_src);
  return id;
}

Shader *Renderer::get_shader(ShaderID id) {
  auto it = shaders_.find(id);
  return it != shaders_.end() ? it->second.get() : nullptr;
}

TextureID Renderer::load_texture(const std::string &path) {
  auto it = texture_path_to_id_.find(path);
  if (it != texture_path_to_id_.end())
    return it->second;

  int width, height, channels;
  stbi_set_flip_vertically_on_load(true);
  uint8_t *data = stbi_load(path.c_str(), &width, &height, &channels, 4);

  if (!data)
    throw std::runtime_error("Failed to load texture: " + path);

  TextureID id = create_texture(width, height, data);
  stbi_image_free(data);

  texture_path_to_id_[path] = id;
  return id;
}

TextureID Renderer::create_texture(int width, int height, const uint8_t *data) {
  uint32_t gl_id;
  glGenTextures(1, &gl_id);
  glBindTexture(GL_TEXTURE_2D, gl_id);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  TextureID id = next_texture_id_++;
  TextureInfo info;
  info.width = width;
  info.height = height;
  info.gl_id = gl_id;
  textures_[id] = info;

  return id;
}

void Renderer::bind_texture(TextureID id, int slot) {
  auto it = textures_.find(id);
  if (it != textures_.end()) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, it->second.gl_id);
  }
}

void Renderer::draw_mesh(const Mesh &mesh, const Vec3 &position,
                         const Vec3 &rotation, const Vec3 &scale,
                         const Material &material) {
  Shader *shader = get_shader(default_shader_);
  if (!shader)
    return;

  shader->bind();

  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, glm::vec3(position.x, position.y, position.z));
  model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
  model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
  model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));
  model = glm::scale(model, glm::vec3(scale.x, scale.y, scale.z));

  float view[16], proj[16];
  camera_.get_view_matrix(view);
  camera_.get_projection_matrix(proj, window_width(), window_height());

  shader->set_mat4("model", glm::value_ptr(model));
  shader->set_mat4("view", view);
  shader->set_mat4("projection", proj);
  shader->set_vec3("lightPos", {5, 10, 5});
  shader->set_vec3("viewPos", camera_.position);

  if (material.texture != INVALID_TEXTURE) {
    bind_texture(material.texture, 0);
    shader->set_int("uTexture", 0);
    shader->set_int("useTexture", 1);
  } else {
    shader->set_int("useTexture", 0);
  }

  mesh.draw();
}

void Renderer::draw_sprite(TextureID texture, const Vec3 &position,
                           const Vec2 &size, const Color &tint) {
  Shader *shader = get_shader(sprite_shader_);
  if (!shader)
    return;

  shader->bind();

  float view[16], proj[16];
  camera_.get_view_matrix(view);
  camera_.get_projection_matrix(proj, window_width(), window_height());

  shader->set_mat4("view", view);
  shader->set_mat4("projection", proj);
  shader->set_vec3("spritePos", position);
  shader->set_vec2("spriteSize", size);
  shader->set_vec4("tint", {tint.r, tint.g, tint.b, tint.a});

  bind_texture(texture, 0);
  shader->set_int("uTexture", 0);

  sprite_mesh_->draw();
}

std::unique_ptr<Mesh> Renderer::create_sprite_quad() {
  std::vector<Vertex> verts = {
      {{-0.5f, -0.5f, 0}, {0, 0, 1}, {0, 0}, Color::White()},
      {{0.5f, -0.5f, 0}, {0, 0, 1}, {1, 0}, Color::White()},
      {{0.5f, 0.5f, 0}, {0, 0, 1}, {1, 1}, Color::White()},
      {{-0.5f, 0.5f, 0}, {0, 0, 1}, {0, 1}, Color::White()}};
  std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
  return Mesh::create(verts, indices);
}

std::unique_ptr<Mesh> Renderer::create_cube(float size) {
  std::vector<Vertex> verts = primitives::create_cube_vertices(size);
  std::vector<uint32_t> indices;
  for (uint32_t i = 0; i < verts.size(); ++i)
    indices.push_back(i);
  return Mesh::create(verts, indices);
}

std::unique_ptr<Mesh> Renderer::create_plane(float w, float d, int segs) {
  std::vector<Vertex> verts = primitives::create_plane_vertices(w, d, segs);
  std::vector<uint32_t> indices;
  for (uint32_t i = 0; i < verts.size(); ++i)
    indices.push_back(i);
  return Mesh::create(verts, indices);
}

int Renderer::window_width() const {
  int w;
  glfwGetWindowSize(window_, &w, nullptr);
  return w;
}

int Renderer::window_height() const {
  int h;
  glfwGetWindowSize(window_, nullptr, &h);
  return h;
}

double Renderer::time() const { return glfwGetTime(); }

namespace primitives {
std::vector<Vertex> create_cube_vertices(float s) {
  float h = s / 2;
  return {{{-h, -h, h}, {0, 0, 1}, {0, 0}, Color::White()},
          {{h, -h, h}, {0, 0, 1}, {1, 0}, Color::White()},
          {{h, h, h}, {0, 0, 1}, {1, 1}, Color::White()},
          {{h, h, h}, {0, 0, 1}, {1, 1}, Color::White()},
          {{-h, h, h}, {0, 0, 1}, {0, 1}, Color::White()},
          {{-h, -h, h}, {0, 0, 1}, {0, 0}, Color::White()},
          {{h, -h, -h}, {0, 0, -1}, {0, 0}, Color::White()},
          {{-h, -h, -h}, {0, 0, -1}, {1, 0}, Color::White()},
          {{-h, h, -h}, {0, 0, -1}, {1, 1}, Color::White()},
          {{-h, h, -h}, {0, 0, -1}, {1, 1}, Color::White()},
          {{h, h, -h}, {0, 0, -1}, {0, 1}, Color::White()},
          {{h, -h, -h}, {0, 0, -1}, {0, 0}, Color::White()},
          {{-h, h, h}, {0, 1, 0}, {0, 0}, Color::White()},
          {{h, h, h}, {0, 1, 0}, {1, 0}, Color::White()},
          {{h, h, -h}, {0, 1, 0}, {1, 1}, Color::White()},
          {{h, h, -h}, {0, 1, 0}, {1, 1}, Color::White()},
          {{-h, h, -h}, {0, 1, 0}, {0, 1}, Color::White()},
          {{-h, h, h}, {0, 1, 0}, {0, 0}, Color::White()},
          {{-h, -h, -h}, {0, -1, 0}, {0, 0}, Color::White()},
          {{h, -h, -h}, {0, -1, 0}, {1, 0}, Color::White()},
          {{h, -h, h}, {0, -1, 0}, {1, 1}, Color::White()},
          {{h, -h, h}, {0, -1, 0}, {1, 1}, Color::White()},
          {{-h, -h, h}, {0, -1, 0}, {0, 1}, Color::White()},
          {{-h, -h, -h}, {0, -1, 0}, {0, 0}, Color::White()},
          {{h, -h, h}, {1, 0, 0}, {0, 0}, Color::White()},
          {{h, -h, -h}, {1, 0, 0}, {1, 0}, Color::White()},
          {{h, h, -h}, {1, 0, 0}, {1, 1}, Color::White()},
          {{h, h, -h}, {1, 0, 0}, {1, 1}, Color::White()},
          {{h, h, h}, {1, 0, 0}, {0, 1}, Color::White()},
          {{h, -h, h}, {1, 0, 0}, {0, 0}, Color::White()},
          {{-h, -h, -h}, {-1, 0, 0}, {0, 0}, Color::White()},
          {{-h, -h, h}, {-1, 0, 0}, {1, 0}, Color::White()},
          {{-h, h, h}, {-1, 0, 0}, {1, 1}, Color::White()},
          {{-h, h, h}, {-1, 0, 0}, {1, 1}, Color::White()},
          {{-h, h, -h}, {-1, 0, 0}, {0, 1}, Color::White()},
          {{-h, -h, -h}, {-1, 0, 0}, {0, 0}, Color::White()}};
}

std::vector<Vertex> create_plane_vertices(float w, float d, int segs) {
  std::vector<Vertex> verts;
  float hw = w / 2, hd = d / 2;
  for (int z = 0; z < segs; ++z) {
    for (int x = 0; x < segs; ++x) {
      float x0 = -hw + (w * x) / segs, x1 = -hw + (w * (x + 1)) / segs;
      float z0 = -hd + (d * z) / segs, z1 = -hd + (d * (z + 1)) / segs;
      verts.push_back({{x0, 0, z0}, {0, 1, 0}, {0, 0}, Color::White()});
      verts.push_back({{x1, 0, z0}, {0, 1, 0}, {1, 0}, Color::White()});
      verts.push_back({{x1, 0, z1}, {0, 1, 0}, {1, 1}, Color::White()});
      verts.push_back({{x1, 0, z1}, {0, 1, 0}, {1, 1}, Color::White()});
      verts.push_back({{x0, 0, z1}, {0, 1, 0}, {0, 1}, Color::White()});
      verts.push_back({{x0, 0, z0}, {0, 1, 0}, {0, 0}, Color::White()});
    }
  }
  return verts;
}
} // namespace primitives

} // namespace pixel::renderer3d
