#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
#include <SDL2/SDL_main.h> // declares SDL_main
#include <TargetConditionals.h>
#define PIXEL_ENTRY SDL_main // iOS needs SDL_main
#else
#define PIXEL_ENTRY main
#endif

#include "pixel/core/clock.hpp"
#include "pixel/platform/platform.hpp"
#include "pixel/renderer2d/renderer.hpp"
#include <SDL.h>
#include <atomic>

int PIXEL_ENTRY(int argc, char **argv) {
  pixel::platform::WindowSpec ws;
  ws.w = 1024;
  ws.h = 576;
  ws.title = "Pixel-Life (skeleton)";
  auto r = pixel::renderer2d::Renderer::create(ws);

  const double dt = 1.0 / 60.0;
  double acc = 0.0, t0 = pixel::core::now_sec();
  bool running = true;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        running = false;
    }
    const double t1 = pixel::core::now_sec();
    acc += (t1 - t0);
    t0 = t1;

    while (acc >= dt) { /* TODO logic tick */
      acc -= dt;
    }

    r->begin_frame();
    r->draw_demo();
    r->end_frame();
  }
  return 0;
}
