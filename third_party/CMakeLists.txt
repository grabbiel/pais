# third_party/CMakeLists.txt
# ============================================================================
# Third-party Dependencies
# ============================================================================

include(FetchContent)

# ============================================================================
# GLFW - Windowing and Input
# ============================================================================

if(NOT TARGET ext::glfw)
  if(PIXEL_GLFW_SYSTEM)
    # System package found - it creates 'glfw' target
    message(STATUS "GLFW: Using system package")

    # Verify the target exists
    if(NOT TARGET glfw)
      message(FATAL_ERROR "GLFW system package found but 'glfw' target not available")
    endif()
  else()
    # Fetch from source
    message(STATUS "GLFW: Fetching from source...")

    FetchContent_Declare(
      glfw
      GIT_REPOSITORY https://github.com/glfw/glfw.git
      GIT_TAG        3.4
      GIT_SHALLOW    TRUE
    )

    # Configure GLFW options
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(glfw)

    message(STATUS "GLFW: Fetched from source")
  endif()

  # Create alias target - should work for both cases now
  if(TARGET glfw)
    add_library(ext::glfw ALIAS glfw)
    message(STATUS "GLFW: Created ext::glfw -> glfw alias")
  else()
    message(FATAL_ERROR "GLFW: 'glfw' target not found. Available targets: ${CMAKE_PROJECT_NAME}")
  endif()
endif()

# ============================================================================
# GLM - Mathematics Library
# ============================================================================

if(NOT TARGET ext::glm)
  if(NOT PIXEL_GLM_SYSTEM)
    message(STATUS "Fetching GLM from source...")

    FetchContent_Declare(
      glm
      GIT_REPOSITORY https://github.com/g-truc/glm.git
      GIT_TAG        1.0.1
      GIT_SHALLOW    TRUE
    )

    # Configure GLM options
    set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(glm)

    message(STATUS "GLM: Fetched from source")
  else()
    message(STATUS "GLM: Using system package")
  endif()

  # Create alias target (try different possible target names)
  if(TARGET glm::glm)
    add_library(ext::glm ALIAS glm::glm)
    message(STATUS "GLM: Created ext::glm -> glm::glm alias")
  elseif(TARGET glm)
    add_library(ext::glm ALIAS glm)
    message(STATUS "GLM: Created ext::glm -> glm alias")
  else()
    # Header-only fallback
    add_library(ext_glm INTERFACE)
    target_include_directories(ext_glm INTERFACE ${GLM_INCLUDE_DIRS})
    add_library(ext::glm ALIAS ext_glm)
    message(STATUS "GLM: Created interface library with includes")
  endif()
endif()

# ============================================================================
# STB - Image Loading (Header-only)
# ============================================================================

if(NOT TARGET ext::stb)
  # STB is header-only, just set up include directory
  set(STB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stb)

  # If the bundled header isn't present, fetch from source
  if(NOT EXISTS ${STB_INCLUDE_DIR}/stb_image.h)
    message(STATUS "STB: bundled headers not found; fetching from source...")
    include(FetchContent)
    FetchContent_Declare(
      stb
      GIT_REPOSITORY https://github.com/nothings/stb.git
      GIT_TAG        master
      GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(stb)
    # The repo places headers at its root (stb_image.h, etc.)
    set(STB_INCLUDE_DIR ${stb_SOURCE_DIR})
  endif()

  # Create interface library for STB
  add_library(stb_impl INTERFACE)
  target_include_directories(stb_impl INTERFACE ${STB_INCLUDE_DIR})

  # Create alias
  add_library(ext::stb ALIAS stb_impl)

  message(STATUS "STB: Using bundled headers at ${STB_INCLUDE_DIR}")
endif()

# ============================================================================
# FlatBuffers (Optional - for networking)
# ============================================================================

if(EXISTS ${CMAKE_SOURCE_DIR}/schemas/flatbuffers AND NOT TARGET flatbuffers)
  find_package(flatbuffers QUIET)

  if(NOT flatbuffers_FOUND)
    message(STATUS "Fetching FlatBuffers from source...")

    FetchContent_Declare(
      flatbuffers
      GIT_REPOSITORY https://github.com/google/flatbuffers.git
      GIT_TAG        v24.3.25
      GIT_SHALLOW    TRUE
    )

    set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(flatbuffers)

    message(STATUS "FlatBuffers: Fetched from source")
  else()
    message(STATUS "FlatBuffers: Using system package")
  endif()
endif()

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "Third-party dependencies configured:")
if(PIXEL_GLFW_SYSTEM)
  message(STATUS "  GLFW:        System package")
else()
  message(STATUS "  GLFW:        FetchContent")
endif()
if(PIXEL_GLM_SYSTEM)
  message(STATUS "  GLM:         System package")
else()
  message(STATUS "  GLM:         FetchContent")
endif()
message(STATUS "  STB:         Bundled (header-only)")
if(TARGET flatbuffers)
  message(STATUS "  FlatBuffers: Available")
endif()
message(STATUS "")

# ============================================================================
# SPIRV-Reflect - Shader reflection for SPIR-V binaries
# ============================================================================

if(NOT TARGET ext::spirv_reflect)
  message(STATUS "Fetching SPIRV-Reflect from source...")

  FetchContent_Declare(
    spirv_reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG        v1.3.275.0
    GIT_SHALLOW    TRUE
  )

  FetchContent_MakeAvailable(spirv_reflect)

  add_library(spirv_reflect STATIC
    ${spirv_reflect_SOURCE_DIR}/spirv_reflect.c
  )
  target_include_directories(spirv_reflect PUBLIC ${spirv_reflect_SOURCE_DIR})
  set_target_properties(spirv_reflect PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
  )

  add_library(ext::spirv_reflect ALIAS spirv_reflect)
  message(STATUS "SPIRV-Reflect: Fetched and configured")
endif()
