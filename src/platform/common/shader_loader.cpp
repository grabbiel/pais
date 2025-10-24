#include "pixel/platform/shader_loader.hpp"
#include "pixel/platform/resources.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace pixel::platform {

std::string load_shader_file(const std::string &relative_path) {
  // Get full path to shader file using resource system
  std::string full_path = get_resource_file(relative_path);

  // Open the file
  std::ifstream file(full_path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open shader file: " + full_path);
  }

  // Read the entire file into a string
  std::stringstream buffer;
  buffer << file.rdbuf();
  file.close();

  std::string source = buffer.str();
  if (source.empty()) {
    throw std::runtime_error("Shader file is empty: " + full_path);
  }

  return source;
}

std::pair<std::string, std::string>
load_shader_pair(const std::string &vert_path, const std::string &frag_path) {
  std::string vert_source = load_shader_file(vert_path);
  std::string frag_source = load_shader_file(frag_path);
  return {vert_source, frag_source};
}

std::vector<uint8_t>
load_shader_bytecode(const std::string &relative_path) {
  std::string full_path = get_resource_file(relative_path);

  std::ifstream file(full_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open shader bytecode: " + full_path);
  }

  std::streamsize size = file.tellg();
  if (size <= 0) {
    throw std::runtime_error("Shader bytecode is empty: " + full_path);
  }
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char *>(data.data()), size)) {
    throw std::runtime_error("Failed to read shader bytecode: " + full_path);
  }

  return data;
}

} // namespace pixel::platform
