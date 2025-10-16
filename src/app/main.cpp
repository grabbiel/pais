#include "pixel/core/clock.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/platform/resources.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/lod.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>

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

using namespace pixel::renderer3d;

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  pixel::platform::WindowSpec ws;
  ws.w = 1920;
  ws.h = 1080;
  ws.title = "Pixel-Life - Multi-LOD System Demo";

  auto r = Renderer::create(ws);

  // ============================================================================
  // Create LOD Meshes
  // ============================================================================

  std::cout << "\n========================================" << std::endl;
  std::cout << "Creating LOD Meshes..." << std::endl;
  std::cout << "========================================\n" << std::endl;

  // Cube LODs
  auto cube_high = RendererLOD::create_cube_high_detail(*r, 1.0f);
  auto cube_medium = RendererLOD::create_cube_medium_detail(*r, 1.0f);
  auto cube_low = RendererLOD::create_cube_low_detail(*r, 1.0f);

  std::cout << "Cube mesh vertices:" << std::endl;
  std::cout << "  High:   " << cube_high->vertex_count() << " vertices"
            << std::endl;
  std::cout << "  Medium: " << cube_medium->vertex_count() << " vertices"
            << std::endl;
  std::cout << "  Low:    " << cube_low->vertex_count() << " vertices"
            << std::endl;

  // Sphere LODs
  auto sphere_high = RendererLOD::create_sphere_high_detail(*r, 0.5f);
  auto sphere_medium = RendererLOD::create_sphere_medium_detail(*r, 0.5f);
  auto sphere_low = RendererLOD::create_sphere_low_detail(*r, 0.5f);

  std::cout << "\nSphere mesh vertices:" << std::endl;
  std::cout << "  High:   " << sphere_high->vertex_count() << " vertices"
            << std::endl;
  std::cout << "  Medium: " << sphere_medium->vertex_count() << " vertices"
            << std::endl;
  std::cout << "  Low:    " << sphere_low->vertex_count() << " vertices"
            << std::endl;

  // Floor
  auto floor_mesh = r->create_plane(200.0f, 200.0f, 40);

  // ============================================================================
  // Setup Camera
  // ============================================================================

  r->camera().position = {80, 50, 80};
  r->camera().target = {0, 0, 0};
  r->camera().mode = Camera::ProjectionMode::Perspective;
  r->camera().fov = 60.0f;
  r->camera().far_clip = 300.0f;

  // ============================================================================
  // Load Textures
  // ============================================================================

  std::vector<std::string> texture_paths = {
      pixel::platform::get_resource_file("assets/textures/brick.png"),
      pixel::platform::get_resource_file("assets/textures/stone.png"),
      pixel::platform::get_resource_file("assets/textures/wood.png"),
      pixel::platform::get_resource_file("assets/textures/metal.png"),
      pixel::platform::get_resource_file("assets/textures/grass.png"),
      pixel::platform::get_resource_file("assets/textures/dirt.png")};

  TextureArrayID tex_array = INVALID_TEXTURE_ARRAY;

  try {
    tex_array = r->load_texture_array(texture_paths);
    std::cout << "\n✓ Successfully loaded texture array!" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "\n✗ Falling back to procedural textures" << std::endl;
    const int TEX_SIZE = 128;
    const int NUM_TEXTURES = 6;
    tex_array = r->create_texture_array(TEX_SIZE, TEX_SIZE, NUM_TEXTURES);

    std::vector<uint8_t> tex_data(TEX_SIZE * TEX_SIZE * 4);
    for (int layer = 0; layer < NUM_TEXTURES; ++layer) {
      for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
          int idx = (y * TEX_SIZE + x) * 4;
          bool checker = ((x / 8) + (y / 8)) % 2 == 0;
          float base = checker ? 0.8f : 0.3f;
          float hue = layer / (float)NUM_TEXTURES;

          tex_data[idx + 0] =
              (uint8_t)(base * 255 * (0.5f + 0.5f * std::sin(hue * 6.28f)));
          tex_data[idx + 1] =
              (uint8_t)(base * 255 *
                        (0.5f + 0.5f * std::sin(hue * 6.28f + 2.0f)));
          tex_data[idx + 2] =
              (uint8_t)(base * 255 *
                        (0.5f + 0.5f * std::sin(hue * 6.28f + 4.0f)));
          tex_data[idx + 3] = 255;
        }
      }
      r->set_texture_array_layer(tex_array, layer, tex_data.data());
    }
  }

  auto array_info = r->get_texture_array_info(tex_array);
  std::cout << "Texture array: " << array_info.width << "x" << array_info.height
            << " with " << array_info.layers << " layers\n"
            << std::endl;

  // ============================================================================
  // Create LOD Instance System
  // ============================================================================

  const int GRID_SIZE = 80; // 80x80 = 6,400 instances
  const int MAX_INSTANCES = GRID_SIZE * GRID_SIZE;

  // Configure LOD distances
  LODConfig lod_config;
  lod_config.distance_high = 30.0f;   // Switch to medium after 30 units
  lod_config.distance_medium = 80.0f; // Switch to low after 80 units
  lod_config.distance_cull = 200.0f;  // Cull after 200 units
  lod_config.hysteresis = 2.0f;

  // Create LOD mesh for cubes
  auto lod_cubes =
      RendererLOD::create_lod_mesh(*cube_high, *cube_medium, *cube_low,
                                   MAX_INSTANCES / 3, // Max per LOD level
                                   lod_config);

  // Create LOD mesh for spheres (separate demo)
  auto lod_spheres = RendererLOD::create_lod_mesh(
      *sphere_high, *sphere_medium, *sphere_low, MAX_INSTANCES / 3, lod_config);

  std::cout << "\n========================================" << std::endl;
  std::cout << "LOD System Configuration:" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Total instances: " << MAX_INSTANCES << std::endl;
  std::cout << "High LOD distance: < " << lod_config.distance_high << " units"
            << std::endl;
  std::cout << "Medium LOD distance: " << lod_config.distance_high << " - "
            << lod_config.distance_medium << " units" << std::endl;
  std::cout << "Low LOD distance: " << lod_config.distance_medium << " - "
            << lod_config.distance_cull << " units" << std::endl;
  std::cout << "Culled beyond: " << lod_config.distance_cull << " units"
            << std::endl;
  std::cout << "GPU LOD: "
            << (lod_cubes->using_gpu_lod() ? "YES" : "NO (CPU fallback)")
            << std::endl;
  std::cout << "========================================\n" << std::endl;

  // Generate instance data
  auto instances =
      RendererInstanced::create_grid(GRID_SIZE, GRID_SIZE, 4.0f, 0.5f);

  // Set culling radii
  for (auto &inst : instances) {
    inst.culling_radius = 0.866f; // Bounding sphere for unit cube/sphere
  }

  RendererInstanced::assign_texture_indices(instances, array_info.layers);
  lod_cubes->set_instances(instances);

  // Create a second set of instances for spheres (offset)
  auto sphere_instances = instances;
  for (auto &inst : sphere_instances) {
    inst.position.x += GRID_SIZE * 2.0f; // Offset to the side
  }
  lod_spheres->set_instances(sphere_instances);

  // Floor material
  Material floor_mat;
  floor_mat.diffuse = Color(0.15f, 0.18f, 0.15f, 1.0f);

  // Material with texture array
  Material textured_mat;
  textured_mat.texture_array = tex_array;

  // ============================================================================
  // Main Loop Setup
  // ============================================================================

  const double dt = 1.0 / 60.0;
  double acc = 0.0, t0 = pixel::core::now_sec();
  float time_elapsed = 0.0f;

  bool mouse_was_pressed = false;
  bool show_stats = true;
  bool animate_instances = true;
  bool show_spheres = false; // Toggle between cubes and spheres

  // Performance tracking
  double last_fps_update = t0;
  int frame_count = 0;
  double fps = 0.0;
  LODMesh::LODStats last_stats;

  std::cout << "\nControls:" << std::endl;
  std::cout << "  Mouse drag: Orbit camera" << std::endl;
  std::cout << "  W/S: Zoom in/out" << std::endl;
  std::cout << "  A/D: Pan left/right" << std::endl;
  std::cout << "  Q/E: Pan up/down" << std::endl;
  std::cout << "  1/2: Switch projection mode" << std::endl;
  std::cout << "  Space: Toggle animation" << std::endl;
  std::cout << "  T: Toggle stats display" << std::endl;
  std::cout << "  M: Toggle mesh type (cubes/spheres)" << std::endl;
  std::cout << "  R: Reset camera" << std::endl;
  std::cout << "  +/-: Adjust LOD distances" << std::endl;
  std::cout << "  [ / ]: Adjust FOV" << std::endl;
  std::cout << "  ESC: Exit\n" << std::endl;

  // Main loop
  while (r->process_events()) {
    const double t1 = pixel::core::now_sec();
    acc += (t1 - t0);
    t0 = t1;

    // ========================================================================
    // Input Handling
    // ========================================================================

    const auto &input = r->input();

    // Camera controls
    if (input.key_pressed(KEY_W)) {
      r->camera().zoom(-1.0f);
    }
    if (input.key_pressed(KEY_S)) {
      r->camera().zoom(1.0f);
    }
    if (input.key_pressed(KEY_A)) {
      r->camera().pan(-1.0f, 0);
    }
    if (input.key_pressed(KEY_D)) {
      r->camera().pan(1.0f, 0);
    }

    // Projection mode
    if (input.key_pressed(KEY_1)) {
      r->camera().mode = Camera::ProjectionMode::Perspective;
      std::cout << "Perspective projection" << std::endl;
    }
    if (input.key_pressed(KEY_2)) {
      r->camera().mode = Camera::ProjectionMode::Orthographic;
      std::cout << "Orthographic projection" << std::endl;
    }

    // FOV adjustment
    static bool bracket_left_pressed = false;
    if (input.key_pressed('[')) {
      if (!bracket_left_pressed) {
        r->camera().fov = std::max(10.0f, r->camera().fov - 5.0f);
        std::cout << "FOV: " << r->camera().fov << "°" << std::endl;
        bracket_left_pressed = true;
      }
    } else {
      bracket_left_pressed = false;
    }

    static bool bracket_right_pressed = false;
    if (input.key_pressed(']')) {
      if (!bracket_right_pressed) {
        r->camera().fov = std::min(120.0f, r->camera().fov + 5.0f);
        std::cout << "FOV: " << r->camera().fov << "°" << std::endl;
        bracket_right_pressed = true;
      }
    } else {
      bracket_right_pressed = false;
    }

    // LOD distance adjustment
    static bool plus_pressed = false;
    if (input.key_pressed('=') || input.key_pressed('+')) {
      if (!plus_pressed) {
        lod_cubes->config().distance_high += 5.0f;
        lod_cubes->config().distance_medium += 5.0f;
        lod_spheres->config().distance_high += 5.0f;
        lod_spheres->config().distance_medium += 5.0f;
        std::cout << "LOD distances increased: High="
                  << lod_cubes->config().distance_high
                  << ", Medium=" << lod_cubes->config().distance_medium
                  << std::endl;
        plus_pressed = true;
      }
    } else {
      plus_pressed = false;
    }

    static bool minus_pressed = false;
    if (input.key_pressed('-') || input.key_pressed('_')) {
      if (!minus_pressed) {
        lod_cubes->config().distance_high =
            std::max(10.0f, lod_cubes->config().distance_high - 5.0f);
        lod_cubes->config().distance_medium =
            std::max(20.0f, lod_cubes->config().distance_medium - 5.0f);
        lod_spheres->config().distance_high = lod_cubes->config().distance_high;
        lod_spheres->config().distance_medium =
            lod_cubes->config().distance_medium;
        std::cout << "LOD distances decreased: High="
                  << lod_cubes->config().distance_high
                  << ", Medium=" << lod_cubes->config().distance_medium
                  << std::endl;
        minus_pressed = true;
      }
    } else {
      minus_pressed = false;
    }

    // Reset camera
    if (input.key_pressed(KEY_R)) {
      r->camera().position = {80, 50, 80};
      r->camera().target = {0, 0, 0};
      r->camera().fov = 60.0f;
      std::cout << "Camera reset" << std::endl;
    }

    // Toggle animation
    static bool space_pressed = false;
    if (input.key_pressed(KEY_SPACE)) {
      if (!space_pressed) {
        animate_instances = !animate_instances;
        std::cout << "Animation: " << (animate_instances ? "ON" : "OFF")
                  << std::endl;
        space_pressed = true;
      }
    } else {
      space_pressed = false;
    }

    // Toggle mesh type
    static bool m_pressed = false;
    if (input.key_pressed('M')) {
      if (!m_pressed) {
        show_spheres = !show_spheres;
        std::cout << "Showing: " << (show_spheres ? "SPHERES" : "CUBES")
                  << std::endl;
        m_pressed = true;
      }
    } else {
      m_pressed = false;
    }

    // Toggle stats
    static bool t_pressed = false;
    if (input.key_pressed('T')) {
      if (!t_pressed) {
        show_stats = !show_stats;
        t_pressed = true;
      }
    } else {
      t_pressed = false;
    }

    if (input.key_pressed(KEY_ESCAPE)) {
      break;
    }

    // Mouse orbit
    if (input.mouse_pressed(0)) {
      if (!mouse_was_pressed) {
        mouse_was_pressed = true;
      } else {
        float dx = static_cast<float>(input.mouse_delta_x);
        float dy = static_cast<float>(input.mouse_delta_y);
        r->camera().orbit(dx * 0.3f, dy * 0.3f);
      }
    } else {
      mouse_was_pressed = false;
    }

    // ========================================================================
    // Fixed Timestep Update
    // ========================================================================

    while (acc >= dt) {
      if (animate_instances) {
        time_elapsed += static_cast<float>(dt);

        // Animate instances with wave motion
        for (size_t i = 0; i < instances.size(); ++i) {
          int x = i % GRID_SIZE;
          int z = i / GRID_SIZE;

          float phase = (x + z) * 0.08f;
          instances[i].position.y =
              0.5f + 0.4f * std::sin(time_elapsed * 1.5f + phase);
          instances[i].rotation.y = time_elapsed * 25.0f + phase * 15.0f;

          // Vary scale
          float scale = 1.0f + 0.15f * std::sin(time_elapsed * 1.2f + phase);
          instances[i].scale = {scale, scale, scale};
          instances[i].culling_radius = 0.866f * scale;
        }

        lod_cubes->set_instances(instances);

        // Update spheres too
        for (size_t i = 0; i < sphere_instances.size(); ++i) {
          sphere_instances[i] = instances[i];
          sphere_instances[i].position.x += GRID_SIZE * 2.0f;
        }
        lod_spheres->set_instances(sphere_instances);
      }

      acc -= dt;
    }

    // ========================================================================
    // Performance Stats
    // ========================================================================

    frame_count++;
    if (t1 - last_fps_update >= 1.0) {
      fps = frame_count / (t1 - last_fps_update);
      frame_count = 0;
      last_fps_update = t1;

      // Get LOD statistics
      last_stats =
          show_spheres ? lod_spheres->get_stats() : lod_cubes->get_stats();

      if (show_stats) {
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "\rFPS: " << fps
                  << " | Total: " << last_stats.total_instances
                  << " | High: " << last_stats.instances_per_lod[0] << " ("
                  << last_stats.visible_per_lod[0] << " vis)"
                  << " | Med: " << last_stats.instances_per_lod[1] << " ("
                  << last_stats.visible_per_lod[1] << " vis)"
                  << " | Low: " << last_stats.instances_per_lod[2] << " ("
                  << last_stats.visible_per_lod[2] << " vis)"
                  << " | Culled: " << last_stats.culled << "        "
                  << std::flush;
      }
    }

    // ========================================================================
    // Rendering
    // ========================================================================

    r->begin_frame(Color(0.05f, 0.05f, 0.08f, 1.0f));

    // Draw floor
    r->draw_mesh(*floor_mesh, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, floor_mat);

    // Draw LOD instances
    if (show_spheres) {
      RendererLOD::draw_lod(*r, *lod_spheres, textured_mat);
    } else {
      RendererLOD::draw_lod(*r, *lod_cubes, textured_mat);
    }

    r->end_frame();
  }

  // ============================================================================
  // Cleanup & Final Stats
  // ============================================================================

  std::cout << "\n\nShutting down..." << std::endl;
  std::cout << "\nFinal LOD Statistics:" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Total instances: " << last_stats.total_instances << std::endl;
  std::cout << "High LOD: " << last_stats.instances_per_lod[0] << " ("
            << (100.0f * last_stats.instances_per_lod[0] /
                last_stats.total_instances)
            << "%)" << std::endl;
  std::cout << "Medium LOD: " << last_stats.instances_per_lod[1] << " ("
            << (100.0f * last_stats.instances_per_lod[1] /
                last_stats.total_instances)
            << "%)" << std::endl;
  std::cout << "Low LOD: " << last_stats.instances_per_lod[2] << " ("
            << (100.0f * last_stats.instances_per_lod[2] /
                last_stats.total_instances)
            << "%)" << std::endl;
  std::cout << "Culled: " << last_stats.culled << " ("
            << (100.0f * last_stats.culled / last_stats.total_instances) << "%)"
            << std::endl;

  uint32_t total_visible = last_stats.visible_per_lod[0] +
                           last_stats.visible_per_lod[1] +
                           last_stats.visible_per_lod[2];
  std::cout << "\nRendering efficiency:" << std::endl;
  std::cout << "  Visible instances: " << total_visible << "/"
            << last_stats.total_instances << " ("
            << (100.0f * total_visible / last_stats.total_instances) << "%)"
            << std::endl;
  std::cout << "  Culled by frustum+distance: "
            << (last_stats.total_instances - total_visible) << " ("
            << (100.0f * (last_stats.total_instances - total_visible) /
                last_stats.total_instances)
            << "%)" << std::endl;
  std::cout << "========================================" << std::endl;

  return 0;
}
