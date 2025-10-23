#include "pixel/platform/window.hpp"
#include "pixel/platform/platform.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>

namespace pixel::platform {

// Static member initialization
bool Window::glfw_initialized_ = false;

// GLFW error callback
static void glfw_error_callback(int error, const char* description) {
  std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

void Window::initialize_glfw() {
  if (glfw_initialized_) {
    return;
  }

  glfwSetErrorCallback(glfw_error_callback);

  if (!glfwInit()) {
    throw std::runtime_error("Failed to initialize GLFW");
  }

  glfw_initialized_ = true;
}

bool Window::is_glfw_initialized() {
  return glfw_initialized_;
}

std::unique_ptr<Window> Window::create(const WindowSpec& spec, GraphicsAPI api) {
  // Initialize GLFW if not already done
  initialize_glfw();

  // Set window hints based on graphics API
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  switch (api) {
    case GraphicsAPI::OpenGL:
      glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
      glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
      break;

    case GraphicsAPI::Metal:
      // Metal doesn't use OpenGL context
      glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
      break;

    case GraphicsAPI::None:
      glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
      break;
  }

  // Create the window
  GLFWwindow* glfw_window = glfwCreateWindow(
    spec.w,
    spec.h,
    spec.title.c_str(),
    nullptr,
    nullptr
  );

  if (!glfw_window) {
    throw std::runtime_error("Failed to create GLFW window");
  }

  // Create Window object and set the native handle
  auto window = std::unique_ptr<Window>(new Window());
  window->window_ = glfw_window;

  return window;
}

Window::~Window() {
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }

  // Note: We don't call glfwTerminate() here because there might be multiple
  // windows or we might want to keep GLFW initialized for the application lifetime.
  // The application should handle GLFW termination at shutdown if needed.
}

Window::Window(Window&& other) noexcept
  : window_(other.window_) {
  other.window_ = nullptr;
}

Window& Window::operator=(Window&& other) noexcept {
  if (this != &other) {
    if (window_) {
      glfwDestroyWindow(window_);
    }
    window_ = other.window_;
    other.window_ = nullptr;
  }
  return *this;
}

GLFWwindow* Window::native_handle() {
  return window_;
}

const GLFWwindow* Window::native_handle() const {
  return window_;
}

int Window::width() const {
  if (!window_) {
    return 0;
  }
  int width = 0;
  glfwGetWindowSize(window_, &width, nullptr);
  return width;
}

int Window::height() const {
  if (!window_) {
    return 0;
  }
  int height = 0;
  glfwGetWindowSize(window_, nullptr, &height);
  return height;
}

bool Window::should_close() const {
  if (!window_) {
    return true;
  }
  return glfwWindowShouldClose(window_);
}

void Window::poll_events() {
  glfwPollEvents();
}

double Window::time() const {
  return glfwGetTime();
}

} // namespace pixel::platform
