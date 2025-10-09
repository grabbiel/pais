#pragma once
#include "../platform/platform.hpp"
#include <memory>
struct SDL_Window;
struct SDL_Renderer;
namespace pixel::renderer2d {
class Renderer {
public:
  static std::unique_ptr<Renderer> create(const pixel::platform::WindowSpec &);
  ~Renderer();
  void begin_frame();
  void draw_demo(); // draw a colored rect
  void end_frame();

private:
  Renderer() = default;
  SDL_Window *win_ = nullptr;
  SDL_Renderer *rdr_ = nullptr;
};
} // namespace pixel::renderer2d
