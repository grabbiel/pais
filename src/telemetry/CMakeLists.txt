# src/telemetry/CMakeLists.txt
# ============================================================================
# Pixel Life - Telemetry Library
# ============================================================================

add_library(pixel_telemetry STATIC
  telemetry.cpp
)

target_include_directories(pixel_telemetry
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# No external dependencies needed - uses standard C++ I/O
target_link_libraries(pixel_telemetry
  PUBLIC
    # No dependencies - simple logging to stderr
)

target_compile_options(pixel_telemetry PRIVATE ${PIXEL_WARN_CXX})

# Create alias for consistent naming
add_library(pixel::telemetry ALIAS pixel_telemetry)

message(STATUS "Telemetry: Simple stderr logging (no external dependencies)")
