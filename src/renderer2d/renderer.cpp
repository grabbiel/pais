#include "pixel/renderer2d/renderer.h"
#include <SDL>
#include <SDL_image>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixel::renderer2d {

// ============================================================================
// Camera Implementation
// ============================================================================

Vec2 Camera::world_to_screen(Vec2 world_pos, int screen_width,
                             int screen_height) const {
  float screen_center_x = screen_width * 0.5f;
  float screen_center_y = screen_height * 0.5f;

  // Apply zoom and translate relative to camera position
  float x = (world_pos.x - position.x) * zoom + screen_center_x;
  float y = (world_pos.y - position.y) * zoom + screen_center_y;

  return {x, y};
}

Vec2 Camera::screen_to_world(Vec2 screen_pos, int screen_width,
                             int screen_height) const {
  float screen_center_x = screen_width * 0.5f;
  float screen_center_y = screen_height * 0.5f;

  // Reverse the transform
  float x = (screen_pos.x - screen_center_x) / zoom + position.x;
  float y = (screen_pos.y - screen_center_y) / zoom + position.y;

  return {x, y};
}

// ============================================================================
// Renderer Implementation
// ============================================================================

std::unique_ptr<Renderer>
Renderer::create(const pixel::platform::WindowSpec &s) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
    throw std::runtime_error(SDL_GetError());

  // Initialize SDL_image for PNG/JPG support
  int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
  if (!(IMG_Init(img_flags) & img_flags))
    throw std::runtime_error(IMG_GetError());

  auto self = std::unique_ptr<Renderer>(new Renderer());

  self->win_ = SDL_CreateWindow(s.title.c_str(), SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, s.w, s.h,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

  if (!self->win_)
    throw std::runtime_error(SDL_GetError());

  self->rdr_ = SDL_CreateRenderer(
      self->win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if (!self->rdr_)
    throw std::runtime_error(SDL_GetError());

  // Enable blend mode for transparency
  SDL_SetRenderDrawBlendMode(self->rdr_, SDL_BLENDMODE_BLEND);

  return self;
}

Renderer::~Renderer() {
  // Clean up all textures
  for (auto &pair : textures_) {
    if (pair.second.sdl_texture)
      SDL_DestroyTexture(pair.second.sdl_texture);
  }

  if (rdr_)
    SDL_DestroyRenderer(rdr_);
  if (win_)
    SDL_DestroyWindow(win_);

  IMG_Quit();
  SDL_Quit();
}

// ============================================================================
// Frame Management
// ============================================================================

void Renderer::begin_frame(const Color &clear_color) {
  draw_commands_.clear();

  SDL_SetRenderDrawColor(rdr_, clear_color.r, clear_color.g, clear_color.b,
                         clear_color.a);
  SDL_RenderClear(rdr_);
}

void Renderer::end_frame() {
  flush_draw_commands();
  SDL_RenderPresent(rdr_);
}

void Renderer::flush_draw_commands() {
  // Sort by layer for proper z-ordering
  std::sort(draw_commands_.begin(), draw_commands_.end(),
            [](const DrawCommand &a, const DrawCommand &b) {
              return a.layer < b.layer;
            });

  // Execute all commands
  for (const auto &cmd : draw_commands_) {
    execute_command(cmd);
  }
}

void Renderer::execute_command(const DrawCommand &cmd) {
  int w = window_width();
  int h = window_height();

  switch (cmd.type) {
  case DrawType::Sprite: {
    auto it = textures_.find(cmd.texture);
    if (it == textures_.end() || !it->second.sdl_texture)
      break;

    SDL_Texture *tex = it->second.sdl_texture;

    // Apply camera transform to destination
    Vec2 top_left =
        camera_.world_to_screen({cmd.dest_rect.x, cmd.dest_rect.y}, w, h);
    float scaled_w = cmd.dest_rect.w * camera_.zoom;
    float scaled_h = cmd.dest_rect.h * camera_.zoom;

    SDL_Rect src = {
        static_cast<int>(cmd.src_rect.x), static_cast<int>(cmd.src_rect.y),
        static_cast<int>(cmd.src_rect.w), static_cast<int>(cmd.src_rect.h)};

    SDL_Rect dst = {static_cast<int>(top_left.x), static_cast<int>(top_left.y),
                    static_cast<int>(scaled_w), static_cast<int>(scaled_h)};

    SDL_SetTextureColorMod(tex, cmd.tint.r, cmd.tint.g, cmd.tint.b);
    SDL_SetTextureAlphaMod(tex, cmd.tint.a);

    if (std::abs(cmd.rotation) > 0.001f) {
      SDL_RenderCopyEx(rdr_, tex, &src, &dst, cmd.rotation, nullptr,
                       SDL_FLIP_NONE);
    } else {
      SDL_RenderCopy(rdr_, tex, &src, &dst);
    }
    break;
  }

  case DrawType::FilledRect: {
    Vec2 top_left =
        camera_.world_to_screen({cmd.dest_rect.x, cmd.dest_rect.y}, w, h);
    float scaled_w = cmd.dest_rect.w * camera_.zoom;
    float scaled_h = cmd.dest_rect.h * camera_.zoom;

    SDL_Rect rect = {static_cast<int>(top_left.x), static_cast<int>(top_left.y),
                     static_cast<int>(scaled_w), static_cast<int>(scaled_h)};

    SDL_SetRenderDrawColor(rdr_, cmd.color.r, cmd.color.g, cmd.color.b,
                           cmd.color.a);
    SDL_RenderFillRect(rdr_, &rect);
    break;
  }

  case DrawType::OutlineRect: {
    Vec2 top_left =
        camera_.world_to_screen({cmd.dest_rect.x, cmd.dest_rect.y}, w, h);
    float scaled_w = cmd.dest_rect.w * camera_.zoom;
    float scaled_h = cmd.dest_rect.h * camera_.zoom;

    SDL_Rect rect = {static_cast<int>(top_left.x), static_cast<int>(top_left.y),
                     static_cast<int>(scaled_w), static_cast<int>(scaled_h)};

    SDL_SetRenderDrawColor(rdr_, cmd.color.r, cmd.color.g, cmd.color.b,
                           cmd.color.a);
    SDL_RenderDrawRect(rdr_, &rect);
    break;
  }

  case DrawType::Line: {
    Vec2 p1_screen = camera_.world_to_screen(cmd.p1, w, h);
    Vec2 p2_screen = camera_.world_to_screen(cmd.p2, w, h);

    SDL_SetRenderDrawColor(rdr_, cmd.color.r, cmd.color.g, cmd.color.b,
                           cmd.color.a);
    SDL_RenderDrawLine(
        rdr_, static_cast<int>(p1_screen.x), static_cast<int>(p1_screen.y),
        static_cast<int>(p2_screen.x), static_cast<int>(p2_screen.y));
    break;
  }

  case DrawType::Circle: {
    Vec2 center_screen = camera_.world_to_screen(cmd.p1, w, h);
    float scaled_radius = cmd.radius * camera_.zoom;

    // Simple circle drawing using SDL_RenderDrawPoint
    // For better quality, consider using a texture or more sophisticated
    // algorithm
    SDL_SetRenderDrawColor(rdr_, cmd.color.r, cmd.color.g, cmd.color.b,
                           cmd.color.a);

    int cx = static_cast<int>(center_screen.x);
    int cy = static_cast<int>(center_screen.y);
    int r = static_cast<int>(scaled_radius);

    // Midpoint circle algorithm
    int x = r;
    int y = 0;
    int err = 0;

    while (x >= y) {
      SDL_RenderDrawPoint(rdr_, cx + x, cy + y);
      SDL_RenderDrawPoint(rdr_, cx + y, cy + x);
      SDL_RenderDrawPoint(rdr_, cx - y, cy + x);
      SDL_RenderDrawPoint(rdr_, cx - x, cy + y);
      SDL_RenderDrawPoint(rdr_, cx - x, cy - y);
      SDL_RenderDrawPoint(rdr_, cx - y, cy - x);
      SDL_RenderDrawPoint(rdr_, cx + y, cy - x);
      SDL_RenderDrawPoint(rdr_, cx + x, cy - y);

      y += 1;
      err += 1 + 2 * y;
      if (2 * (err - x) + 1 > 0) {
        x -= 1;
        err += 1 - 2 * x;
      }
    }
    break;
  }

  case DrawType::Text:
    // Text rendering would need SDL_ttf
    // For now, just a placeholder
    break;
  }
}

// ============================================================================
// Texture Management
// ============================================================================

TextureHandle Renderer::load_texture(const std::string &path) {
  // Check if already loaded
  auto it = texture_path_to_handle_.find(path);
  if (it != texture_path_to_handle_.end())
    return it->second;

  // Load new texture
  SDL_Surface *surface = IMG_Load(path.c_str());
  if (!surface)
    throw std::runtime_error(std::string("Failed to load image: ") +
                             IMG_GetError());

  SDL_Texture *texture = SDL_CreateTextureFromSurface(rdr_, surface);
  int w = surface->w;
  int h = surface->h;
  SDL_FreeSurface(surface);

  if (!texture)
    throw std::runtime_error(std::string("Failed to create texture: ") +
                             SDL_GetError());

  // Store texture
  TextureHandle handle = next_texture_handle_++;
  TextureInfo info;
  info.width = w;
  info.height = h;
  info.sdl_texture = texture;

  textures_[handle] = info;
  texture_path_to_handle_[path] = handle;

  return handle;
}

void Renderer::unload_texture(TextureHandle handle) {
  auto it = textures_.find(handle);
  if (it != textures_.end()) {
    if (it->second.sdl_texture)
      SDL_DestroyTexture(it->second.sdl_texture);
    textures_.erase(it);
  }

  // Remove from path map
  for (auto it = texture_path_to_handle_.begin();
       it != texture_path_to_handle_.end(); ++it) {
    if (it->second == handle) {
      texture_path_to_handle_.erase(it);
      break;
    }
  }
}

TextureInfo Renderer::get_texture_info(TextureHandle handle) const {
  auto it = textures_.find(handle);
  if (it != textures_.end())
    return it->second;
  return TextureInfo{};
}

// ============================================================================
// Drawing API - World Space
// ============================================================================

void Renderer::draw_sprite(TextureHandle texture, const Rect &dest,
                           const Rect *src, const Color &tint, float rotation,
                           int layer) {
  DrawCommand cmd;
  cmd.type = DrawType::Sprite;
  cmd.texture = texture;
  cmd.dest_rect = dest;
  cmd.tint = tint;
  cmd.rotation = rotation;
  cmd.layer = layer;

  if (src) {
    cmd.src_rect = *src;
  } else {
    // Use full texture
    TextureInfo info = get_texture_info(texture);
    cmd.src_rect = {0, 0, static_cast<float>(info.width),
                    static_cast<float>(info.height)};
  }

  draw_commands_.push_back(cmd);
}

void Renderer::draw_rect_filled(const Rect &rect, const Color &color,
                                int layer) {
  DrawCommand cmd;
  cmd.type = DrawType::FilledRect;
  cmd.dest_rect = rect;
  cmd.color = color;
  cmd.layer = layer;
  draw_commands_.push_back(cmd);
}

void Renderer::draw_rect_outline(const Rect &rect, const Color &color,
                                 int layer) {
  DrawCommand cmd;
  cmd.type = DrawType::OutlineRect;
  cmd.dest_rect = rect;
  cmd.color = color;
  cmd.layer = layer;
  draw_commands_.push_back(cmd);
}

void Renderer::draw_line(Vec2 start, Vec2 end, const Color &color, int layer) {
  DrawCommand cmd;
  cmd.type = DrawType::Line;
  cmd.p1 = start;
  cmd.p2 = end;
  cmd.color = color;
  cmd.layer = layer;
  draw_commands_.push_back(cmd);
}

void Renderer::draw_circle(Vec2 center, float radius, const Color &color,
                           int layer) {
  DrawCommand cmd;
  cmd.type = DrawType::Circle;
  cmd.p1 = center;
  cmd.radius = radius;
  cmd.color = color;
  cmd.layer = layer;
  draw_commands_.push_back(cmd);
}

// ============================================================================
// Drawing API - Screen Space (UI)
// ============================================================================

void Renderer::draw_sprite_screen(TextureHandle texture, const Rect &dest,
                                  const Rect *src, const Color &tint) {
  auto it = textures_.find(texture);
  if (it == textures_.end() || !it->second.sdl_texture)
    return;

  SDL_Texture *tex = it->second.sdl_texture;

  SDL_Rect src_rect;
  if (src) {
    src_rect = {static_cast<int>(src->x), static_cast<int>(src->y),
                static_cast<int>(src->w), static_cast<int>(src->h)};
  } else {
    src_rect = {0, 0, it->second.width, it->second.height};
  }

  SDL_Rect dst_rect = {static_cast<int>(dest.x), static_cast<int>(dest.y),
                       static_cast<int>(dest.w), static_cast<int>(dest.h)};

  SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
  SDL_SetTextureAlphaMod(tex, tint.a);
  SDL_RenderCopy(rdr_, tex, &src_rect, &dst_rect);
}

void Renderer::draw_rect_screen(const Rect &rect, const Color &color) {
  SDL_Rect r = {static_cast<int>(rect.x), static_cast<int>(rect.y),
                static_cast<int>(rect.w), static_cast<int>(rect.h)};
  SDL_SetRenderDrawColor(rdr_, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(rdr_, &r);
}

void Renderer::draw_text_screen(const std::string &text, Vec2 pos,
                                const Color &color) {
  // Would need SDL_ttf for proper text rendering
  // This is a placeholder
  (void)text;
  (void)pos;
  (void)color;
}

// ============================================================================
// Utility
// ============================================================================

int Renderer::window_width() const {
  int w = 0;
  SDL_GetWindowSize(win_, &w, nullptr);
  return w;
}

int Renderer::window_height() const {
  int h = 0;
  SDL_GetWindowSize(win_, nullptr, &h);
  return h;
}

void Renderer::draw_demo() {
  // Keep the original demo for testing
  SDL_Rect r{50, 50, 200, 120};
  SDL_SetRenderDrawColor(rdr_, 60, 180, 255, 255);
  SDL_RenderFillRect(rdr_, &r);
}

} // namespace pixel::renderer2d
