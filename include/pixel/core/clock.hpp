#pragma once
#include <cstdint>
namespace pixel::core {
struct TimeStep {
  double dt;
  std::uint64_t frame;
};
double now_sec(); // monotonic
} // namespace pixel::core
