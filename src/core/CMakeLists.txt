# src/core/CMakeLists.txt
# ============================================================================
# Pixel Life - Core Library
# ============================================================================

add_library(pixel_core STATIC
  clock.cpp
)

target_include_directories(pixel_core
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Core library has no external dependencies (only uses std::chrono)
# No target_link_libraries needed

target_compile_options(pixel_core PRIVATE ${PIXEL_WARN_CXX})

# Create alias for consistent naming (only if not already exists)
if(NOT TARGET pixel::core)
  add_library(pixel::core ALIAS pixel_core)
  message(STATUS "Core: Created pixel::core alias")
endif()

message(STATUS "Core: Clock and timing utilities (no dependencies)")
