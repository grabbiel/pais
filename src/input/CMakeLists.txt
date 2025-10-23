# Input module - keyboard, mouse, and controller input management
add_library(pixel_input
  input_manager.cpp
)

target_include_directories(pixel_input
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# Dependencies
target_link_libraries(pixel_input
  PUBLIC
    pixel::math    # For Vec2 in InputState
    ext::glfw      # For GLFW input polling
)

# Set compile options
target_compile_features(pixel_input PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(pixel_input PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(pixel_input PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Create alias for consistent naming
if(NOT TARGET pixel::input)
  add_library(pixel::input ALIAS pixel_input)
  message(STATUS "Input: Created pixel::input alias")
endif()
