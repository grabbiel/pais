# src/app/CMakeLists.txt
# ============================================================================
# Pixel Life - Main Application
# ============================================================================

add_executable(pixel_life
  main.cpp
)

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(pixel_life PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

# ============================================================================
# Link dependencies
# ============================================================================

# Link to renderer3d - it will bring in RHI and other dependencies transitively
target_link_libraries(pixel_life PRIVATE
  pixel::renderer3d  # CHANGED: Link to renderer3d instead of just RHI
  pixel_input        # For InputManager
  pixel::core        # For clock and other utilities
  pixel::platform    # For platform utilities
)

# ============================================================================
# Compiler settings
# ============================================================================

target_compile_options(pixel_life PRIVATE ${PIXEL_WARN_CXX})

# ============================================================================
# Platform-specific configuration
# ============================================================================

if(APPLE)
  # ============================================================
  # macOS Bundle Configuration
  # ============================================================

  set_target_properties(pixel_life PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/platform/apple/Info.plist
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.pixel-life"
    MACOSX_BUNDLE_BUNDLE_NAME "Pixel Life"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  )

  # Copy app icon if it exists
  set(_icon_path ${CMAKE_SOURCE_DIR}/src/platform/apple/AppIcon.icns)
  if(EXISTS ${_icon_path})
    set_source_files_properties(${_icon_path}
      PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(pixel_life PRIVATE ${_icon_path})
    message(STATUS "macOS: App icon found and will be included")
  else()
    message(STATUS "macOS: No app icon found at ${_icon_path}")
  endif()

  # Copy assets to bundle Resources
  if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    add_custom_command(TARGET pixel_life POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_BUNDLE_CONTENT_DIR:pixel_life>/Resources/assets
      COMMENT "Copying assets into macOS app bundle Resources"
      VERBATIM
    )
    message(STATUS "macOS: Assets will be copied to app bundle")
  else()
    message(WARNING "macOS: assets/ directory not found - textures may fail to load")
  endif()

  # ============================================================
  # Metal Shader Compilation (NEW)
  # ============================================================



  if(PIXEL_HAS_METAL)
    set(METAL_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/assets/shaders/metal/shaders.metal")
    set(METAL_AIR_FILE "${CMAKE_BINARY_DIR}/shaders.air")
    set(METAL_LIB_FILE "${CMAKE_BINARY_DIR}/default.metallib")

    # Check if shader source exists - FAIL FAST if missing
    if(NOT EXISTS ${METAL_SHADER_SOURCE})
      message(FATAL_ERROR "Metal shader source not found at: ${METAL_SHADER_SOURCE}")
    endif()

    # Determine correct Metal standard based on deployment target
    # NOTE: Use macos-metal* or osx-metal* prefix for macOS
    if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "11.0")
      set(METAL_STD "macos-metal2.0")  # macOS 10.15 supports Metal 2.0+
      set(MIN_OS "10.15")
      message(STATUS "Using Metal 2.0 for macOS ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    elseif(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "12.0")
      set(METAL_STD "macos-metal2.4")  # macOS 11.x supports Metal 2.4
      set(MIN_OS "11.0")
      message(STATUS "Using Metal 2.4 for macOS ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    elseif(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "13.0")
      set(METAL_STD "metal3.0")  # macOS 12+ supports Metal 3.0
      set(MIN_OS "12.0")
      message(STATUS "Using Metal 3.0 for macOS ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
      set(METAL_STD "metal3.1")  # macOS 13+ supports Metal 3.1+
      set(MIN_OS "13.0")
      message(STATUS "Using Metal 3.1 for macOS ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()

    # Step 1: Compile .metal to .air (Metal IR)
    add_custom_command(
      OUTPUT ${METAL_AIR_FILE}
      COMMAND xcrun -sdk macosx metal
              -c ${METAL_SHADER_SOURCE}
              -o ${METAL_AIR_FILE}
              -std=${METAL_STD}
              -mmacos-version-min=${MIN_OS}
              -ffast-math
              -O2
      DEPENDS ${METAL_SHADER_SOURCE}
      COMMENT "Compiling Metal shader to IR (${METAL_STD})..."
      VERBATIM
      COMMAND_EXPAND_LISTS
    )

    # Step 2: Link .air to .metallib
    add_custom_command(
      OUTPUT ${METAL_LIB_FILE}
      COMMAND xcrun -sdk macosx metallib
              ${METAL_AIR_FILE}
              -o ${METAL_LIB_FILE}
      DEPENDS ${METAL_AIR_FILE}
      COMMENT "Creating Metal library (default.metallib)..."
      VERBATIM
    )

    # Custom target to ensure shaders are built
    add_custom_target(CompileMetalShaders ALL
      DEPENDS ${METAL_LIB_FILE}
    )

    # Make pixel_life depend on shader compilation
    add_dependencies(pixel_life CompileMetalShaders)

    # Step 3: Copy metallib to app bundle Resources
    add_custom_command(TARGET pixel_life POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              $<TARGET_BUNDLE_CONTENT_DIR:pixel_life>/Resources
      COMMAND ${CMAKE_COMMAND} -E copy
              ${METAL_LIB_FILE}
              $<TARGET_BUNDLE_CONTENT_DIR:pixel_life>/Resources/default.metallib
      COMMENT "Installing Metal library to app bundle..."
      VERBATIM
    )

    message(STATUS "Metal shader configuration:")
    message(STATUS "  Standard:     ${METAL_STD}")
    message(STATUS "  Min macOS:    ${MIN_OS}")
    message(STATUS "  Source:       ${METAL_SHADER_SOURCE}")
    message(STATUS "  Output:       $<TARGET_BUNDLE_CONTENT_DIR:pixel_life>/Resources/default.metallib")
  endif()

  # Link CoreFoundation for resource path utilities
  target_link_libraries(pixel_life PRIVATE "-framework CoreFoundation")

  # Metal backend info
  if(PIXEL_HAS_METAL)
    message(STATUS "macOS: Building with Metal backend support")
  endif()

elseif(WIN32)
  # ============================================================
  # Windows Configuration
  # ============================================================

  # Uncomment to hide console window:
  # set_target_properties(pixel_life PROPERTIES WIN32_EXECUTABLE TRUE)

  # Copy assets next to executable
  if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    add_custom_command(TARGET pixel_life POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:pixel_life>/assets
      COMMENT "Copying assets to Windows build directory"
      VERBATIM
    )
    message(STATUS "Windows: Assets will be copied to build directory")
  else()
    message(WARNING "Windows: assets/ directory not found")
  endif()

  # Copy GLFW DLL if using shared library
  # (Usually not needed with static linking, but good to have)
  if(TARGET glfw AND NOT glfw_STATIC)
    add_custom_command(TARGET pixel_life POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:glfw>
        $<TARGET_FILE_DIR:pixel_life>
      COMMENT "Copying GLFW DLL to build directory"
      VERBATIM
    )
  endif()

elseif(UNIX)
  # ============================================================
  # Linux Configuration
  # ============================================================

  # Copy assets next to executable
  if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    add_custom_command(TARGET pixel_life POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:pixel_life>/assets
      COMMENT "Copying assets to Linux build directory"
      VERBATIM
    )
    message(STATUS "Linux: Assets will be copied to build directory")
  else()
    message(WARNING "Linux: assets/ directory not found")
  endif()

  # Linux desktop entry (optional)
  set(_desktop_file ${CMAKE_SOURCE_DIR}/packaging/pixel-life.desktop)
  if(EXISTS ${_desktop_file})
    install(FILES ${_desktop_file}
      DESTINATION share/applications
    )
  endif()

endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS pixel_life
  RUNTIME DESTINATION bin
  BUNDLE DESTINATION .
)

# Install assets
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
  if(APPLE)
    # Assets are already bundled for macOS
  else()
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets
      DESTINATION share/pixel-life
    )
  endif()
endif()

# ============================================================================
# Configuration Summary
# ============================================================================

message(STATUS "")
message(STATUS "Application Configuration:")
message(STATUS "  Executable:        pixel_life")

if(APPLE)
  message(STATUS "  Package Type:      macOS Bundle (.app)")
  if(PIXEL_HAS_METAL)
    message(STATUS "  Graphics:          Metal + OpenGL fallback")
  else()
    message(STATUS "  Graphics:          OpenGL 3.3 Core")
  endif()
elseif(WIN32)
  message(STATUS "  Package Type:      Windows Executable (.exe)")
  message(STATUS "  Graphics:          OpenGL 3.3 Core")
elseif(UNIX)
  message(STATUS "  Package Type:      Linux Executable")
  message(STATUS "  Graphics:          OpenGL 3.3 Core")
endif()

message(STATUS "  Dependencies:      Linked transitively via renderer3d")
message(STATUS "")
