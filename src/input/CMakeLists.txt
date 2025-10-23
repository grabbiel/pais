# src/input/CMakeLists.txt
# Input module - platform-independent input handling

add_library(pixel_input STATIC
  input_manager.cpp
)

target_include_directories(pixel_input PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(pixel_input PUBLIC
  pixel_math
  ext::glfw
)

# Compiler warnings
target_compile_options(pixel_input PRIVATE ${PIXEL_WARN_CXX})
