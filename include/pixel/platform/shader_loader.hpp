#pragma once
#include <string>
#include <vector>

namespace pixel::platform {

// Load a shader file from disk
// Returns the shader source code as a string
// Example: load_shader_file("shaders/basic.vert")
// Throws std::runtime_error if file cannot be read
std::string load_shader_file(const std::string &relative_path);

// Load vertex and fragment shader files
// Returns a pair of strings: {vertex_source, fragment_source}
// Example: load_shader_pair("shaders/basic.vert", "shaders/basic.frag")
// Throws std::runtime_error if either file cannot be read
std::pair<std::string, std::string>
load_shader_pair(const std::string &vert_path, const std::string &frag_path);

// Load compiled shader bytecode
std::vector<uint8_t>
load_shader_bytecode(const std::string &relative_path);

} // namespace pixel::platform
