#include "pixel/renderer3d/renderer_instanced.hpp"
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <random>

// Platform-specific OpenGL function loading
#ifdef _WIN32
typedef void(APIENTRYP PFNGLDRAWELEMENTSINSTANCEDPROC)(GLenum, GLsizei, GLenum,
                                                       const void *, GLsizei);
typedef void(APIENTRYP PFNGLVERTEXATTRIBDIVISORPROC)(GLuint, GLuint);
typedef void(APIENTRYP PFNGLBUFFERSTORAGEPROC)(GLenum, GLsizeiptr, const void *,
                                               GLbitfield);
typedef void *(APIENTRYP PFNGLMAPBUFFERRANGEPROC)(GLenum, GLintptr, GLsizeiptr,
                                                  GLbitfield);
typedef GLboolean(APIENTRYP PFNGLUNMAPBUFFERPROC)(GLenum);
typedef uint64_t(APIENTRYP PFNGLFENCESYNCPROC)(GLenum, GLbitfield);
typedef void(APIENTRYP PFNGLDELETESYNCPROC)(uint64_t);
typedef GLenum(APIENTRYP PFNGLCLIENTWAITSYNCPROC)(uint64_t, GLbitfield,
                                                  uint64_t);

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
PFNGLBUFFERSTORAGEPROC glBufferStorage = nullptr;
PFNGLMAPBUFFERRANGEPROC glMapBufferRange = nullptr;
PFNGLUNMAPBUFFERPROC glUnmapBuffer = nullptr;
PFNGLFENCESYNCPROC glFenceSync = nullptr;
PFNGLDELETESYNCPROC glDeleteSync = nullptr;
PFNGLCLIENTWAITSYNCPROC glClientWaitSync = nullptr;

static bool g_persistent_mapping_supported = false;

static void load_instancing_functions() {
  static bool loaded = false;
  if (loaded)
    return;

  glDrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)glfwGetProcAddress(
      "glDrawElementsInstanced");
  glVertexAttribDivisor =
      (PFNGLVERTEXATTRIBDIVISORPROC)glfwGetProcAddress("glVertexAttribDivisor");

  // Try to load persistent mapping functions
  glBufferStorage =
      (PFNGLBUFFERSTORAGEPROC)glfwGetProcAddress("glBufferStorage");
  glMapBufferRange =
      (PFNGLMAPBUFFERRANGEPROC)glfwGetProcAddress("glMapBufferRange");
  glUnmapBuffer = (PFNGLUNMAPBUFFERPROC)glfwGetProcAddress("glUnmapBuffer");
  glFenceSync = (PFNGLFENCESYNCPROC)glfwGetProcAddress("glFenceSync");
  glDeleteSync = (PFNGLDELETESYNCPROC)glfwGetProcAddress("glDeleteSync");
  glClientWaitSync =
      (PFNGLCLIENTWAITSYNCPROC)glfwGetProcAddress("glClientWaitSync");

  g_persistent_mapping_supported =
      (glBufferStorage && glMapBufferRange && glFenceSync && glClientWaitSync);

  if (g_persistent_mapping_supported) {
    std::cout << "Persistent mapped buffers: SUPPORTED" << std::endl;
  } else {
    std::cout << "Persistent mapped buffers: NOT SUPPORTED (using fallback)"
              << std::endl;
  }

  loaded = true;
}
#else
#include <GLFW/glfw3.h>

// Define GL constants if not available
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#define GL_MAP_COHERENT_BIT 0x0080
#define GL_DYNAMIC_STORAGE_BIT 0x0100
#define GL_CLIENT_STORAGE_BIT 0x0200
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#define GL_ALREADY_SIGNALED 0x911A
#define GL_TIMEOUT_EXPIRED 0x911B
#define GL_CONDITION_SATISFIED 0x911C
#define GL_WAIT_FAILED 0x911D
#endif

// Check for persistent mapping support at runtime
static bool g_persistent_mapping_supported = false;

static void load_instancing_functions() {
#ifdef __APPLE__
  // macOS OpenGL is stuck at 4.1 - persistent mapping not available
  g_persistent_mapping_supported = false;
  std::cout << "Persistent mapped buffers: NOT SUPPORTED (macOS OpenGL 4.1)"
            << std::endl;
#else
  // Linux - check for extension at runtime
  const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
  if (extensions && strstr(extensions, "GL_ARB_buffer_storage")) {
    g_persistent_mapping_supported = true;
    std::cout << "Persistent mapped buffers: SUPPORTED" << std::endl;
  } else {
    g_persistent_mapping_supported = false;
    std::cout << "Persistent mapped buffers: NOT SUPPORTED (missing extension)"
              << std::endl;
  }
#endif
}

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
  const auto &vertices = base_mesh.vertices();
  glBindBuffer(GL_ARRAY_BUFFER, instanced->vbo_);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  // Copy base mesh index data
  const auto &indices = base_mesh.indices();
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, instanced->ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
               indices.data(), GL_STATIC_DRAW);

  // Setup base vertex attributes (same as regular mesh)
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

  // Try to setup persistent mapped buffer, fallback to regular if not supported
  if (g_persistent_mapping_supported) {
    instanced->setup_persistent_buffer();
  } else {
    instanced->setup_instance_buffer();
  }

  glBindVertexArray(0);

  return instanced;
}

InstancedMesh::~InstancedMesh() {
  // Cleanup persistent mapping if active
  if (persistent_mapped_ && mapped_buffer_) {
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
#ifdef _WIN32
    if (glUnmapBuffer) {
      glUnmapBuffer(GL_ARRAY_BUFFER);
    }
#elif !defined(__APPLE__)
    // Linux
    typedef GLboolean (*PFNGLUNMAPBUFFERPROC)(GLenum);
    PFNGLUNMAPBUFFERPROC glUnmapBufferFunc =
        (PFNGLUNMAPBUFFERPROC)glfwGetProcAddress("glUnmapBuffer");
    if (glUnmapBufferFunc) {
      glUnmapBufferFunc(GL_ARRAY_BUFFER);
    }
#endif
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  if (sync_fence_) {
#ifdef _WIN32
    if (glDeleteSync)
      glDeleteSync(sync_fence_);
#elif !defined(__APPLE__)
    // Linux
    typedef void (*PFNGLDELETESYNCPROC)(GLsync);
    PFNGLDELETESYNCPROC glDeleteSyncFunc =
        (PFNGLDELETESYNCPROC)glfwGetProcAddress("glDeleteSync");
    if (glDeleteSyncFunc) {
      glDeleteSyncFunc((GLsync)sync_fence_);
    }
#endif
  }

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
  // Traditional approach - allocate mutable buffer
  instance_data_.reserve(max_instances_);

  glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
  glBufferData(GL_ARRAY_BUFFER, max_instances_ * sizeof(InstanceData), nullptr,
               GL_DYNAMIC_DRAW);

  // Setup instance attributes
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, position));
  glVertexAttribDivisor(4, 1);

  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, rotation));
  glVertexAttribDivisor(5, 1);

  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, scale));
  glVertexAttribDivisor(6, 1);

  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, color));
  glVertexAttribDivisor(7, 1);

  glEnableVertexAttribArray(8);
  glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, texture_index));
  glVertexAttribDivisor(8, 1);

  glBindBuffer(GL_ARRAY_BUFFER, 0);

  persistent_mapped_ = false;
  std::cout << "Using traditional buffer updates" << std::endl;
}

void InstancedMesh::setup_persistent_buffer() {
  // Modern approach - persistent mapped buffer
  instance_data_.reserve(max_instances_);

  glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);

  // Create immutable storage with persistent mapping flags
  GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT |
                     GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT;

  bool success = false;

#ifdef _WIN32
  if (glBufferStorage) {
    glBufferStorage(GL_ARRAY_BUFFER, max_instances_ * sizeof(InstanceData),
                    nullptr, flags);
    success = true;
  }
#elif defined(__APPLE__)
  std::cerr << "ERROR: setup_persistent_buffer called on macOS - this is a bug!"
            << std::endl;
  setup_instance_buffer();
  return;
#else
  // Linux - try to use glBufferStorage if available
  // We need to get the function pointer first
  typedef void (*PFNGLBUFFERSTORAGEPROC)(GLenum, GLsizeiptr, const void *,
                                         GLbitfield);
  PFNGLBUFFERSTORAGEPROC glBufferStorageFunc =
      (PFNGLBUFFERSTORAGEPROC)glfwGetProcAddress("glBufferStorage");

  if (glBufferStorageFunc) {
    glBufferStorageFunc(GL_ARRAY_BUFFER, max_instances_ * sizeof(InstanceData),
                        nullptr, flags);
    success = true;
  }
#endif

  if (!success) {
    std::cerr << "Failed to create persistent buffer storage, falling back"
              << std::endl;
    setup_instance_buffer();
    return;
  }

  // Map the buffer persistently
  void *mapped = nullptr;

#ifdef _WIN32
  if (glMapBufferRange) {
    mapped = glMapBufferRange(GL_ARRAY_BUFFER, 0,
                              max_instances_ * sizeof(InstanceData), flags);
  }
#else
  // Linux - get function pointer
  typedef void *(*PFNGLMAPBUFFERRANGEPROC)(GLenum, GLintptr, GLsizeiptr,
                                           GLbitfield);
  PFNGLMAPBUFFERRANGEPROC glMapBufferRangeFunc =
      (PFNGLMAPBUFFERRANGEPROC)glfwGetProcAddress("glMapBufferRange");

  if (glMapBufferRangeFunc) {
    mapped = glMapBufferRangeFunc(GL_ARRAY_BUFFER, 0,
                                  max_instances_ * sizeof(InstanceData), flags);
  }
#endif

  mapped_buffer_ = mapped;

  if (!mapped_buffer_) {
    std::cerr << "Failed to map buffer persistently, falling back to regular "
                 "buffer"
              << std::endl;
    setup_instance_buffer();
    return;
  }

  // Setup instance attributes (same as before)
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, position));
  glVertexAttribDivisor(4, 1);

  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, rotation));
  glVertexAttribDivisor(5, 1);

  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, scale));
  glVertexAttribDivisor(6, 1);

  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, color));
  glVertexAttribDivisor(7, 1);

  glEnableVertexAttribArray(8);
  glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                        (void *)offsetof(InstanceData, texture_index));
  glVertexAttribDivisor(8, 1);

  glBindBuffer(GL_ARRAY_BUFFER, 0);

  persistent_mapped_ = true;
  std::cout << "Using persistent mapped buffers (zero-copy updates)"
            << std::endl;
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

void InstancedMesh::wait_for_previous_frame() {
  if (!sync_fence_)
    return;

#ifdef _WIN32
  if (glClientWaitSync) {
    GLenum result = glClientWaitSync(sync_fence_, GL_SYNC_FLUSH_COMMANDS_BIT,
                                     1000000000); // 1 second timeout
    if (result == GL_WAIT_FAILED) {
      std::cerr << "Sync wait failed!" << std::endl;
    }
  }
  if (glDeleteSync) {
    glDeleteSync(sync_fence_);
  }
#elif defined(__APPLE__)
  // Should never reach here on macOS
  std::cerr << "ERROR: Fence sync on macOS - this shouldn't happen!"
            << std::endl;
#else
  // Linux - get function pointers
  typedef GLenum (*PFNGLCLIENTWAITSYNCPROC)(GLsync, GLbitfield, uint64_t);
  typedef void (*PFNGLDELETESYNCPROC)(GLsync);

  PFNGLCLIENTWAITSYNCPROC glClientWaitSyncFunc =
      (PFNGLCLIENTWAITSYNCPROC)glfwGetProcAddress("glClientWaitSync");
  PFNGLDELETESYNCPROC glDeleteSyncFunc =
      (PFNGLDELETESYNCPROC)glfwGetProcAddress("glDeleteSync");

  if (glClientWaitSyncFunc && glDeleteSyncFunc) {
    GLenum result =
        glClientWaitSyncFunc((GLsync)sync_fence_, GL_SYNC_FLUSH_COMMANDS_BIT,
                             1000000000); // 1 second timeout
    if (result == GL_WAIT_FAILED) {
      std::cerr << "Sync wait failed!" << std::endl;
    }
    glDeleteSyncFunc((GLsync)sync_fence_);
  }
#endif

  sync_fence_ = 0;
}

void InstancedMesh::update_persistent_buffer() {
  if (!needs_update_ || !mapped_buffer_)
    return;

  // Wait for previous frame to finish reading the buffer
  wait_for_previous_frame();

  // Direct memory write (zero-copy!)
  memcpy(mapped_buffer_, instance_data_.data(),
         instance_data_.size() * sizeof(InstanceData));

  // With GL_MAP_COHERENT_BIT, writes are automatically visible to GPU
  // No need for glFlushMappedBufferRange or memory barrier

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

void InstancedMesh::compute_culling(const Vec3 &camera_pos,
                                    const Vec3 &camera_dir, float fov_radians,
                                    float aspect_ratio, float near_plane,
                                    float far_plane) {
  RendererInstanced::compute_culling_for_instances(
      instance_data_, camera_pos, camera_dir, fov_radians, aspect_ratio,
      near_plane, far_plane);
}

void InstancedMesh::draw() const {
  if (instance_count_ == 0)
    return;

  // Update buffer if needed
  if (persistent_mapped_) {
    const_cast<InstancedMesh *>(this)->update_persistent_buffer();
  } else {
    const_cast<InstancedMesh *>(this)->update_instance_buffer();
  }

  glBindVertexArray(vao_);
  glDrawElementsInstanced(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, 0,
                          instance_count_);
  glBindVertexArray(0);

  // Place fence for next frame's synchronization
  if (persistent_mapped_ && needs_update_) {
#ifdef _WIN32
    if (glFenceSync) {
      const_cast<InstancedMesh *>(this)->sync_fence_ =
          glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
#elif !defined(__APPLE__)
    // Linux
    typedef GLsync (*PFNGLFENCESYNCPROC)(GLenum, GLbitfield);
    PFNGLFENCESYNCPROC glFenceSyncFunc =
        (PFNGLFENCESYNCPROC)glfwGetProcAddress("glFenceSync");
    if (glFenceSyncFunc) {
      const_cast<InstancedMesh *>(this)->sync_fence_ =
          (uint64_t)glFenceSyncFunc(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
#endif
  }
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

  std::vector<InstanceData> instances = mesh.get_visible_instances();
  compute_culling_for_instances(
      instances, renderer.camera().position,
      (renderer.camera().target - renderer.camera().position).normalized(),
      glm::radians(renderer.camera().fov),
      renderer.window_width() / static_cast<float>(renderer.window_height()),
      renderer.camera().near_clip, // Added near_clip
      renderer.camera().far_clip);

  Shader *shader = renderer.get_shader(renderer.instanced_shader());
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
  if (base_material.texture_array != INVALID_TEXTURE_ARRAY) {
    renderer.bind_texture_array(base_material.texture_array, 0);
    shader->set_int("uTextureArray", 0);
    shader->set_int("useTextureArray", 1);
  } else {
    shader->set_int("useTextureArray", 0);
  }

  // Draw all instances
  mesh.draw();

  shader->unbind();
}

void RendererInstanced::compute_culling_for_instances(
    std::vector<InstanceData> &instances, const Vec3 &camera_pos,
    const Vec3 &camera_dir, float fov_radians, float aspect_ratio,
    float near_plane, // Added near_plane parameter
    float far_plane) {
  // Compute view frustum planes
  std::vector<Vec4> frustum_planes(6);

  float half_vfov = fov_radians * 0.5f;
  float half_hfov = std::atan(std::tan(half_vfov) * aspect_ratio);

  Vec3 near_center = camera_pos + camera_dir * near_plane;
  Vec3 far_center = camera_pos + camera_dir * far_plane;

  float near_height = 2.0f * std::tan(half_vfov) * near_plane;
  float near_width = near_height * aspect_ratio;
  float far_height = 2.0f * std::tan(half_vfov) * far_plane;
  float far_width = far_height * aspect_ratio;

  const Vec3 up = {0, 1, 0}; // Assume world up
  Vec3 right = glm::cross(camera_dir.to_glm(), up.to_glm());

  // Compute frustum planes
  // (near, far, left, right, top, bottom)
  for (auto &instance : instances) {
    // Distance from camera
    Vec3 to_instance = instance.position - camera_pos;
    float dist_to_instance = to_instance.length();

    // Skip culling for very close instances or with infinite radius
    if (dist_to_instance <= near_plane || instance._culling_radius <= 0) {
      instance._is_visible = true;
      continue;
    }

    // Simple frustum culling: check if instance's bounding sphere intersects
    // view frustum
    bool in_frustum = true;

    // Rough distance checks
    if (dist_to_instance > far_plane + instance._culling_radius) {
      in_frustum = false;
    }

    // You can add more precise plane checks here if needed

    instance._is_visible = in_frustum;
  }
}

// ============================================================================
// Helper Functions for Instance Generation
// ============================================================================

void RendererInstanced::assign_texture_indices(
    std::vector<InstanceData> &instances, int num_textures) {
  if (num_textures <= 0)
    return;

  for (size_t i = 0; i < instances.size(); ++i) {
    instances[i].texture_index = static_cast<float>(i % num_textures);
  }
}

void RendererInstanced::assign_random_texture_indices(
    std::vector<InstanceData> &instances, int num_textures) {
  if (num_textures <= 0)
    return;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, num_textures - 1);

  for (auto &instance : instances) {
    instance.texture_index = static_cast<float>(dis(gen));
  }
}

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

      // Texture index will be 0 by default, can be set later
      data.texture_index = 0.0f;

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

    data.texture_index = 0.0f;

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
    data.texture_index = 0.0f;

    instances.push_back(data);
  }

  return instances;
}

} // namespace pixel::renderer3d
