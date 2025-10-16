#include "pixel/core/clock.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/platform/resources.hpp"
#include "pixel/renderer3d/renderer.hpp"
#include "pixel/renderer3d/renderer_instanced.hpp"
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
  ws.title = "Pixel-Life - GPU Frustum Culling Demo";

  auto r = Renderer::create(ws);

  auto cube_mesh = r->create_cube(1.0f);
  auto floor_mesh = r->create_plane(100.0f, 100.0f, 20);

  // Setup camera
  r->camera().position = {50, 30, 50};
  r->camera().target = {0, 0, 0};
  r->camera().mode = Camera::ProjectionMode::Perspective;
  r->camera().fov = 60.0f;
  r->camera().far_clip = 200.0f;

  // Load or create textures
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
    std::cout << "✓ Successfully loaded texture array!" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "✗ Falling back to procedural textures" << std::endl;
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
            << " with " << array_info.layers << " layers" << std::endl;

  // ============================================================================
  // CREATE LARGE INSTANCED MESH FOR CULLING DEMO
  // ============================================================================

  const int GRID_SIZE = 100; // 100x100 = 10,000 instances
  const int MAX_INSTANCES = GRID_SIZE * GRID_SIZE;

  auto instanced_cubes =
      RendererInstanced::create_instanced_mesh(*cube_mesh, MAX_INSTANCES);

  std::cout << "\n========================================" << std::endl;
  std::cout << "GPU Culling Demo Configuration:" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Total instances: " << MAX_INSTANCES << std::endl;
  std::cout << "Persistent mapping: "
            << (instanced_cubes->using_persistent_mapping() ? "YES" : "NO")
            << std::endl;
  std::cout << "GPU culling: "
            << (instanced_cubes->using_gpu_culling() ? "YES" : "NO")
            << std::endl;
  std::cout << "========================================\n" << std::endl;

  // Generate instances in a large grid
  auto instances =
      RendererInstanced::create_grid(GRID_SIZE, GRID_SIZE, 3.0f, 0.5f);

  // Set culling radius for each instance (based on scale)
  for (auto &inst : instances) {
    inst.culling_radius = 0.866f; // sqrt(3)/2 for unit cube diagonal
  }

  RendererInstanced::assign_texture_indices(instances, array_info.layers);
  instanced_cubes->set_instances(instances);

  // Floor material
  Material floor_mat;
  floor_mat.diffuse = Color(0.2f, 0.25f, 0.2f, 1.0f);

  // Material with texture array
  Material textured_mat;
  textured_mat.texture_array = tex_array;

  const double dt = 1.0 / 60.0;
  double acc = 0.0, t0 = pixel::core::now_sec();
  float time_elapsed = 0.0f;

  bool mouse_was_pressed = false;
  bool show_stats = true;
  bool animate_instances = true;

  // Performance tracking
  double last_fps_update = t0;
  int frame_count = 0;
  double fps = 0.0;
  uint32_t last_visible_count = 0;

  std::cout << "\nControls:" << std::endl;
  std::cout << "  Mouse drag: Orbit camera" << std::endl;
  std::cout << "  W/S: Zoom in/out" << std::endl;
  std::cout << "  A/D: Pan left/right" << std::endl;
  std::cout << "  Q/E: Pan up/down" << std::endl;
  std::cout << "  1/2: Switch projection mode" << std::endl;
  std::cout << "  Space: Toggle animation" << std::endl;
  std::cout << "  T: Toggle stats display" << std::endl;
  std::cout << "  R: Reset camera" << std::endl;
  std::cout << "  +/-: Adjust FOV" << std::endl;
  std::cout << "  ESC: Exit" << std::endl;

  // Main loop
  while (r->process_events()) {
    const double t1 = pixel::core::now_sec();
    acc += (t1 - t0);
    t0 = t1;

    // Handle input
    const auto &input = r->input();

    // Camera controls
    if (input.key_pressed(KEY_W)) {
      r->camera().zoom(-0.5f);
    }
    if (input.key_pressed(KEY_S)) {
      r->camera().zoom(0.5f);
    }
    if (input.key_pressed(KEY_A)) {
      r->camera().pan(-0.5f, 0);
    }
    if (input.key_pressed(KEY_D)) {
      r->camera().pan(0.5f, 0);
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
    static bool plus_pressed = false;
    if (input.key_pressed('=') || input.key_pressed('+')) {
      if (!plus_pressed) {
        r->camera().fov = std::min(120.0f, r->camera().fov + 5.0f);
        std::cout << "FOV: " << r->camera().fov << "°" << std::endl;
        plus_pressed = true;
      }
    } else {
      plus_pressed = false;
    }

    static bool minus_pressed = false;
    if (input.key_pressed('-') || input.key_pressed('_')) {
      if (!minus_pressed) {
        r->camera().fov = std::max(10.0f, r->camera().fov - 5.0f);
        std::cout << "FOV: " << r->camera().fov << "°" << std::endl;
        minus_pressed = true;
      }
    } else {
      minus_pressed = false;
    }

    // Reset camera
    if (input.key_pressed(KEY_R)) {
      r->camera().position = {50, 30, 50};
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

    // Fixed timestep update
    while (acc >= dt) {
      if (animate_instances) {
        time_elapsed += static_cast<float>(dt);

        // Animate instances with wave motion
        for (size_t i = 0; i < instances.size(); ++i) {
          int x = i % GRID_SIZE;
          int z = i / GRID_SIZE;

          float phase = (x + z) * 0.1f;
          instances[i].position.y =
              0.5f + 0.3f * std::sin(time_elapsed * 2.0f + phase);
          instances[i].rotation.y = time_elapsed * 30.0f + phase * 20.0f;

          // Vary scale slightly
          float scale = 1.0f + 0.2f * std::sin(time_elapsed * 1.5f + phase);
          instances[i].scale = {scale, scale, scale};

          // Update culling radius based on scale
          instances[i].culling_radius = 0.866f * scale;
        }

        instanced_cubes->set_instances(instances);
      }

      acc -= dt;
    }

    // FPS calculation
    frame_count++;
    if (t1 - last_fps_update >= 1.0) {
      fps = frame_count / (t1 - last_fps_update);
      frame_count = 0;
      last_fps_update = t1;

      // Get visible instance count after culling
      last_visible_count = instanced_cubes->get_visible_count();

      if (show_stats) {
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "\rFPS: " << fps << " | Instances: " << last_visible_count
                  << "/" << MAX_INSTANCES << " ("
                  << (100.0f * last_visible_count / MAX_INSTANCES)
                  << "% visible)"
                  << " | Cam: (" << r->camera().position.x << ", "
                  << r->camera().position.y << ", " << r->camera().position.z
                  << ")" << std::flush;
      }
    }

    // Rendering
    r->begin_frame(Color(0.05f, 0.05f, 0.08f, 1.0f));

    // Draw floor
    r->draw_mesh(*floor_mesh, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, floor_mat);

    // Draw instanced cubes with GPU culling
    RendererInstanced::draw_instanced(*r, *instanced_cubes, textured_mat);

    r->end_frame();
  }

  std::cout << "\n\nShutting down..." << std::endl;
  std::cout << "Final stats:" << std::endl;
  std::cout << "  Total instances: " << MAX_INSTANCES << std::endl;
  std::cout << "  Average visible: " << last_visible_count << std::endl;
  std::cout << "  Culling efficiency: "
            << (100.0f * (MAX_INSTANCES - last_visible_count) / MAX_INSTANCES)
            << "% culled" << std::endl;

  return 0;
}
