#pragma once
#include "../platform/platform.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace pixel::renderer2d {

// ============================================================================
// Core Types
// ============================================================================

struct Vec2 {
  float x, y;
  Vec2() : x(0), y(0) {}
  Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Color {
  uint8_t r, g, b, a;
  Color() : r(255), g(255), b(255), a(255) {}
  Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
      : r(r_), g(g_), b(b_), a(a_) {}

  static Color White() { return {255, 255, 255, 255}; }
  static Color Black() { return {0, 0, 0, 255}; }
  static Color Red() { return {255, 0, 0, 255}; }
  static Color Green() { return {0, 255, 0, 255}; }
  static Color Blue() { return {0, 0, 255, 255}; }
  static Color Transparent() { return {0, 0, 0, 0}; }
};

struct Rect {
  float x, y, w, h;
  Rect() : x(0), y(0), w(0), h(0) {}
  Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
};

// ============================================================================
// Texture Management
// ============================================================================

using TextureHandle = uint32_t;
constexpr TextureHandle INVALID_TEXTURE = 0;

struct TextureInfo {
  int width = 0;
  int height = 0;
  SDL_Texture *sdl_texture = nullptr;
};

// ============================================================================
// Camera
// ============================================================================

class Camera {
public:
  Vec2 position{0, 0}; // World position
  float zoom = 1.0f;   // Zoom factor (1.0 = normal, 2.0 = 2x zoom)

  Camera() = default;

  // Convert world coordinates to screen coordinates
  Vec2 world_to_screen(Vec2 world_pos, int screen_width,
                       int screen_height) const;

  // Convert screen coordinates to world coordinates
  Vec2 screen_to_world(Vec2 screen_pos, int screen_width,
                       int screen_height) const;

  void move(Vec2 delta) {
    position.x += delta.x;
    position.y += delta.y;
  }
  void set_zoom(float z) { zoom = z < 0.1f ? 0.1f : z; }
};

// ============================================================================
// Draw Commands (for batching)
// ============================================================================

enum class DrawType { Sprite, FilledRect, OutlineRect, Line, Circle, Text };

struct DrawCommand {
  DrawType type;
  int layer = 0; // For z-ordering

  // Sprite data
  TextureHandle texture = INVALID_TEXTURE;
  Rect src_rect;  // Source rect in texture
  Rect dest_rect; // Destination in world space
  Color tint;
  float rotation = 0.0f; // Rotation in degrees

  // Primitive data
  Vec2 p1, p2;         // For lines, or center/size for shapes
  float radius = 0.0f; // For circles
  Color color;

  // Text data
  std::string text;
};

// ============================================================================
// Main Renderer
// ============================================================================

class Renderer {
public:
  static std::unique_ptr<Renderer> create(const pixel::platform::WindowSpec &);
  ~Renderer();

  // Frame lifecycle
  void begin_frame(const Color &clear_color = Color::Black());
  void end_frame();

  // Texture management
  TextureHandle load_texture(const std::string &path);
  void unload_texture(TextureHandle handle);
  TextureInfo get_texture_info(TextureHandle handle) const;

  // Drawing commands (world space, batched)
  void draw_sprite(TextureHandle texture, const Rect &dest,
                   const Rect *src = nullptr,
                   const Color &tint = Color::White(), float rotation = 0.0f,
                   int layer = 0);

  void draw_rect_filled(const Rect &rect, const Color &color, int layer = 0);
  void draw_rect_outline(const Rect &rect, const Color &color, int layer = 0);
  void draw_line(Vec2 start, Vec2 end, const Color &color, int layer = 0);
  void draw_circle(Vec2 center, float radius, const Color &color,
                   int layer = 0);

  // Screen-space drawing (for UI, no camera transform)
  void draw_sprite_screen(TextureHandle texture, const Rect &dest,
                          const Rect *src = nullptr,
                          const Color &tint = Color::White());
  void draw_rect_screen(const Rect &rect, const Color &color);
  void draw_text_screen(const std::string &text, Vec2 pos, const Color &color);

  // Camera control
  Camera &camera() { return camera_; }
  const Camera &camera() const { return camera_; }

  // Window info
  int window_width() const;
  int window_height() const;

  // Debug
  void draw_demo(); // Keep for testing

private:
  Renderer() = default;
  void flush_draw_commands(); // Sort and execute all batched commands
  void execute_command(const DrawCommand &cmd);

  SDL_Window *win_ = nullptr;
  SDL_Renderer *rdr_ = nullptr;

  // Texture cache
  std::unordered_map<std::string, TextureHandle> texture_path_to_handle_;
  std::unordered_map<TextureHandle, TextureInfo> textures_;
  TextureHandle next_texture_handle_ = 1;

  // Draw command batching
  std::vector<DrawCommand> draw_commands_;

  // Camera
  Camera camera_;
};

// ============================================================================
// Utility Functions
// ============================================================================

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline Vec2 lerp(const Vec2 &a, const Vec2 &b, float t) {
  return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)};
}

} // namespace pixel::renderer2d
