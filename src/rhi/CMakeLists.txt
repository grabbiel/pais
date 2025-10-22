# src/rhi/CMakeLists.txt
# ============================================================================
# Pixel Life - RHI (Render Hardware Interface) Library
# ============================================================================


# ============================================================================
# Platform-specific backend selection
# ============================================================================

if(APPLE)
  # macOS: Use Metal backend (primary) with optional OpenGL 3.3 fallback
  message(STATUS "RHI: Configuring for macOS/ARM64")

  if(PIXEL_HAS_METAL)
    # Metal backend sources (RHI implementation only)
    set(RHI_BACKEND_SOURCES
      backends/metal/device_metal.mm
      backends/metal/cmd_metal.mm
      backends/metal/pipeline_metal.mm
    )
    message(STATUS "RHI: Using Metal backend (RHI implementation)")

    # Optional: Add OpenGL 3.3 Core fallback if available
    # Only include GL sources that are compatible with OpenGL 3.3
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/backends/gl/device_gl_legacy.cpp")
      list(APPEND RHI_BACKEND_SOURCES
        backends/gl/device_gl_legacy.cpp
        backends/gl/cmd_gl_legacy.cpp
        backends/gl/pipeline_gl_legacy.cpp
      )
      message(STATUS "RHI: Including OpenGL 3.3 fallback")
    endif()
  else()
    # Fallback to OpenGL 3.3 Core only (no 4.3+ features)
    message(WARNING "RHI: Metal not available, using OpenGL 3.3 Core fallback")
    set(RHI_BACKEND_SOURCES
      backends/gl/device_gl_legacy.cpp
      backends/gl/cmd_gl_legacy.cpp
      backends/gl/pipeline_gl_legacy.cpp
    )
  endif()

elseif(WIN32)
  # Windows: Use OpenGL 4.3+ or Vulkan
  message(STATUS "RHI: Configuring for Windows")
  set(RHI_BACKEND_SOURCES
    backends/gl/device_gl.cpp
    backends/gl/cmd_gl.cpp
    backends/gl/pipeline_gl.cpp
  )
  message(STATUS "RHI: Using OpenGL 4.3+ backend")

elseif(UNIX)
  # Linux: Use OpenGL 4.3+ or Vulkan
  message(STATUS "RHI: Configuring for Linux")
  set(RHI_BACKEND_SOURCES
    backends/gl/device_gl.cpp
    backends/gl/cmd_gl.cpp
    backends/gl/pipeline_gl.cpp
  )
  message(STATUS "RHI: Using OpenGL 4.3+ backend")

else()
  message(FATAL_ERROR "RHI: Unsupported platform")
endif()

# Combine all sources
set(RHI_SOURCES
  ${RHI_BACKEND_SOURCES}
)

# ============================================================================
# Create RHI library
# ============================================================================

add_library(pixel_rhi STATIC
  ${RHI_SOURCES}
)

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(pixel_rhi
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================================
# Link dependencies
# ============================================================================

target_link_libraries(pixel_rhi
  PUBLIC
    pixel::core      # Use alias (created in src/core/CMakeLists.txt)
    pixel::platform  # Use alias (created in src/platform/common/CMakeLists.txt)
    ext::glfw        # Use alias (created in third_party/CMakeLists.txt)
)

# Link OpenGL only on platforms where it's used
if(NOT APPLE OR NOT PIXEL_HAS_METAL)
  target_link_libraries(pixel_rhi PUBLIC OpenGL::GL)
endif()

# Metal frameworks (macOS only)
if(APPLE AND PIXEL_HAS_METAL)
  target_link_libraries(pixel_rhi PUBLIC
    ${METAL_LIBRARY}
    ${METALKIT_LIBRARY}
    ${QUARTZCORE_LIBRARY}
    ${COCOA_LIBRARY}
  )
endif()

# ============================================================================
# Platform-specific configuration
# ============================================================================

if(APPLE)
  target_compile_definitions(pixel_rhi PRIVATE
    GL_SILENCE_DEPRECATION
  )

  if(PIXEL_HAS_METAL)
    target_compile_definitions(pixel_rhi PUBLIC
      PIXEL_USE_METAL=1
      PIXEL_RHI_BACKEND_METAL=1
    )
  else()
    target_compile_definitions(pixel_rhi PUBLIC
      PIXEL_RHI_BACKEND_GL33=1
    )
  endif()

  # Enable Objective-C++ for Metal sources
  if(PIXEL_HAS_METAL)
    set_source_files_properties(
      backends/metal/device_metal.mm
      backends/metal/cmd_metal.mm
      backends/metal/pipeline_metal.mm
      PROPERTIES
      COMPILE_FLAGS "-x objective-c++"
    )
  endif()

elseif(WIN32)
  target_compile_definitions(pixel_rhi PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
  target_compile_definitions(pixel_rhi PUBLIC
    PIXEL_RHI_BACKEND_GL43=1
  )

elseif(UNIX)
  target_compile_definitions(pixel_rhi PUBLIC
    PIXEL_RHI_BACKEND_GL43=1
  )
endif()

# ============================================================================
# Compiler settings
# ============================================================================

target_compile_options(pixel_rhi PRIVATE ${PIXEL_WARN_CXX})

if(MSVC)
  target_compile_options(pixel_rhi PRIVATE
    /wd4201  # nonstandard extension used: nameless struct/union
  )
else()
  target_compile_options(pixel_rhi PRIVATE
    -Wno-gnu-anonymous-struct
    -Wno-nested-anon-types
  )
endif()

# ============================================================================
# Create alias (only if not already exists)
# ============================================================================

if(NOT TARGET pixel::rhi)
  add_library(pixel::rhi ALIAS pixel_rhi)
  message(STATUS "RHI: Created pixel::rhi alias")
endif()

# ============================================================================
# Configuration summary
# ============================================================================

message(STATUS "RHI Configuration:")
if(APPLE AND PIXEL_HAS_METAL)
  message(STATUS "  Backend:     Metal (via RHI abstraction)")
  message(STATUS "  API Version: Metal 2.0+")
elseif(APPLE)
  message(STATUS "  Backend:     OpenGL 3.3 Core")
elseif(WIN32 OR UNIX)
  message(STATUS "  Backend:     OpenGL 4.3+")
endif()
