// src/renderer3d/renderer_factory.cpp
// ============================================================================
// Renderer Factory - Backend Selection for Metal/OpenGL
// ============================================================================

#include "pixel/renderer3d/renderer.hpp"
#include <iostream>
#include <cstdlib>

#if PIXEL_USE_METAL && __APPLE__
#include "pixel/renderer3d/metal/metal_renderer.hpp"
#endif

namespace pixel::renderer3d {

// Override the existing Renderer::create to add backend selection
#if PIXEL_USE_METAL && __APPLE__

std::unique_ptr<Renderer>
Renderer::create_with_metal(const pixel::platform::WindowSpec &spec) {
  // Try Metal first
  std::cout << "Attempting to create Metal renderer..." << std::endl;

  try {
    auto metal_renderer = metal::MetalRenderer::create(spec);
    if (metal_renderer) {
      std::cout << "Successfully created Metal renderer" << std::endl;
      return metal_renderer;
    }
  } catch (const std::exception &e) {
    std::cerr << "Metal renderer creation failed: " << e.what() << std::endl;
    std::cerr << "Falling back to OpenGL..." << std::endl;
  }

  // Fall back to OpenGL
  return create_with_opengl(spec);
}

std::unique_ptr<Renderer>
Renderer::create_with_opengl(const pixel::platform::WindowSpec &spec) {
  // Original OpenGL implementation (move existing Renderer::create here)
  std::cout << "Creating OpenGL renderer..." << std::endl;

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
  renderer->load_gl_functions();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glfwSwapInterval(1);

  std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
  std::cout << "GLFW Version: " << glfwGetVersionString() << std::endl;

  renderer->setup_default_shaders();
  renderer->sprite_mesh_ = renderer->create_sprite_quad();

  return renderer;
}

// New unified create that selects backend
std::unique_ptr<Renderer>
Renderer::create(const pixel::platform::WindowSpec &spec) {
  // Check environment variable for backend override
  const char *backend_env = std::getenv("PIXEL_RENDERER_BACKEND");

  if (backend_env) {
    std::string backend(backend_env);
    if (backend == "OPENGL") {
      std::cout << "Backend override: Using OpenGL (via environment)"
                << std::endl;
      return create_with_opengl(spec);
    } else if (backend == "METAL") {
      std::cout << "Backend override: Using Metal (via environment)"
                << std::endl;
      return create_with_metal(spec);
    }
  }

  // Default: try Metal first, fall back to OpenGL
  return create_with_metal(spec);
}

#endif // PIXEL_USE_METAL && __APPLE__

// Backend query methods
bool Renderer::is_metal_backend() const {
#if PIXEL_USE_METAL && __APPLE__
  return dynamic_cast<const metal::MetalRenderer *>(this) != nullptr;
#else
  return false;
#endif
}

bool Renderer::is_opengl_backend() const { return !is_metal_backend(); }

const char *Renderer::backend_name() const {
  return is_metal_backend() ? "Metal" : "OpenGL";
}

// Feature support queries
bool Renderer::supports_compute_shaders() const {
  if (is_metal_backend()) {
    return true; // Metal always supports compute
  }

  // Check OpenGL version
  const char *version = (const char *)glGetString(GL_VERSION);
  if (!version)
    return false;

  int major = 0, minor = 0;
  sscanf(version, "%d.%d", &major, &minor);
  return (major > 4) || (major == 4 && minor >= 3);
}

bool Renderer::supports_texture_arrays() const {
  if (is_metal_backend()) {
    return true;
  }
  // OpenGL 3.0+ supports texture arrays
  return true; // We require 3.3 minimum
}

bool Renderer::supports_indirect_drawing() const {
  if (is_metal_backend()) {
    return true;
  }

  // OpenGL 4.0+ or extension
  const char *version = (const char *)glGetString(GL_VERSION);
  if (!version)
    return false;

  int major = 0, minor = 0;
  sscanf(version, "%d.%d", &major, &minor);

  if (major >= 4)
    return true;

  const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
  return extensions && strstr(extensions, "GL_ARB_draw_indirect");
}

bool Renderer::supports_persistent_mapping() const {
  if (is_metal_backend()) {
    return true; // Different memory model but equivalent
  }

  const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
  return extensions && strstr(extensions, "GL_ARB_buffer_storage");
}

// Performance profile
Renderer::PerformanceProfile Renderer::get_performance_profile() const {
  PerformanceProfile profile;

  if (is_metal_backend()) {
    // Metal on Apple Silicon
    profile.max_recommended_instances = 100000;
    profile.max_recommended_vertices = 10000000;
    profile.max_recommended_texture_size = 16384;
    profile.max_recommended_texture_array_layers = 2048;
    profile.supports_gpu_culling = true;
    profile.supports_gpu_lod = true;
    profile.recommended_lod_mode = "Hybrid";
  } else {
    // OpenGL (conservative)
    profile.max_recommended_instances = 10000;
    profile.max_recommended_vertices = 1000000;
    profile.max_recommended_texture_size = 8192;
    profile.max_recommended_texture_array_layers = 256;
    profile.supports_gpu_culling = supports_compute_shaders();
    profile.supports_gpu_lod = supports_compute_shaders();
    profile.recommended_lod_mode =
        profile.supports_gpu_lod ? "Hybrid" : "Distance";
  }

  return profile;
}

} // namespace pixel::renderer3d
