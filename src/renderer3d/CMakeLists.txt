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
# Shader compilation (GLSL/HLSL -> SPIR-V)
# ============================================================================

find_program(PIXEL_GLSLANG_VALIDATOR glslangValidator)
if(NOT PIXEL_GLSLANG_VALIDATOR)
  message(FATAL_ERROR
          "glslangValidator not found. Install the Vulkan SDK or glslang tools")
endif()

function(pixel_make_variant_suffix OUT_VAR DEFINE_STRING)
  if("${DEFINE_STRING}" STREQUAL "")
    set(${OUT_VAR} "" PARENT_SCOPE)
    return()
  endif()

  string(REPLACE "," ";" _define_list "${DEFINE_STRING}")
  set(_suffix "")
  foreach(_define ${_define_list})
    if(_define STREQUAL "")
      continue()
    endif()
    string(REPLACE "=" ";" _pair "${_define}")
    list(LENGTH _pair _pair_len)
    list(GET _pair 0 _name)
    if(_pair_len GREATER 1)
      list(GET _pair 1 _value)
    else()
      set(_value "1")
    endif()
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _name "${_name}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _value "${_value}")
    if(_suffix STREQUAL "")
      set(_suffix "${_name}_${_value}")
    else()
      set(_suffix "${_suffix}__${_name}_${_value}")
    endif()
  endforeach()
  set(${OUT_VAR} "${_suffix}" PARENT_SCOPE)
endfunction()

set(PIXEL_SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/assets/shaders)
set(PIXEL_SHADER_VARIANTS
  "${PIXEL_SHADER_SOURCE_DIR}/default.vert|"
  "${PIXEL_SHADER_SOURCE_DIR}/default.frag|"
  "${PIXEL_SHADER_SOURCE_DIR}/instanced.vert|"
  "${PIXEL_SHADER_SOURCE_DIR}/instanced.frag|"
  "${PIXEL_SHADER_SOURCE_DIR}/shadow_depth.vert|"
  "${PIXEL_SHADER_SOURCE_DIR}/shadow_depth.frag|"
  "${PIXEL_SHADER_SOURCE_DIR}/shadow_depth_instanced.vert|"
  "${PIXEL_SHADER_SOURCE_DIR}/shadow_depth_instanced.frag|"
  "${PIXEL_SHADER_SOURCE_DIR}/culling.comp|"
  "${PIXEL_SHADER_SOURCE_DIR}/lod.comp|"
)

set(PIXEL_COMPILED_SHADERS)

foreach(_entry ${PIXEL_SHADER_VARIANTS})
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 0 _shader_file)
  list(LENGTH _parts _part_count)
  if(_part_count GREATER 1)
    list(GET _parts 1 _define_string)
  else()
    set(_define_string "")
  endif()

  pixel_make_variant_suffix(_variant_suffix "${_define_string}")

  get_filename_component(_shader_name ${_shader_file} NAME)
  get_filename_component(_shader_ext ${_shader_file} EXT)

  if(_shader_ext STREQUAL ".vert")
    set(_shader_stage vert)
  elseif(_shader_ext STREQUAL ".frag")
    set(_shader_stage frag)
  elseif(_shader_ext STREQUAL ".comp")
    set(_shader_stage comp)
  else()
    message(FATAL_ERROR "Unsupported shader extension '${_shader_ext}' for ${_shader_file}")
  endif()

  if(_variant_suffix STREQUAL "")
    set(_output_name "${_shader_name}.spv")
  else()
    set(_output_name "${_shader_name}__${_variant_suffix}.spv")
  endif()

  set(_output_file ${PIXEL_SHADER_SPIRV_DIR}/${_output_name})

  set(_define_args)
  if(NOT _define_string STREQUAL "")
    string(REPLACE "," ";" _define_list "${_define_string}")
    foreach(_define ${_define_list})
      if(_define STREQUAL "")
        continue()
      endif()
      string(REPLACE "=" ";" _pair "${_define}")
      list(LENGTH _pair _pair_len)
      list(GET _pair 0 _def_name)
      if(_pair_len GREATER 1)
        list(GET _pair 1 _def_value)
      else()
        set(_def_value "1")
      endif()
      list(APPEND _define_args "-D${_def_name}=${_def_value}")
    endforeach()
  endif()

  add_custom_command(
    OUTPUT ${_output_file}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PIXEL_SHADER_SPIRV_DIR}
    COMMAND ${PIXEL_GLSLANG_VALIDATOR}
            -V ${_shader_file}
            -S ${_shader_stage}
            ${_define_args}
            -o ${_output_file}
    DEPENDS ${_shader_file}
    COMMENT "Compiling ${_shader_name} (${_shader_stage}) -> ${_output_name}"
    VERBATIM
  )

  list(APPEND PIXEL_COMPILED_SHADERS ${_output_file})
endforeach()

if(PIXEL_COMPILED_SHADERS)
  add_custom_target(pixel_renderer3d_shaders ALL
    DEPENDS ${PIXEL_COMPILED_SHADERS}
  )
  add_dependencies(pixel_renderer3d_shaders pixel_stage_assets)
  add_dependencies(pixel_renderer3d pixel_renderer3d_shaders)
endif()

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
    ext::spirv_reflect
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

if(PIXEL_USE_VULKAN)
  target_compile_definitions(pixel_renderer3d PUBLIC
    PIXEL_USE_VULKAN=1
  )
  message(STATUS "Renderer3D: Vulkan backend support enabled")
  set(RENDERER3D_BACKEND_NAME "Vulkan")
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
elseif(RENDERER3D_BACKEND_NAME STREQUAL "Vulkan")
  message(STATUS "  Graphics API:      Vulkan")
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
