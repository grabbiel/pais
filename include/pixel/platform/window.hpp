#pragma once

#include <memory>

struct GLFWwindow;

namespace pixel::platform {

struct WindowSpec;

enum class GraphicsAPI {
  None,
  OpenGL,
  Metal
};

/**
 * @brief Platform-agnostic window abstraction
 *
 * Provides a clean interface for window creation and management,
 * isolating GLFW dependency to the platform layer.
 */
class Window {
public:
  /**
   * @brief Create a window with specified parameters
   * @param spec Window specification (size, title)
   * @param api Graphics API to use for backend initialization
   * @return Unique pointer to the created Window
   * @throws std::runtime_error if window creation fails
   */
  static std::unique_ptr<Window> create(const WindowSpec& spec, GraphicsAPI api);

  /**
   * @brief Destructor - cleans up window and GLFW resources
   */
  ~Window();

  // Delete copy constructor and assignment
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Allow move
  Window(Window&& other) noexcept;
  Window& operator=(Window&& other) noexcept;

  /**
   * @brief Get native GLFW window handle
   * @return Pointer to the underlying GLFWwindow
   */
  GLFWwindow* native_handle();

  /**
   * @brief Get native GLFW window handle (const version)
   * @return Const pointer to the underlying GLFWwindow
   */
  const GLFWwindow* native_handle() const;

  /**
   * @brief Get current window width
   * @return Window width in pixels
   */
  int width() const;

  /**
   * @brief Get current window height
   * @return Window height in pixels
   */
  int height() const;

  /**
   * @brief Check if window should close
   * @return true if window close was requested
   */
  bool should_close() const;

  /**
   * @brief Poll window events
   *
   * Processes all pending window events from the event queue
   */
  void poll_events();

  /**
   * @brief Get time since GLFW initialization
   * @return Time in seconds
   */
  double time() const;

private:
  /**
   * @brief Private constructor - use create() factory method
   */
  Window() = default;

  /**
   * @brief Initialize GLFW library
   * @throws std::runtime_error if initialization fails
   */
  static void initialize_glfw();

  /**
   * @brief Check if GLFW has been initialized
   */
  static bool is_glfw_initialized();

  GLFWwindow* window_ = nullptr;
  static bool glfw_initialized_;
};

} // namespace pixel::platform
