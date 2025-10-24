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
    pixel::core
    pixel::math
    pixel::platform
    pixel::rhi
    pixel::resources
    ext::glfw
    ext::glm
)

# ============================================================================
# Platform-specific configuration
# ============================================================================

set(RENDERER3D_BACKEND_NAME "(not configured)")

if(APPLE)
  target_compile_definitions(pixel_renderer3d PRIVATE
    GL_SILENCE_DEPRECATION
  )

  if(PIXEL_HAS_METAL)
    target_compile_definitions(pixel_renderer3d PUBLIC
      PIXEL_USE_METAL=1
    )
    message(STATUS "Renderer3D: Metal backend support enabled")
    set(RENDERER3D_BACKEND_NAME "Metal")
  elseif(PIXEL_USE_METAL)
    message(WARNING "Renderer3D: PIXEL_USE_METAL requested but Metal frameworks were not detected.")
  endif()
elseif(PIXEL_USE_METAL)
  message(WARNING "Renderer3D: PIXEL_USE_METAL requested but the current platform does not support Metal.")
endif()

if(PIXEL_USE_DX12 AND WIN32)
  target_compile_definitions(pixel_renderer3d PUBLIC
    PIXEL_USE_DX12=1
  )
  message(STATUS "Renderer3D: DirectX 12 backend support enabled")
  set(RENDERER3D_BACKEND_NAME "DirectX 12")
elseif(PIXEL_USE_DX12)
  message(WARNING "Renderer3D: PIXEL_USE_DX12 requested but the current platform is not Windows.")
endif()

# ============================================================================
# Compiler settings
# ============================================================================

# Apply project-wide warnings
target_compile_options(pixel_renderer3d PRIVATE ${PIXEL_WARN_CXX})

if(MSVC)
  target_compile_options(pixel_renderer3d PRIVATE
    /wd4201
    /wd4127
  )
else()
  target_compile_options(pixel_renderer3d PRIVATE
    -Wno-gnu-anonymous-struct
    -Wno-nested-anon-types
  )
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
message(STATUS "  RHI Backend:       ${RENDERER3D_BACKEND_NAME}")
if(RENDERER3D_BACKEND_NAME STREQUAL "Metal")
  message(STATUS "  Graphics API:      Metal")
elseif(RENDERER3D_BACKEND_NAME STREQUAL "DirectX 12")
  message(STATUS "  Graphics API:      DirectX 12")
else()
  message(STATUS "  Graphics API:      (not configured)")
endif()
message(STATUS "  Instancing:        ENABLED")
message(STATUS "  LOD System:        ENABLED")
message(STATUS "  Dithered LOD:      ENABLED")

# ============================================================================
# Installation
# ============================================================================

install(TARGETS pixel_renderer3d
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/pixel/renderer3d
  DESTINATION include/pixel
  FILES_MATCHING PATTERN "*.hpp"
)
