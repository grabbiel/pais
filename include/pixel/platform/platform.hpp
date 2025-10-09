#pragma once
#include <string>
namespace pixel::platform {
struct WindowSpec {
  int w = 960, h = 540;
  std::string title = "Pixel-Life";
};
} // namespace pixel::platform
