#pragma once
#include <cstdint>

namespace pixel::telemetry {

enum class Level { kDebug, kInfo, kWarn, kError };

void log(Level lvl, const char *msg);
void frame_time_ms(double ms);
void net_rtt_ms(double ms);

} // namespace pixel::telemetry
