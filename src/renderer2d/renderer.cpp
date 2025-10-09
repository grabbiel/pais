#include "pixel/renderer2d/renderer.hpp"
#include <SDL.h>
#include <stdexcept>

namespace pixel::renderer2d {
std::unique_ptr<Renderer>
Renderer::create(const pixel::platform::WindowSpec &s) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
    throw std::runtime_error(SDL_GetError());
  auto self = std::unique_ptr<Renderer>(new Renderer());
  self->win_ =
      SDL_CreateWindow(s.title.c_str(), SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, s.w, s.h, SDL_WINDOW_SHOWN);
  if (!self->win_)
    throw std::runtime_error(SDL_GetError());
  self->rdr_ = SDL_CreateRenderer(
      self->win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!self->rdr_)
    throw std::runtime_error(SDL_GetError());
  return self;
}
Renderer::~Renderer() {
  if (rdr_)
    SDL_DestroyRenderer(rdr_);
  if (win_)
    SDL_DestroyWindow(win_);
  SDL_Quit();
}
void Renderer::begin_frame() {
  SDL_SetRenderDrawColor(rdr_, 18, 18, 22, 255);
  SDL_RenderClear(rdr_);
}
void Renderer::draw_demo() {
  SDL_Rect r{50, 50, 200, 120};
  SDL_SetRenderDrawColor(rdr_, 60, 180, 255, 255);
  SDL_RenderFillRect(rdr_, &r);
}
void Renderer::end_frame() { SDL_RenderPresent(rdr_); }
} // namespace pixel::renderer2d
