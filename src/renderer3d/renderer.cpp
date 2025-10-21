// src/renderer3d/renderer.cpp (Updated for RHI)
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_fwd.hpp"
#include "pixel/rhi/rhi.hpp"
#include "pixel/platform/shader_loader.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

struct GLFWwindow;

namespace pixel::renderer3d {

struct DeviceCreationResult {
  rhi::Device *device = nullptr;
  GLFWwindow *window = nullptr;
  std::string backend_name;
};
DeviceCreationResult create_renderer_device(const platform::WindowSpec &spec);

static void glfw_error_callback(int error, const char *description) {
  std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

// ============================================================================
// Default Shaders
// ============================================================================

// In renderer.cpp - Update default shaders to include material color

const char *default_vertex_shader = R"(
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

const char *default_fragment_shader = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 Color;

uniform sampler2D uTexture;
uniform int useTexture;
uniform vec4 materialColor;  // ADD THIS
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
  
  vec3 viewDir = normalize(viewPos - FragPos);
  vec3 reflectDir = reflect(-lightDir, norm);
  float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
  vec3 specular = 0.5 * spec * lightColor;
  
  // Use materialColor instead of just vertex color
  vec4 baseColor = materialColor * Color;
  vec4 texColor = (useTexture == 1) ? texture(uTexture, TexCoord) * baseColor : baseColor;
  
  vec3 result = (ambient + diffuse + specular) * texColor.rgb;
  FragColor = vec4(result, texColor.a);
}
)";

const char *instanced_vertex_shader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aColor;

// Instance attributes
layout (location = 4) in vec3 iPosition;
layout (location = 5) in vec3 iRotation;
layout (location = 6) in vec3 iScale;
layout (location = 7) in vec4 iColor;
layout (location = 8) in float iTextureIndex;
layout (location = 9) in float iLODAlpha;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out vec4 Color;
out float TextureIndex;
out float LODAlpha;

uniform mat4 view;
uniform mat4 projection;

mat4 rotationMatrix(vec3 axis, float angle) {
  axis = normalize(axis);
  float s = sin(angle);
  float c = cos(angle);
  float oc = 1.0 - c;
  
  return mat4(
    oc * axis.x * axis.x + c,           oc * axis.x * axis.y - axis.z * s,  oc * axis.z * axis.x + axis.y * s,  0.0,
    oc * axis.x * axis.y + axis.z * s,  oc * axis.y * axis.y + c,           oc * axis.y * axis.z - axis.x * s,  0.0,
    oc * axis.z * axis.x - axis.y * s,  oc * axis.y * axis.z + axis.x * s,  oc * axis.z * axis.z + c,           0.0,
    0.0,                                 0.0,                                 0.0,                                 1.0
  );
}

void main() {
  // Build transformation matrix
  mat4 rotX = rotationMatrix(vec3(1, 0, 0), iRotation.x);
  mat4 rotY = rotationMatrix(vec3(0, 1, 0), iRotation.y);
  mat4 rotZ = rotationMatrix(vec3(0, 0, 1), iRotation.z);
  mat4 rotation = rotZ * rotY * rotX;
  
  // Apply scale and rotation to position
  vec4 scaledPos = vec4(aPos * iScale, 1.0);
  vec4 rotatedPos = rotation * scaledPos;
  vec4 worldPos = rotatedPos + vec4(iPosition, 0.0);
  
  FragPos = worldPos.xyz;
  Normal = mat3(rotation) * aNormal;
  TexCoord = aTexCoord;
  Color = aColor * iColor;
  TextureIndex = iTextureIndex;
  LODAlpha = iLODAlpha;
  
  gl_Position = projection * view * worldPos;
}
)";

const char *instanced_fragment_shader = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 Color;
in float TextureIndex;
in float LODAlpha;

uniform sampler2DArray uTextureArray;
uniform int useTextureArray;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform float uTime;
uniform int uDitherEnabled;

float getBayerValue(vec2 pos) {
  int x = int(mod(pos.x, 4.0));
  int y = int(mod(pos.y, 4.0));
  
  float bayer[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
  );
  
  return bayer[y * 4 + x];
}

void main() {
  // Dithered LOD transition
  if (uDitherEnabled > 0 && LODAlpha < 1.0) {
    float threshold = getBayerValue(gl_FragCoord.xy);
    
    // Temporal jitter for animated dither
    if (uDitherEnabled > 1) {
      float jitter = fract(uTime * 0.5);
      threshold = fract(threshold + jitter);
    }
    
    if (LODAlpha < threshold) {
      discard;
    }
  }
  
  // Standard lighting
  vec3 lightColor = vec3(1.0);
  float ambientStrength = 0.3;
  vec3 ambient = ambientStrength * lightColor;
  
  vec3 norm = normalize(Normal);
  vec3 lightDir = normalize(lightPos - FragPos);
  float diff = max(dot(norm, lightDir), 0.0);
  vec3 diffuse = diff * lightColor;
  
  vec3 viewDir = normalize(viewPos - FragPos);
  vec3 reflectDir = reflect(-lightDir, norm);
  float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
  vec3 specular = 0.5 * spec * lightColor;
  
  vec4 texColor;
  if (useTextureArray == 1) {
    texColor = texture(uTextureArray, vec3(TexCoord, TextureIndex));
  } else {
    texColor = Color;
  }
  
  vec3 result = (ambient + diffuse + specular) * texColor.rgb * Color.rgb;
  FragColor = vec4(result, texColor.a * Color.a);
}
)";

// ============================================================================
// Renderer Implementation
// ============================================================================

std::unique_ptr<Renderer>
Renderer::create(const pixel::platform::WindowSpec &spec) {
  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit())
    throw std::runtime_error("Failed to initialize GLFW");

  auto renderer = std::unique_ptr<Renderer>(new Renderer());

  try {
    // Use the device factory helper - it handles all backend selection logic
    auto result = create_renderer_device(spec);

    renderer->window_ = result.window;
    renderer->device_ = result.device;

    std::cout << "Renderer Backend: " << result.backend_name << std::endl;

  } catch (const std::exception &e) {
    glfwTerminate();
    throw std::runtime_error(std::string("Failed to initialize renderer: ") +
                             e.what());
  }

  renderer->setup_default_shaders();
  renderer->sprite_mesh_ = renderer->create_sprite_quad();

  return renderer;
}

Renderer::~Renderer() {
  if (device_) {
    delete device_;
    device_ = nullptr;
  }

  if (window_)
    glfwDestroyWindow(window_);
  glfwTerminate();
}

void Renderer::setup_default_shaders() {
  default_shader_ =
      create_shader_from_source(default_vertex_shader, default_fragment_shader);
  instanced_shader_ = create_shader_from_source(instanced_vertex_shader,
                                                instanced_fragment_shader);
  sprite_shader_ = default_shader_; // Use same for sprites
}

void Renderer::begin_frame(const Color &clear_color) {
  auto *cmd = device_->getImmediate();
  cmd->begin();

  float clear[4] = {clear_color.r, clear_color.g, clear_color.b, clear_color.a};

  rhi::TextureHandle backbuffer{0};
  rhi::TextureHandle depth_buffer{0};

  cmd->beginRender(backbuffer, depth_buffer, clear, 1.0f, 0);
}

void Renderer::end_frame() {
  auto *cmd = device_->getImmediate();
  cmd->endRender();
  cmd->end();

  device_->present();
}

bool Renderer::process_events() {
  glfwPollEvents();
  update_input_state();
  return !glfwWindowShouldClose(window_);
}

void Renderer::update_input_state() {
  std::memcpy(input_state_.prev_keys, input_state_.keys,
              sizeof(input_state_.keys));
  std::memcpy(input_state_.prev_mouse_buttons, input_state_.mouse_buttons,
              sizeof(input_state_.mouse_buttons));
  input_state_.prev_mouse_x = input_state_.mouse_x;
  input_state_.prev_mouse_y = input_state_.mouse_y;

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

ShaderID Renderer::create_shader_from_source(const std::string &vert_src,
                                             const std::string &frag_src) {
  ShaderID id = next_shader_id_++;
  shaders_[id] = Shader::create(device_, vert_src, frag_src);
  return id;
}

ShaderID Renderer::load_shader(const std::string &vert_path,
                               const std::string &frag_path) {
  // Load shader source files from disk
  auto [vert_src, frag_src] = platform::load_shader_pair(vert_path, frag_path);

  // Create shader from the loaded source code
  return create_shader_from_source(vert_src, frag_src);
}

Shader *Renderer::get_shader(ShaderID id) {
  auto it = shaders_.find(id);
  return it != shaders_.end() ? it->second.get() : nullptr;
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

const char *Renderer::backend_name() const { return "OpenGL 3.3 Core"; }

} // namespace pixel::renderer3d
