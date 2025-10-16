#include "pixel/renderer3d/renderer_instanced.hpp"
#include <GLFW/glfw3.h>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <random>

// ============================================================================
// Platform-specific OpenGL Function Loading
// ============================================================================

// Define constants for all platforms
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GL_COMMAND_BARRIER_BIT 0x00000040
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#endif

// Function pointer types
typedef void(APIENTRYP PFNGLDISPATCHCOMPUTEPROC)(GLuint, GLuint, GLuint);
typedef void(APIENTRYP PFNGLMEMORYBARRIERPROC)(GLbitfield);
typedef void(APIENTRYP PFNGLDRAWELEMENTSINDIRECTPROC)(GLenum, GLenum,
                                                      const void *);
typedef void(APIENTRYP PFNGLBINDBUFFERBASEPROC)(GLenum, GLuint, GLuint);

// Global function pointers - NULL if not supported
static PFNGLDISPATCHCOMPUTEPROC glDispatchCompute = nullptr;
static PFNGLMEMORYBARRIERPROC glMemoryBarrier = nullptr;

static bool g_compute_shaders_supported = false;

// Platform-specific instancing function loading
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

// ============================================================================
// Compute Shader Function Loading
// ============================================================================

static bool check_opengl_version(int major, int minor) {
  const char *version_str = (const char *)glGetString(GL_VERSION);
  if (!version_str)
    return false;

  int gl_major = 0, gl_minor = 0;
  sscanf(version_str, "%d.%d", &gl_major, &gl_minor);

  if (gl_major > major)
    return true;
  if (gl_major == major && gl_minor >= minor)
    return true;
  return false;
}

static void load_compute_functions() {
  static bool loaded = false;
  if (loaded)
    return;
  loaded = true;

  const char *version_str = (const char *)glGetString(GL_VERSION);
  std::cout << "OpenGL Version: " << version_str << std::endl;

  // Check if OpenGL 4.3+ (required for compute shaders)
  if (!check_opengl_version(4, 3)) {
    std::cout << "OpenGL 4.3+ required for compute shaders (have "
              << version_str << ")" << std::endl;
    g_compute_shaders_supported = false;
    return;
  }

  // Try to load function pointers
  glDispatchCompute =
      (PFNGLDISPATCHCOMPUTEPROC)glfwGetProcAddress("glDispatchCompute");
  glMemoryBarrier =
      (PFNGLMEMORYBARRIERPROC)glfwGetProcAddress("glMemoryBarrier");

  // Verify all required functions loaded
  g_compute_shaders_supported =
      (glDispatchCompute != nullptr && glMemoryBarrier != nullptr &&
       glDrawElementsIndirect != nullptr && glBindBufferBase != nullptr);

  if (g_compute_shaders_supported) {
    std::cout << "Compute shaders: SUPPORTED" << std::endl;
  } else {
    std::cout << "Compute shaders: NOT SUPPORTED (missing functions)"
              << std::endl;
  }
}

// ============================================================================
// Compute Shader Source Code
// ============================================================================

static const char *culling_compute_shader_src = R"(
#version 430 core

layout(local_size_x = 256) in;

// Input: all instances
struct InstanceData {
  vec3 position;
  vec3 rotation;
  vec3 scale;
  vec4 color;
  float texture_index;
  float culling_radius;
  vec2 _padding;
};

layout(std430, binding = 0) readonly buffer InstanceBuffer {
  InstanceData instances[];
};

// Output: indices of visible instances
layout(std430, binding = 1) writeonly buffer VisibleInstancesBuffer {
  uint visible_instances[];
};

// Output: draw command (updated atomically)
struct DrawCommand {
  uint index_count;
  uint instance_count;
  uint first_index;
  uint base_vertex;
  uint base_instance;
};

layout(std430, binding = 2) buffer DrawCommandBuffer {
  DrawCommand draw_command;
};

// Uniforms: frustum planes and camera info
uniform mat4 view_projection;
uniform vec4 frustum_planes[6];  // left, right, bottom, top, near, far
uniform uint total_instances;
uniform uint base_index_count;

// Check if sphere intersects frustum
bool is_visible(vec3 center, float radius) {
  // Test against all 6 frustum planes
  for (int i = 0; i < 6; i++) {
    vec4 plane = frustum_planes[i];
    float distance = dot(plane.xyz, center) + plane.w;
    
    if (distance < -radius) {
      return false;  // Completely outside this plane
    }
  }
  return true;
}

void main() {
  uint index = gl_GlobalInvocationID.x;
  
  if (index >= total_instances) {
    return;
  }
  
  InstanceData inst = instances[index];
  
  // Transform position to world space
  vec3 world_pos = inst.position;
  
  // Calculate bounding sphere radius considering scale
  float max_scale = max(max(inst.scale.x, inst.scale.y), inst.scale.z);
  float effective_radius = inst.culling_radius * max_scale;
  
  // Perform frustum culling
  if (is_visible(world_pos, effective_radius)) {
    // Atomically increment instance count and get output index
    uint output_index = atomicAdd(draw_command.instance_count, 1);
    
    // Store visible instance index
    visible_instances[output_index] = index;
  }
}
)";

namespace pixel::renderer3d {

// ============================================================================
// Helper Functions
// ============================================================================

// Extract frustum planes from view-projection matrix
static void extract_frustum_planes(const float *vp_matrix, Vec4 planes[6]) {
  const float *m = vp_matrix;

  // Left
  planes[0].x = m[3] + m[0];
  planes[0].y = m[7] + m[4];
  planes[0].z = m[11] + m[8];
  planes[0].w = m[15] + m[12];

  // Right
  planes[1].x = m[3] - m[0];
  planes[1].y = m[7] - m[4];
  planes[1].z = m[11] - m[8];
  planes[1].w = m[15] - m[12];

  // Bottom
  planes[2].x = m[3] + m[1];
  planes[2].y = m[7] + m[5];
  planes[2].z = m[11] + m[9];
  planes[2].w = m[15] + m[13];

  // Top
  planes[3].x = m[3] - m[1];
  planes[3].y = m[7] - m[5];
  planes[3].z = m[11] - m[9];
  planes[3].w = m[15] - m[13];

  // Near
  planes[4].x = m[3] + m[2];
  planes[4].y = m[7] + m[6];
  planes[4].z = m[11] + m[10];
  planes[4].w = m[15] + m[14];

  // Far
  planes[5].x = m[3] - m[2];
  planes[5].y = m[7] - m[6];
  planes[5].z = m[11] - m[10];
  planes[5].w = m[15] - m[14];

  // Normalize planes
  for (int i = 0; i < 6; i++) {
    float len =
        std::sqrt(planes[i].x * planes[i].x + planes[i].y * planes[i].y +
                  planes[i].z * planes[i].z);
    if (len > 0) {
      planes[i].x /= len;
      planes[i].y /= len;
      planes[i].z /= len;
      planes[i].w /= len;
    }
  }
}

// ============================================================================
// InstancedMesh Implementation
// ============================================================================

std::unique_ptr<InstancedMesh> InstancedMesh::create(const Mesh &base_mesh,
                                                     size_t max_instances) {
  load_instancing_functions();

  auto instanced = std::unique_ptr<InstancedMesh>(new InstancedMesh());
  instanced->max_instances_ = max_instances;
  instanced->index_count_ = base_mesh.index_count();

  // Setup VAO/VBO/EBO
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

  // Setup instance buffer (persistent or regular)
  if (g_persistent_mapping_supported) {
    instanced->setup_persistent_buffer();
  } else {
    instanced->setup_instance_buffer();
  }

  glBindVertexArray(0);

  // Setup GPU culling if available
  instanced->setup_gpu_culling_buffers();

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

  // Cleanup GPU culling resources
  if (culling_compute_shader_)
    glDeleteProgram(culling_compute_shader_);
  if (visible_instances_ssbo_)
    glDeleteBuffers(1, &visible_instances_ssbo_);
  if (draw_command_buffer_)
    glDeleteBuffers(1, &draw_command_buffer_);
  if (instance_input_ssbo_)
    glDeleteBuffers(1, &instance_input_ssbo_);
  if (culling_vao_)
    glDeleteVertexArrays(1, &culling_vao_);

  // Cleanup regular resources
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

void InstancedMesh::setup_gpu_culling_buffers() {
  load_compute_functions();

  // Early exit if compute shaders not supported
  if (!g_compute_shaders_supported) {
    std::cout << "GPU culling: DISABLED (compute shaders not available)"
              << std::endl;
    gpu_culling_enabled_ = false;
    return;
  }

  // Verify function pointers are valid before using them
  if (!glDispatchCompute || !glMemoryBarrier || !glDrawElementsIndirect ||
      !glBindBufferBase) {
    std::cerr << "ERROR: Compute shader functions not loaded properly"
              << std::endl;
    gpu_culling_enabled_ = false;
    return;
  }

  // Create and compile compute shader
  GLuint compute_shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(compute_shader, 1, &culling_compute_shader_src, nullptr);
  glCompileShader(compute_shader);

  GLint success;
  glGetShaderiv(compute_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[512];
    glGetShaderInfoLog(compute_shader, 512, nullptr, log);
    std::cerr << "Compute shader compilation failed:\n" << log << std::endl;
    glDeleteShader(compute_shader);
    gpu_culling_enabled_ = false;
    return;
  }

  culling_compute_shader_ = glCreateProgram();
  glAttachShader(culling_compute_shader_, compute_shader);
  glLinkProgram(culling_compute_shader_);

  glGetProgramiv(culling_compute_shader_, GL_LINK_STATUS, &success);
  if (!success) {
    char log[512];
    glGetProgramInfoLog(culling_compute_shader_, 512, nullptr, log);
    std::cerr << "Compute shader linking failed:\n" << log << std::endl;
    glDeleteShader(compute_shader);
    glDeleteProgram(culling_compute_shader_);
    gpu_culling_enabled_ = false;
    return;
  }

  glDeleteShader(compute_shader);

  // Create SSBOs
  glGenBuffers(1, &instance_input_ssbo_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, instance_input_ssbo_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, max_instances_ * sizeof(InstanceData),
               nullptr, GL_DYNAMIC_DRAW);

  glGenBuffers(1, &visible_instances_ssbo_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, visible_instances_ssbo_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, max_instances_ * sizeof(uint32_t),
               nullptr, GL_DYNAMIC_DRAW);

  // Create draw command buffer
  glGenBuffers(1, &draw_command_buffer_);
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, draw_command_buffer_);

  DrawCommand initial_command;
  initial_command.index_count = index_count_;
  initial_command.instance_count = 0;
  initial_command.first_index = 0;
  initial_command.base_vertex = 0;
  initial_command.base_instance = 0;

  glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(DrawCommand), &initial_command,
               GL_DYNAMIC_DRAW);

  // Create VAO for indirect rendering with visible instances
  glGenVertexArrays(1, &culling_vao_);
  glBindVertexArray(culling_vao_);

  // Bind base mesh buffers
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);

  // Setup base vertex attributes
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

  // Bind instance buffer for rendering
  glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);

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

  glBindVertexArray(0);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

  gpu_culling_enabled_ = true;
  std::cout << "GPU culling: ENABLED" << std::endl;
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

void InstancedMesh::compute_gpu_culling(const Renderer &renderer) {
  // Early exit if GPU culling not available or no instances
  if (!gpu_culling_enabled_ || instance_count_ == 0) {
    return;
  }

  // Critical safety check: If compute functions don't exist, we CANNOT proceed
  // This prevents calling nullptr function pointers which would crash
  if (!glBindBufferBase || !glDispatchCompute || !glMemoryBarrier) {
    // On systems without OpenGL 4.3+ (like macOS), these will be nullptr
    // Don't attempt to call them - just return and use fallback rendering
    return;
  }

  // === Safe to proceed - all required functions are available ===

  // Update instance data buffer
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, instance_input_ssbo_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  instance_count_ * sizeof(InstanceData),
                  instance_data_.data());

  // Reset draw command
  DrawCommand reset_command;
  reset_command.index_count = index_count_;
  reset_command.instance_count = 0;
  reset_command.first_index = 0;
  reset_command.base_vertex = 0;
  reset_command.base_instance = 0;

  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, draw_command_buffer_);
  glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawCommand),
                  &reset_command);

  // Get view-projection matrix
  float view[16], proj[16];
  renderer.camera().get_view_matrix(view);
  renderer.camera().get_projection_matrix(proj, renderer.window_width(),
                                          renderer.window_height());

  glm::mat4 view_mat, proj_mat;
  memcpy(&view_mat[0][0], view, 16 * sizeof(float));
  memcpy(&proj_mat[0][0], proj, 16 * sizeof(float));
  glm::mat4 vp = proj_mat * view_mat;

  // Extract frustum planes
  Vec4 frustum_planes[6];
  extract_frustum_planes(glm::value_ptr(vp), frustum_planes);

  // Bind compute shader and set uniforms
  glUseProgram(culling_compute_shader_);

  GLint vp_loc =
      glGetUniformLocation(culling_compute_shader_, "view_projection");
  glUniformMatrix4fv(vp_loc, 1, GL_FALSE, glm::value_ptr(vp));

  GLint planes_loc =
      glGetUniformLocation(culling_compute_shader_, "frustum_planes");
  glUniform4fv(planes_loc, 6, (float *)frustum_planes);

  GLint total_loc =
      glGetUniformLocation(culling_compute_shader_, "total_instances");
  glUniform1ui(total_loc, instance_count_);

  GLint index_loc =
      glGetUniformLocation(culling_compute_shader_, "base_index_count");
  glUniform1ui(index_loc, index_count_);

  // === OpenGL 4.3+ functions - verified to exist above ===

  // Bind SSBOs
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instance_input_ssbo_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, visible_instances_ssbo_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, draw_command_buffer_);

  // Dispatch compute shader
  uint32_t work_groups = (instance_count_ + 255) / 256;
  glDispatchCompute(work_groups, 1, 1);

  // Memory barrier to ensure compute shader writes are visible
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);

  glUseProgram(0);
}

uint32_t InstancedMesh::get_visible_count() const {
  if (!gpu_culling_enabled_) {
    return instance_count_;
  }

  // Safety check for indirect draw function
  if (!glDrawElementsIndirect) {
    return instance_count_;
  }

  DrawCommand cmd;
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, draw_command_buffer_);
  glGetBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, sizeof(DrawCommand), &cmd);
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

  return cmd.instance_count;
}

void InstancedMesh::draw() const {
  if (instance_count_ == 0)
    return;

  if (gpu_culling_enabled_ && glDrawElementsIndirect) {
    // GPU culling path: use indirect draw
    glBindVertexArray(culling_vao_);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, draw_command_buffer_);
    glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
  } else {
    // Fallback: regular instanced draw (CPU path or unsupported GPU culling)
    if (persistent_mapped_) {
      const_cast<InstancedMesh *>(this)->update_persistent_buffer();
    } else {
      const_cast<InstancedMesh *>(this)->update_instance_buffer();
    }

    glBindVertexArray(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, 0,
                            instance_count_);
    glBindVertexArray(0);
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

  // Perform GPU culling before rendering (safe on all platforms)
  const_cast<InstancedMesh &>(mesh).compute_gpu_culling(renderer);

  Shader *shader = renderer.get_shader(renderer.instanced_shader());
  if (!shader)
    return;

  shader->bind();

  float view[16], proj[16];
  renderer.camera().get_view_matrix(view);
  renderer.camera().get_projection_matrix(proj, renderer.window_width(),
                                          renderer.window_height());

  shader->set_mat4("view", view);
  shader->set_mat4("projection", proj);
  shader->set_vec3("lightPos", {5, 10, 5});
  shader->set_vec3("viewPos", renderer.camera().position);

  if (base_material.texture_array != INVALID_TEXTURE_ARRAY) {
    renderer.bind_texture_array(base_material.texture_array, 0);
    shader->set_int("uTextureArray", 0);
    shader->set_int("useTextureArray", 1);
  } else {
    shader->set_int("useTextureArray", 0);
  }

  mesh.draw();

  shader->unbind();
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

      // Default culling radius for unit cube
      data.culling_radius = 0.866f; // sqrt(3)/2

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
    data.culling_radius = 0.866f;

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
    data.culling_radius = 0.866f * s; // Scale culling radius with object size

    instances.push_back(data);
  }

  return instances;
}

} // namespace pixel::renderer3d
