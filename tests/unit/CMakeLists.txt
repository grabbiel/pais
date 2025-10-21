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

if(APPLE)
  add_executable(metal_compute_test
    metal_compute_test.mm
  )

  target_link_libraries(metal_compute_test PRIVATE
    pixel_rhi
  )

  add_test(NAME MetalComputePipelineTest COMMAND metal_compute_test)
endif()

message(STATUS "Unit tests configured")
