# src/resources/CMakeLists.txt
# ============================================================================
# Pixel Life - Resource Management System
# ============================================================================

# Resource management sources
set(RESOURCES_SOURCES
  texture_loader.cpp
)

# Create resources library
add_library(pixel_resources STATIC
  ${RESOURCES_SOURCES}
)

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(pixel_resources
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================================
# Link dependencies
# ============================================================================

target_link_libraries(pixel_resources
  PUBLIC
    pixel::rhi       # For RHI device and types
    ext::stb         # For STB image loading
)

# ============================================================================
# Compiler settings
# ============================================================================

# Apply project-wide warnings
target_compile_options(pixel_resources PRIVATE ${PIXEL_WARN_CXX})

# Disable specific warnings for STB headers
if(MSVC)
  target_compile_options(pixel_resources PRIVATE
    /wd4996  # 'function': was declared deprecated
  )
else()
  target_compile_options(pixel_resources PRIVATE
    -Wno-unused-parameter
    -Wno-sign-compare
  )
endif()

# ============================================================================
# Compile definitions
# ============================================================================

target_compile_definitions(pixel_resources
  PUBLIC
    $<$<CONFIG:Debug>:PIXEL_RESOURCES_DEBUG=1>
)

# ============================================================================
# Source groups for IDEs
# ============================================================================

source_group("Resources\\Loaders" FILES
  texture_loader.cpp
)

# ============================================================================
# Alias target
# ============================================================================

if(NOT TARGET pixel::resources)
  add_library(pixel::resources ALIAS pixel_resources)
  message(STATUS "Resources: Created pixel::resources alias")
endif()

# ============================================================================
# Configuration messages
# ============================================================================

message(STATUS "Resources Configuration:")
message(STATUS "  Texture Loader:    ENABLED")
message(STATUS "  Resource Cache:    ENABLED (integrated)")

# ============================================================================
# Installation
# ============================================================================

install(TARGETS pixel_resources
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

# Install public headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/pixel/resources
  DESTINATION include/pixel
  FILES_MATCHING PATTERN "*.hpp"
)
