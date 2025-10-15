#include "pixel/renderer3d/renderer_instanced.hpp"
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <random>

// Platform-specific OpenGL function loading
#ifdef _WIN32
typedef void(APIENTRYP PFNGLDRAWELEMENTSINSTANCEDPROC)(GLenum, GLsizei, GLenum,
                                                       const void *, GLsizei);
typedef void(APIENTRYP PFNGLVERTEXATTRIBDIVISORPROC)(GLuint, GLuint);

extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;

PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced = nullptr;
PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor = nullptr;

static void load_instancing_functions() {
  static bool loaded = false;
  if (loaded)
    return;

  glDrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)glfwGetProcAddress(
      "glDrawElementsInstanced");
  glVertexAttribDivisor =
      (PFNGLVERTEXATTRIBDIVISORPROC)glfwGetProcAddress("glVertexAttribDivisor");

  loaded = true;
}
#else
#include <GLFW/glfw3.h>
static void load_instancing_functions() {} // No-op on non-Windows
#endif

namespace pixel::renderer3d {

// ============================================================================
// InstancedMesh Implementation
// ============================================================================

std::unique_ptr<InstancedMesh> InstancedMesh::create(const Mesh &base_mesh,
                                                     size_t max_instances) {
  load_instancing_functions();

  auto instanced = std::unique_ptr<InstancedMesh>(new InstancedMesh());
  instanced->max_instances_ = max_instances;
  instanced->index_count_ = base_mesh.index_count();

  // We need to recreate the VAO/VBO/EBO with instance attributes
  glGenVertexArrays(1, &instanced->vao_);
  glGenBuffers(1, &instanced->vbo_);
  glGenBuffers(1, &instanced->ebo_);
  glGenBuffers(1, &instanced->instance_vbo_);

  glBindVertexArray(instanced->vao_);

  // Copy base mesh data
  // Note: This is a simplified approach. In production, you'd want to
  // extract vertices/indices from the base mesh properly.
  // For now, we assume the mesh data is accessible or we create a new mesh.

  instanced->setup_instance_buffer();

  glBindVertexArray(0);

  return instanced;
}

InstancedMesh::~InstancedMesh() {
  if (vao_)
    glDeleteVertexArrays(1, &vao_);
  if (vbo_)
    glDeleteBuffers(1, &vbo_);
  if (ebo_)
    glDeleteBuffers(1, &ebo_);
  if (instance_vbo_)
    glDeleteBuffers(1, &instance_vbo_);
}

void InstancedMesh::setup_instance_buffer() {
  // Reserve space for instance data
  instance_data_.reserve(max_instances_);

  // Bind instance buffer
  glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
  glBufferData(GL_ARRAY_BUFFER, max_instances_ * sizeof(InstanceData), nullptr,
               GL_DYNAMIC_DRAW);

  // Setup instance attributes
  // Attribute 4: Position (vec3)
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, position));
  glVertexAttribDivisor(4, 1);

  // Attribute 5: Rotation (vec3)
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, rotation));
  glVertexAttribDivisor(5, 1);

  // Attribute 6: Scale (vec3)
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, scale));
  glVertexAttribDivisor(6, 1);

  // Attribute 7: Color (vec4)
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, color));
  glVertexAttribDivisor(7, 1);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void InstancedMesh::update_instance_buffer() {
  if (!needs_update_)
    return;

  glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
  glBufferData(GL_ARRAY_BUFFER, instance_data_.size() * sizeof(InstanceData),
               instance_data_.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  needs_update_ = false;
}

void InstancedMesh::set_instances(const std::vector<InstanceData> &instances) {
  instance_data_ = instances;
  instance_count_ = instances.size();
  needs_update_ = true;
}

void InstancedMesh::update_instance(size_t index, const InstanceData &data) {
  if (index < instance_data_.size()) {
    instance_data_[index] = data;
    needs_update_ = true;
  }
}

void InstancedMesh::draw() const {
  if (instance_count_ == 0)
    return;

  // Update buffer if needed (const_cast is safe here for lazy update)
  const_cast<InstancedMesh *>(this)->update_instance_buffer();

  glBindVertexArray(vao_);
  glDrawElementsInstanced(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, 0,
                          instance_count_);
  glBindVertexArray(0);
}

// ============================================================================
// RendererInstanced Implementation
// ============================================================================

std::unique_ptr<InstancedMesh>
RendererInstanced::create_instanced_mesh(const Mesh &mesh,
                                         size_t max_instances) {
  return InstancedMesh::create(mesh, max_instances);
}

void RendererInstanced::draw_instanced(Renderer &renderer,
                                       const InstancedMesh &mesh,
                                       const Material &base_material) {
  // Get instanced shader (you'd need to add this to Renderer)
  // For now, we'll use the default shader
  Shader *shader = renderer.get_shader(renderer.default_shader());
  if (!shader)
    return;

  shader->bind();

  // Set view and projection matrices
  float view[16], proj[16];
  renderer.camera().get_view_matrix(view);
  renderer.camera().get_projection_matrix(proj, renderer.window_width(),
                                          renderer.window_height());

  shader->set_mat4("view", view);
  shader->set_mat4("projection", proj);
  shader->set_vec3("lightPos", {5, 10, 5});
  shader->set_vec3("viewPos", renderer.camera().position);

  // Set material properties
  if (base_material.texture != INVALID_TEXTURE) {
    renderer.bind_texture(base_material.texture, 0);
    shader->set_int("uTexture", 0);
    shader->set_int("useTexture", 1);
  } else {
    shader->set_int("useTexture", 0);
  }

  // Draw all instances
  mesh.draw();

  shader->unbind();
}

// ============================================================================
// Helper Functions for Instance Generation
// ============================================================================

std::vector<InstanceData> RendererInstanced::create_grid(int width, int depth,
                                                         float spacing,
                                                         float y_offset) {
  std::vector<InstanceData> instances;
  instances.reserve(width * depth);

  float start_x = -(width - 1) * spacing * 0.5f;
  float start_z = -(depth - 1) * spacing * 0.5f;

  for (int z = 0; z < depth; ++z) {
    for (int x = 0; x < width; ++x) {
      InstanceData data;
      data.position = {start_x + x * spacing, y_offset, start_z + z * spacing};
      data.rotation = {0, 0, 0};
      data.scale = {1, 1, 1};

      // Vary color based on position
      float hue = (float)(x + z) / (width + depth);
      data.color = Color(0.5f + 0.5f * std::sin(hue * 6.28f),
                         0.5f + 0.5f * std::sin(hue * 6.28f + 2.0f),
                         0.5f + 0.5f * std::sin(hue * 6.28f + 4.0f), 1.0f);

      instances.push_back(data);
    }
  }

  return instances;
}

std::vector<InstanceData>
RendererInstanced::create_circle(int count, float radius, float y_offset) {
  std::vector<InstanceData> instances;
  instances.reserve(count);

  for (int i = 0; i < count; ++i) {
    float angle = (float)i / count * 6.28318530718f; // 2*PI

    InstanceData data;
    data.position = {radius * std::cos(angle), y_offset,
                     radius * std::sin(angle)};
    data.rotation = {0, angle * 57.2957795131f, 0}; // Convert to degrees
    data.scale = {1, 1, 1};

    float hue = (float)i / count;
    data.color = Color(0.5f + 0.5f * std::sin(hue * 6.28f),
                       0.5f + 0.5f * std::sin(hue * 6.28f + 2.0f),
                       0.5f + 0.5f * std::sin(hue * 6.28f + 4.0f), 1.0f);

    instances.push_back(data);
  }

  return instances;
}

std::vector<InstanceData>
RendererInstanced::create_random(int count, const Vec3 &min_bounds,
                                 const Vec3 &max_bounds) {
  std::vector<InstanceData> instances;
  instances.reserve(count);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> pos_x(min_bounds.x, max_bounds.x);
  std::uniform_real_distribution<float> pos_y(min_bounds.y, max_bounds.y);
  std::uniform_real_distribution<float> pos_z(min_bounds.z, max_bounds.z);
  std::uniform_real_distribution<float> rot(0.0f, 360.0f);
  std::uniform_real_distribution<float> scale_dist(0.5f, 1.5f);
  std::uniform_real_distribution<float> color_dist(0.0f, 1.0f);

  for (int i = 0; i < count; ++i) {
    InstanceData data;
    data.position = {pos_x(gen), pos_y(gen), pos_z(gen)};
    data.rotation = {rot(gen), rot(gen), rot(gen)};

    float s = scale_dist(gen);
    data.scale = {s, s, s};

    data.color = Color(color_dist(gen), color_dist(gen), color_dist(gen), 1.0f);

    instances.push_back(data);
  }

  return instances;
}

} // namespace pixel::renderer3d
