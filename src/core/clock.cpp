#include "pixel/core/clock.hpp"
#include <chrono>
namespace pixel::core {
double now_sec() {
  using clk = std::chrono::steady_clock;
  return std::chrono::duration<double>(clk::now().time_since_epoch()).count();
}
} // namespace pixel::core
