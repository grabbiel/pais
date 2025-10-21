# tests/unit/CMakeLists.txt
# ============================================================================
# Pixel Life - Unit Tests
# ============================================================================

# Core clock test
add_executable(core_clock_test
  core_clock_test.cpp
)

target_link_libraries(core_clock_test PRIVATE
  pixel_core
)

add_test(NAME CoreClockTest COMMAND core_clock_test)

message(STATUS "Unit tests configured")
