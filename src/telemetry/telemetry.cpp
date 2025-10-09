#include "telemetry.hpp"
#include <cstdio>

namespace pixel::telemetry {

static const char *lvl_name(Level l) {
  switch (l) {
  case Level::kDebug:
    return "DEBUG";
  case Level::kInfo:
    return "INFO";
  case Level::kWarn:
    return "WARN";
  default:
    return "ERROR";
  }
}

void log(Level lvl, const char *msg) {
  std::fprintf(stderr, "[%s] %s\n", lvl_name(lvl), msg);
}

void frame_time_ms(double ms) {
  // TODO: push to histogram; for now just print every ~60 frames
  static int n = 0;
  if ((++n % 60) == 0)
    std::fprintf(stderr, "[METRIC] frame_ms=%.3f\n", ms);
}

void net_rtt_ms(double ms) {
  static int n = 0;
  if ((++n % 120) == 0)
    std::fprintf(stderr, "[METRIC] net_rtt_ms=%.2f\n", ms);
}

} // namespace pixel::telemetry
