#include "pixel/resources/texture_loader.hpp"
#include "pixel/rhi/rhi.hpp"
#include "pixel/rhi/types.hpp"
#include <iostream>
#include <span>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace pixel::resources {

TextureLoader::TextureLoader(rhi::Device* device)
    : device_(device) {
  if (!device_) {
    throw std::invalid_argument("TextureLoader: device cannot be null");
  }
}

rhi::TextureHandle TextureLoader::load(const std::string& path) {
  // Check cache
  auto it = cache_.find(path);
  if (it != cache_.end()) {
    return it->second;
  }

  // Load image using STB
  int width, height, channels;
  unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

  if (!data) {
    std::cerr << "TextureLoader: Failed to load texture: " << path << std::endl;
    return {0};
  }

  // Create texture
  auto texture_handle = create(width, height, data);
  stbi_image_free(data);

  if (texture_handle.id == 0) {
    std::cerr << "TextureLoader: Failed to create texture from: " << path << std::endl;
    return {0};
  }

  // Cache the result
  cache_[path] = texture_handle;

  std::cout << "TextureLoader: Loaded texture: " << path << " (" << width << "x" << height << ")" << std::endl;

  return texture_handle;
}

rhi::TextureHandle TextureLoader::create(int width, int height, const uint8_t* data) {
  // Create texture descriptor
  rhi::TextureDesc desc;
  desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  desc.format = rhi::Format::RGBA8;
  desc.mipLevels = 1;
  desc.renderTarget = false;

  auto texture_handle = device_->createTexture(desc);

  if (data) {
    // Upload texture data using immediate command list
    auto* cmd = device_->getImmediate();
    cmd->begin();

    // Convert pixel data to span
    size_t data_size = width * height * 4; // RGBA8
    std::span<const std::byte> data_span(
        reinterpret_cast<const std::byte*>(data), data_size);
    cmd->copyToTexture(texture_handle, 0, data_span);

    cmd->end();
  }

  return texture_handle;
}

rhi::TextureHandle TextureLoader::create_array(int width, int height, int layers) {
  // Create texture array descriptor
  rhi::TextureDesc desc;
  desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  desc.format = rhi::Format::RGBA8;
  desc.mipLevels = 1;
  desc.layers = static_cast<uint32_t>(layers);
  desc.renderTarget = false;

  auto texture_handle = device_->createTexture(desc);

  std::cout << "TextureLoader: Created texture array: " << width << "x" << height
            << " with " << layers << " layers" << std::endl;

  return texture_handle;
}

rhi::TextureHandle TextureLoader::load_array(const std::vector<std::string>& paths) {
  if (paths.empty()) {
    std::cerr << "TextureLoader: Empty texture array path list" << std::endl;
    return {0};
  }

  // Load first texture to determine dimensions
  int width, height, channels;
  unsigned char* first_data =
      stbi_load(paths[0].c_str(), &width, &height, &channels, 4);

  if (!first_data) {
    std::cerr << "TextureLoader: Failed to load first texture: " << paths[0] << std::endl;
    return {0};
  }
  stbi_image_free(first_data);

  // Create texture array
  auto array_handle = create_array(width, height, static_cast<int>(paths.size()));

  // Load and upload each texture
  for (size_t i = 0; i < paths.size(); ++i) {
    int w, h, c;
    unsigned char* data = stbi_load(paths[i].c_str(), &w, &h, &c, 4);

    if (!data) {
      std::cerr << "TextureLoader: Failed to load texture: " << paths[i] << std::endl;
      continue;
    }

    if (w != width || h != height) {
      std::cerr << "TextureLoader: Texture " << paths[i]
                << " has mismatched dimensions (" << w << "x" << h
                << "), expected (" << width << "x" << height
                << "), skipping" << std::endl;
      stbi_image_free(data);
      continue;
    }

    set_array_layer(array_handle, static_cast<int>(i), width, height, data);
    stbi_image_free(data);
  }

  std::cout << "TextureLoader: Loaded texture array with " << paths.size()
            << " textures" << std::endl;

  return array_handle;
}

void TextureLoader::set_array_layer(rhi::TextureHandle array_handle, int layer,
                                    int width, int height, const uint8_t* data) {
  if (!data) {
    std::cerr << "TextureLoader: Cannot upload null data to texture array layer" << std::endl;
    return;
  }

  auto* cmd = device_->getImmediate();
  cmd->begin();

  // Convert pixel data to span (RGBA8 format)
  size_t data_size = width * height * 4; // RGBA8
  std::span<const std::byte> data_span(
      reinterpret_cast<const std::byte*>(data), data_size);

  // Upload to the specific layer (mip level 0)
  cmd->copyToTextureLayer(array_handle, static_cast<uint32_t>(layer), 0, data_span);

  cmd->end();
}

void TextureLoader::clear_cache() {
  cache_.clear();
}

} // namespace pixel::resources
