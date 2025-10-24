#./CMakeLists.txt
cmake_minimum_required(VERSION 3.24)
project(pixel_life LANGUAGES C CXX)

# ============================================================================
# C++ Standard and Build Settings
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if(MSVC)
  set(PIXEL_WARN_CXX /W4 /permissive- /Zc:__cplusplus)
else()
  set(PIXEL_WARN_CXX -Wall -Wextra -Wpedantic)
endif()

# ============================================================================
# Build Type Configuration
# ============================================================================
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ============================================================================
# Platform Detection
# ============================================================================
set(PIXEL_USE_METAL_DEFAULT OFF)

if(APPLE)
  set(PIXEL_PLATFORM_APPLE ON)
  message(STATUS "Platform: macOS")

  set(PIXEL_USE_METAL_DEFAULT ON)

  # macOS deployment target
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "macOS deployment target" FORCE)
  endif()
  message(STATUS "macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
elseif(WIN32)
  set(PIXEL_PLATFORM_WINDOWS ON)
  message(STATUS "Platform: Windows")
elseif(UNIX)
  set(PIXEL_PLATFORM_LINUX ON)
  message(STATUS "Platform: Linux")
endif()

option(PIXEL_USE_METAL "Enable the Metal graphics backend" ${PIXEL_USE_METAL_DEFAULT})

set(PIXEL_USE_DX12_DEFAULT OFF)
if(WIN32)
  set(PIXEL_USE_DX12_DEFAULT OFF)
endif()
option(PIXEL_USE_DX12 "Enable the DirectX 12 graphics backend" ${PIXEL_USE_DX12_DEFAULT})

if(PIXEL_USE_METAL AND PIXEL_USE_DX12)
  message(FATAL_ERROR "PIXEL_USE_METAL and PIXEL_USE_DX12 cannot be enabled at the same time.")
endif()

# ============================================================================
# Dependencies
# ============================================================================

# GLFW - check if system package is available
find_package(glfw3 3.3 QUIET)
if(glfw3_FOUND)
  message(STATUS "GLFW: Found system package (${glfw3_VERSION})")
  set(PIXEL_GLFW_SYSTEM ON)
  # Note: glfw3 package may create 'glfw' target, will verify in third_party
else()
  message(STATUS "GLFW: System package not found, will fetch from source")
  set(PIXEL_GLFW_SYSTEM OFF)
endif()

# GLM - check if system package is available
find_package(glm QUIET)
if(glm_FOUND)
  message(STATUS "GLM: Found system package")
  set(PIXEL_GLM_SYSTEM ON)
  # Note: glm package creates 'glm::glm' or 'glm' target
else()
  message(STATUS "GLM: System package not found, will fetch from source")
  set(PIXEL_GLM_SYSTEM OFF)
endif()

# Platform-specific frameworks (macOS)
if(APPLE AND PIXEL_USE_METAL)
  find_library(METAL_LIBRARY Metal)
  find_library(METALKIT_LIBRARY MetalKit)
  find_library(QUARTZCORE_LIBRARY QuartzCore)
  find_library(COCOA_LIBRARY Cocoa)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)

  if(METAL_LIBRARY AND METALKIT_LIBRARY)
    message(STATUS "Metal frameworks found - Metal backend available")
    set(PIXEL_HAS_METAL ON)
  else()
    message(FATAL_ERROR "Metal frameworks not found. The OpenGL backend has been removed, so Metal support is required.")
  endif()
elseif(PIXEL_USE_METAL)
  message(WARNING "PIXEL_USE_METAL=ON but the current platform does not support Metal.")
endif()

# ============================================================================
# Global compile definitions
# ============================================================================
add_compile_definitions(
  $<$<CONFIG:Debug>:PIXEL_DEBUG=1>
  $<$<CONFIG:Release>:PIXEL_RELEASE=1>
  $<$<PLATFORM_ID:Darwin>:PIXEL_PLATFORM_MACOS=1>
  $<$<PLATFORM_ID:Windows>:PIXEL_PLATFORM_WINDOWS=1>
  $<$<PLATFORM_ID:Linux>:PIXEL_PLATFORM_LINUX=1>
)

# Silence OpenGL deprecation warnings on macOS (still suppresses legacy headers
# that some dependencies may include)
if(APPLE)
  add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

# ============================================================================
# Source subdirectories (ORDER MATTERS!)
# ============================================================================

# STEP 1: Third-party dependencies FIRST (imports/fetches packages)
add_subdirectory(third_party)

# STEP 1.5: Create ext::* aliases at ROOT scope (visible to all subdirectories)
if(NOT TARGET ext::glfw)
  if(TARGET glfw)
    add_library(ext::glfw ALIAS glfw)
    message(STATUS "Root: Created ext::glfw alias")
  else()
    message(FATAL_ERROR "Root: glfw target not found after third_party configuration")
  endif()
endif()

if(NOT TARGET ext::glm)
  if(TARGET glm::glm)
    add_library(ext::glm ALIAS glm::glm)
    message(STATUS "Root: Created ext::glm alias")
  elseif(TARGET glm)
    add_library(ext::glm ALIAS glm)
    message(STATUS "Root: Created ext::glm alias (from glm)")
  else()
    message(FATAL_ERROR "Root: glm target not found after third_party configuration")
  endif()
endif()

if(NOT TARGET ext::stb)
  if(TARGET stb_impl)
    add_library(ext::stb ALIAS stb_impl)
    message(STATUS "Root: Created ext::stb alias")
  else()
    message(FATAL_ERROR "Root: stb_impl target not found after third_party configuration")
  endif()
endif()

# STEP 2: Core libraries (no dependencies on other pixel:: targets)
add_subdirectory(src/core)
add_subdirectory(src/math)
add_subdirectory(src/input)
add_subdirectory(src/platform/common)
add_subdirectory(src/telemetry)

# STEP 3: Schemas (optional, depends on flatbuffers if available)
add_subdirectory(schemas)

# STEP 4: RHI (depends on core, platform, third-party)
add_subdirectory(src/rhi)

# STEP 5: Resources (depends on RHI)
add_subdirectory(src/resources)

# STEP 6: Renderer (depends on RHI, Resources, and everything below it)
add_subdirectory(src/renderer3d)

# STEP 7: Application (depends on everything)
add_subdirectory(src/app)

# ============================================================================
# Clangd configuration (after all subdirectories)
# ============================================================================
set(_clangd_path "${CMAKE_SOURCE_DIR}/.clangd")
file(WRITE "${_clangd_path}"
"CompileFlags:
  CompilationDatabase: ${CMAKE_BINARY_DIR}
  Add:
    - -std=c++20
    - -I${CMAKE_SOURCE_DIR}/include
    - -I${CMAKE_BINARY_DIR}/_deps/glfw-src/include
    - -I${CMAKE_BINARY_DIR}/_deps/glm-src
    - -I${CMAKE_SOURCE_DIR}/third_party/stb
")
message(STATUS "Clangd config: ${_clangd_path}")

# ============================================================================
# Testing
# ============================================================================
option(PIXEL_BUILD_TESTS "Build unit tests" ON)

if(PIXEL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/unit)
  message(STATUS "Unit tests: ENABLED")
else()
  message(STATUS "Unit tests: DISABLED")
endif()

# ============================================================================
# Installation
# ============================================================================
install(TARGETS pixel_life
  RUNTIME DESTINATION bin
  BUNDLE DESTINATION .
)

# Install resources on macOS
if(APPLE)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets
    DESTINATION pixel_life.app/Contents/Resources
    OPTIONAL
  )
endif()

# ============================================================================
# Configuration Summary
# ============================================================================
message(STATUS "")
message(STATUS "=== Pixel-Life Configuration Summary ===")
message(STATUS "")
message(STATUS "Build Configuration:")
message(STATUS "  Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:      C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:          ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
message(STATUS "Platform:")
if(APPLE)
  message(STATUS "  Target:            macOS ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
message(STATUS "")
message(STATUS "Graphics Backend:")
if(PIXEL_HAS_METAL)
  message(STATUS "  Primary:           Metal (macOS native)")
endif()
message(STATUS "")
message(STATUS "Dependencies:")
if(PIXEL_GLFW_SYSTEM)
  message(STATUS "  GLFW:              System")
else()
  message(STATUS "  GLFW:              FetchContent")
endif()
if(PIXEL_GLM_SYSTEM)
  message(STATUS "  GLM:               System")
else()
  message(STATUS "  GLM:               FetchContent")
endif()
message(STATUS "  STB:               Bundled")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  RHI:               ENABLED")
message(STATUS "  3D Renderer:       ENABLED")
message(STATUS "  Instancing:        ENABLED")
message(STATUS "  LOD System:        ENABLED")
message(STATUS "  Unit Tests:        ${PIXEL_BUILD_TESTS}")
message(STATUS "")
message(STATUS "========================================")
message(STATUS "")
