#include "pixel/renderer3d/renderer.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stdexcept>

namespace pixel::renderer3d {

static void glfw_error_callback(int error, const char *description) {
  std::cerr << "GLFW Error " << error << ": " << description << std::endl;
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
    layout (location = 9) in float instanceCullingRadius;
    layout (location = 10) in float instanceLODAlpha;
    
    out vec3 FragPos;
    out vec3 Normal;
    out vec2 TexCoord;
    out vec4 Color;
    out float TexIndex;
    out float LODTransitionAlpha;
    
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
      LODTransitionAlpha = instanceLODAlpha;
      
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
    in float TexIndex;
    in float LODTransitionAlpha;
    
    uniform sampler2DArray uTextureArray;
    uniform bool useTextureArray;
    uniform vec3 lightPos;
    uniform vec3 viewPos;
    uniform float uTime;
    uniform int uDitheredEnabled;

    const mat4 bayerMatrix4 = mat4(
      0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
      12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
      3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
      15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    );

    float getDitherThreshold(vec2 fragCoord) {
      // Use 4x4 Bayer matrix tiled
      float bayer4 = getBayerValue(fragCoord);
      
      // Optional: Add temporal jitter to animate pattern
      if (uDitherEnabled > 1) {  // Mode 2 = temporal jitter
        float jitter = fract(uTime * 0.5);
        bayer4 = fract(bayer4 + jitter);
      }
      
      return bayer4;
    }
        
    void main() {
      // Dithered LOD transition discard test
      if (uDitherEnabled > 0 && LODTransitionAlpha < 1.0) {
        float threshold = getDitherThreshold(gl_FragCoord.xy);
        
        // Discard fragments based on transition alpha
        // When alpha is 0.0, discard all; when 1.0, keep all
        if (LODTransitionAlpha < threshold) {
          discard;
        }
      }
      
      // Standard lighting calculation
      vec3 norm = normalize(Normal);
      vec3 lightDir = normalize(lightPos - FragPos);
      vec3 viewDir = normalize(viewPos - FragPos);
      vec3 reflectDir = reflect(-lightDir, norm);
      
      float diff = max(dot(norm, lightDir), 0.0);
      float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
      
      vec3 ambient = 0.3 * Color.rgb;
      vec3 diffuse = diff * Color.rgb;
      vec3 specular = spec * 0.5 * vec3(1.0);
      
      if (useTextureArray == 1) {
        vec4 texColor = texture(uTextureArray, vec3(TexCoord, TexIndex));
        vec3 result = (ambient + diffuse + specular) * texColor.rgb * Color.rgb;
        FragColor = vec4(result, texColor.a * Color.a);
      } else {
        vec3 result = ambient + diffuse + specular;
        FragColor = vec4(result, Color.a);
      }
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
