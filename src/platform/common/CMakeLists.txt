# src/platform/common/CMakeLists.txt
# ============================================================================
# Pixel Life - Platform Library
# ============================================================================

set(PLATFORM_SOURCES
  platform.cpp
  resources.cpp
  shader_loader.cpp
)

# Add platform-specific sources
if(APPLE)
  list(APPEND PLATFORM_SOURCES
    ${CMAKE_SOURCE_DIR}/src/platform/apple/platform_apple.cpp
  )
endif()

add_library(pixel_platform STATIC
  ${PLATFORM_SOURCES}
)

target_include_directories(pixel_platform
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Platform-specific dependencies
if(APPLE)
  target_link_libraries(pixel_platform PUBLIC "-framework CoreFoundation")
endif()

target_compile_options(pixel_platform PRIVATE ${PIXEL_WARN_CXX})

# Create alias for consistent naming (only if not already created)
if(NOT TARGET pixel::platform)
  add_library(pixel::platform ALIAS pixel_platform)
  message(STATUS "Platform: Created pixel::platform alias")
endif()

message(STATUS "Platform: Cross-platform utilities")
