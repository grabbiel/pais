# src/rhi/CMakeLists.txt
# ============================================================================
# Pixel Life - RHI (Render Hardware Interface) Library
# ============================================================================

# ============================================================================
# Platform-specific backend selection
# ============================================================================

set(RHI_BACKEND_SOURCES)
set(RHI_BACKEND_LIBS)
set(RHI_BACKEND_NAME "")

if(PIXEL_USE_DX12)
  if(WIN32)
    message(STATUS "RHI: Configuring for Windows (DirectX 12)")
    set(RHI_BACKEND_SOURCES
      backends/dx12/device_dx12.cpp
    )
    set(RHI_BACKEND_NAME "DirectX 12")
    list(APPEND RHI_BACKEND_LIBS d3d12 dxgi dxguid)
  else()
    message(FATAL_ERROR "RHI: DirectX 12 backend requested but the current platform is not Windows.")
  endif()
elseif(PIXEL_USE_METAL)
  if(APPLE)
    if(PIXEL_HAS_METAL)
      message(STATUS "RHI: Configuring for macOS")
      set(RHI_BACKEND_SOURCES
        backends/metal/device_metal.mm
        backends/metal/cmd_metal.mm
        backends/metal/resources_metal.mm
        backends/metal/pipeline_metal.mm
        backends/metal/state_metal.mm
      )
      message(STATUS "RHI: Using Metal backend (RHI implementation)")
      set(RHI_BACKEND_NAME "Metal")
    else()
      message(FATAL_ERROR "RHI: Metal frameworks not found while PIXEL_USE_METAL=ON.")
    endif()
  else()
    message(FATAL_ERROR "RHI: Metal backend requested but the current platform does not support Metal.")
  endif()
elseif(PIXEL_USE_VULKAN)
  if(PIXEL_HAS_VULKAN)
    message(STATUS "RHI: Configuring for Vulkan")
    set(RHI_BACKEND_SOURCES
      backends/vulkan/device_vulkan.cpp
    )
    set(RHI_BACKEND_NAME "Vulkan")
    list(APPEND RHI_BACKEND_LIBS Vulkan::Vulkan)
  else()
    message(FATAL_ERROR "RHI: Vulkan backend requested but Vulkan SDK was not detected.")
  endif()
else()
  message(FATAL_ERROR "RHI: No graphics backend selected. Enable PIXEL_USE_METAL, PIXEL_USE_VULKAN, or PIXEL_USE_DX12.")
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

if(RHI_BACKEND_LIBS)
  target_link_libraries(pixel_rhi PUBLIC ${RHI_BACKEND_LIBS})
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
  endif()
elseif(PIXEL_USE_DX12 AND WIN32)
  target_compile_definitions(pixel_rhi PUBLIC
    PIXEL_USE_DX12=1
    PIXEL_RHI_BACKEND_DX12=1
  )
elseif(PIXEL_USE_VULKAN)
  target_compile_definitions(pixel_rhi PUBLIC
    PIXEL_USE_VULKAN=1
    PIXEL_RHI_BACKEND_VULKAN=1
  )
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

if(RHI_BACKEND_NAME)
  message(STATUS "RHI Configuration:")
  message(STATUS "  Backend:     ${RHI_BACKEND_NAME} (via RHI abstraction)")
  if(RHI_BACKEND_NAME STREQUAL "Metal")
    message(STATUS "  API Version: Metal 2.0+")
  elseif(RHI_BACKEND_NAME STREQUAL "DirectX 12")
    message(STATUS "  API Version: DirectX 12")
  elseif(RHI_BACKEND_NAME STREQUAL "Vulkan")
    message(STATUS "  API Version: Vulkan 1.x")
  endif()
endif()
