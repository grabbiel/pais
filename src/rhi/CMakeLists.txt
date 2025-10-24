# src/rhi/CMakeLists.txt
# ============================================================================
# Pixel Life - RHI (Render Hardware Interface) Library
# ============================================================================

# ============================================================================
# Platform-specific backend selection
# ============================================================================

if(APPLE)
  if(PIXEL_HAS_METAL)
    message(STATUS "RHI: Configuring for macOS/ARM64")
    set(RHI_BACKEND_SOURCES
      backends/metal/device_metal.mm
      backends/metal/cmd_metal.mm
      backends/metal/resources_metal.mm
      backends/metal/pipeline_metal.mm
      backends/metal/state_metal.mm
    )
    message(STATUS "RHI: Using Metal backend (RHI implementation)")
  else()
    message(FATAL_ERROR "RHI: Metal frameworks not found. OpenGL backend has been removed.")
  endif()
else()
  message(FATAL_ERROR "RHI: Only the Metal backend is available after removing OpenGL.")
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
    pixel::core
    pixel::platform
    ext::glfw
)

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
  endif()
endif()

# Objective-C++ for Metal sources
if(APPLE AND PIXEL_HAS_METAL)
  set_source_files_properties(
    backends/metal/device_metal.mm
    backends/metal/cmd_metal.mm
    backends/metal/resources_metal.mm
    backends/metal/pipeline_metal.mm
    backends/metal/state_metal.mm
    PROPERTIES
    COMPILE_FLAGS "-x objective-c++"
  )
endif()

# ============================================================================
# Compiler settings
# ============================================================================

target_compile_options(pixel_rhi PRIVATE ${PIXEL_WARN_CXX})

if(MSVC)
  target_compile_options(pixel_rhi PRIVATE
    /wd4201
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

if(APPLE AND PIXEL_HAS_METAL)
  message(STATUS "RHI Configuration:")
  message(STATUS "  Backend:     Metal (via RHI abstraction)")
  message(STATUS "  API Version: Metal 2.0+")
endif()
