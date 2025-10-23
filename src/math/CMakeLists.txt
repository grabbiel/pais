# Math module - foundational math types and operations
add_library(pixel_math
  vec2.cpp
  vec3.cpp
)

target_include_directories(pixel_math
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# GLM dependency for Vec3 interop
target_link_libraries(pixel_math
  PUBLIC
    ext::glm
)

# Set compile options
target_compile_features(pixel_math PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(pixel_math PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(pixel_math PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Create alias for consistent naming
if(NOT TARGET pixel::math)
  add_library(pixel::math ALIAS pixel_math)
  message(STATUS "Math: Created pixel::math alias")
endif()
