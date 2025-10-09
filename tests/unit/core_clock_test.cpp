#include "pixel/core/clock.hpp"
#include <cassert>
int main() {
  auto t0 = pixel::core::now_sec();
  auto t1 = pixel::core::now_sec();
  assert(t1 >= t0);
  return 0;
}
