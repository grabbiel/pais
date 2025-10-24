# src/renderer3d/CMakeLists.txt
# ============================================================================
# Pixel Life - 3D Renderer with RHI Backend
# ============================================================================

# Core renderer sources
set(RENDERER3D_SOURCES
  clip_space.cpp
  device_factory.cpp
  renderer.cpp
  camera.cpp
  mesh.cpp
  shader.cpp
  shader_reflection.cpp
  primitives.cpp
  renderer_instanced.cpp
  shadow_map.cpp
  lod.cpp
)

# Create renderer library
add_library(pixel_renderer3d STATIC
  ${RENDERER3D_SOURCES}
)

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(pixel_renderer3d
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================================
# Link dependencies (use aliases)
# ============================================================================

target_link_libraries(pixel_renderer3d
  PUBLIC
    pixel::core      # Alias created in src/core/CMakeLists.txt
    pixel::math      # Alias created in src/math/CMakeLists.txt
    pixel::platform  # Alias created in src/platform/common/CMakeLists.txt
    pixel::rhi       # Alias created in src/rhi/CMakeLists.txt
    pixel::resources # Alias created in src/resources/CMakeLists.txt
    ext::glfw        # Alias created in third_party/CMakeLists.txt
    ext::glm         # Alias created in third_party/CMakeLists.txt
)

# Note: OpenGL::GL is now linked through pixel::rhi, not directly here

# ============================================================================
# Platform-specific configuration
# ============================================================================

if(APPLE)
  # macOS-specific settings
  target_compile_definitions(pixel_renderer3d PRIVATE
    GL_SILENCE_DEPRECATION
  )

  # Metal backend support (if available)
  if(PIXEL_HAS_METAL)
    target_compile_definitions(pixel_renderer3d PUBLIC
      PIXEL_USE_METAL=1
    )
    message(STATUS "Renderer3D: Metal backend support enabled")
    # Note: When PIXEL_USE_METAL=1, OpenGL is NOT compiled (exclusive backend)
    # The device_factory will only use Metal with no GL fallback
  else()
    # No Metal, so we must have OpenGL
    message(STATUS "Renderer3D: Using OpenGL backend (Metal not available)")
  endif()

elseif(WIN32)
  # Windows-specific settings
  target_compile_definitions(pixel_renderer3d PRIVATE
    NOMINMAX           # Prevent Windows.h from defining min/max macros
    WIN32_LEAN_AND_MEAN
  )

elseif(UNIX)
  # Linux-specific settings
  target_link_libraries(pixel_renderer3d PUBLIC ${CMAKE_DL_LIBS})
endif()

# ============================================================================
# Compiler settings
# ============================================================================

# Apply project-wide warnings
target_compile_options(pixel_renderer3d PRIVATE ${PIXEL_WARN_CXX})

# Disable specific warnings for third-party headers
if(MSVC)
  target_compile_options(pixel_renderer3d PRIVATE
    /wd4201  # nonstandard extension used: nameless struct/union
    /wd4127  # conditional expression is constant (from GLM)
  )
else()
  target_compile_options(pixel_renderer3d PRIVATE
    -Wno-gnu-anonymous-struct
    -Wno-nested-anon-types
  )

  # Additional warnings to catch common issues
  if(NOT APPLE)
    target_compile_options(pixel_renderer3d PRIVATE
      -Wno-unused-parameter
    )
  endif()
endif()

# ============================================================================
# Compile definitions
# ============================================================================

target_compile_definitions(pixel_renderer3d
  PUBLIC
    $<$<CONFIG:Debug>:PIXEL_RENDERER_DEBUG=1>
    PIXEL_RENDERER_INSTANCING=1
    PIXEL_RENDERER_LOD=1
)

# ============================================================================
# Source groups for IDEs
# ============================================================================

source_group("Renderer\\Core" FILES
  renderer.cpp
  camera.cpp
  device_factory.cpp
)

source_group("Renderer\\Resources" FILES
  mesh.cpp
  shader.cpp
  shadow_map.cpp
)

source_group("Renderer\\Primitives" FILES
  primitives.cpp
)

source_group("Renderer\\Advanced" FILES
  renderer_instanced.cpp
  lod.cpp
)

# ============================================================================
# Alias target (only if not already exists)
# ============================================================================

if(NOT TARGET pixel::renderer3d)
  add_library(pixel::renderer3d ALIAS pixel_renderer3d)
  message(STATUS "Renderer3D: Created pixel::renderer3d alias")
endif()

# ============================================================================
# Configuration messages
# ============================================================================

message(STATUS "Renderer3D Configuration:")
message(STATUS "  RHI Backend:       Abstracted (OpenGL/Metal)")
message(STATUS "  Instancing:        ENABLED")
message(STATUS "  LOD System:        ENABLED")
message(STATUS "  Dithered LOD:      ENABLED")

if(APPLE AND PIXEL_HAS_METAL)
  message(STATUS "  Graphics API:      Metal (exclusive - no OpenGL fallback)")
else()
  message(STATUS "  Graphics API:      OpenGL 3.3 Core")
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS pixel_renderer3d
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

# Install public headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/pixel/renderer3d
  DESTINATION include/pixel
  FILES_MATCHING PATTERN "*.hpp"
)
