#include "pixel/renderer3d/renderer.hpp"
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
// ============================================================================
// Texture Array Implementation
// ============================================================================

namespace pixel::renderer3d {
TextureArrayID Renderer::create_texture_array(int width, int height,
                                              int layers) {
  if (layers <= 0 || width <= 0 || height <= 0) {
    throw std::runtime_error("Invalid texture array dimensions");
  }

  uint32_t gl_id;
  glGenTextures(1, &gl_id);
  glBindTexture(GL_TEXTURE_2D_ARRAY, gl_id);

  // Allocate storage for all layers
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, width, height, layers, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

  // Set texture parameters
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  TextureArrayID id = next_texture_array_id_++;
  TextureArrayInfo info;
  info.width = width;
  info.height = height;
  info.layers = layers;
  info.gl_id = gl_id;
  texture_arrays_[id] = info;

  std::cout << "Created texture array: " << width << "x" << height << " with "
            << layers << " layers" << std::endl;

  return id;
}

TextureArrayID
Renderer::load_texture_array(const std::vector<std::string> &paths) {
  if (paths.empty()) {
    throw std::runtime_error(
        "Cannot create texture array from empty path list");
  }

  // Load first texture to get dimensions
  int width, height, channels;
  stbi_set_flip_vertically_on_load(true);
  uint8_t *first_data =
      stbi_load(paths[0].c_str(), &width, &height, &channels, 4);

  if (!first_data) {
    throw std::runtime_error("Failed to load texture: " + paths[0]);
  }

  // Create texture array
  TextureArrayID array_id = create_texture_array(width, height, paths.size());

  // Upload first texture
  set_texture_array_layer(array_id, 0, first_data);
  stbi_image_free(first_data);

  // Load and upload remaining textures
  for (size_t i = 1; i < paths.size(); ++i) {
    int w, h, c;
    uint8_t *data = stbi_load(paths[i].c_str(), &w, &h, &c, 4);

    if (!data) {
      std::cerr << "Warning: Failed to load texture " << paths[i]
                << ", using placeholder" << std::endl;
      // Create a simple colored placeholder
      std::vector<uint8_t> placeholder(width * height * 4);
      for (size_t j = 0; j < placeholder.size(); j += 4) {
        placeholder[j] = (i * 50) % 256;      // R
        placeholder[j + 1] = (i * 100) % 256; // G
        placeholder[j + 2] = (i * 150) % 256; // B
        placeholder[j + 3] = 255;             // A
      }
      set_texture_array_layer(array_id, i, placeholder.data());
      continue;
    }

    if (w != width || h != height) {
      std::cerr << "Warning: Texture " << paths[i]
                << " has different dimensions (" << w << "x" << h
                << ") than expected (" << width << "x" << height
                << "), skipping" << std::endl;
      stbi_image_free(data);
      continue;
    }

    set_texture_array_layer(array_id, i, data);
    stbi_image_free(data);
  }

  // Generate mipmaps
  auto it = texture_arrays_.find(array_id);
  if (it != texture_arrays_.end()) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, it->second.gl_id);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  }

  std::cout << "Loaded texture array with " << paths.size() << " textures"
            << std::endl;

  return array_id;
}

void Renderer::set_texture_array_layer(TextureArrayID array_id, int layer,
                                       const uint8_t *data) {
  auto it = texture_arrays_.find(array_id);
  if (it == texture_arrays_.end()) {
    throw std::runtime_error("Invalid texture array ID");
  }

  const auto &info = it->second;

  if (layer < 0 || layer >= info.layers) {
    throw std::runtime_error("Layer index out of range");
  }

  glBindTexture(GL_TEXTURE_2D_ARRAY, info.gl_id);
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, info.width, info.height,
                  1, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Renderer::bind_texture_array(TextureArrayID id, int slot) {
  auto it = texture_arrays_.find(id);
  if (it != texture_arrays_.end()) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D_ARRAY, it->second.gl_id);
  }
}

TextureArrayInfo Renderer::get_texture_array_info(TextureArrayID id) const {
  auto it = texture_arrays_.find(id);
  return it != texture_arrays_.end() ? it->second : TextureArrayInfo{};
}

TextureID Renderer::load_texture(const std::string &path) {
  auto it = texture_path_to_id_.find(path);
  if (it != texture_path_to_id_.end())
    return it->second;

  int width, height, channels;
  stbi_set_flip_vertically_on_load(true);
  uint8_t *data = stbi_load(path.c_str(), &width, &height, &channels, 4);

  if (!data)
    throw std::runtime_error("Failed to load texture: " + path);

  TextureID id = create_texture(width, height, data);
  stbi_image_free(data);

  texture_path_to_id_[path] = id;
  return id;
}

TextureID Renderer::create_texture(int width, int height, const uint8_t *data) {
  uint32_t gl_id;
  glGenTextures(1, &gl_id);
  glBindTexture(GL_TEXTURE_2D, gl_id);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  TextureID id = next_texture_id_++;
  TextureInfo info;
  info.width = width;
  info.height = height;
  info.gl_id = gl_id;
  textures_[id] = info;

  return id;
}

void Renderer::bind_texture(TextureID id, int slot) {
  auto it = textures_.find(id);
  if (it != textures_.end()) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, it->second.gl_id);
  }
}
} // namespace pixel::renderer3d
