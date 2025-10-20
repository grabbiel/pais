#include "pixel/renderer3d/renderer.hpp"
#include <iostream>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace pixel::renderer3d {

rhi::TextureHandle Renderer::load_texture(const std::string &path) {
  // Check cache
  auto it = texture_path_to_id_.find(path);
  if (it != texture_path_to_id_.end()) {
    return it->second;
  }

  // Load image
  int width, height, channels;
  unsigned char *data = stbi_load(path.c_str(), &width, &height, &channels, 4);

  if (!data) {
    std::cerr << "Failed to load texture: " << path << std::endl;
    return {0};
  }

  auto texture_id = create_texture(width, height, data);
  stbi_image_free(data);

  // Cache it
  texture_path_to_id_[path] = texture_id;

  std::cout << "Loaded texture: " << path << " (" << width << "x" << height
            << ")" << std::endl;

  return texture_id;
}

rhi::TextureHandle Renderer::create_texture(int width, int height,
                                            const uint8_t *data) {
  rhi::TextureDesc desc;
  desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  desc.format = rhi::Format::RGBA8;
  desc.mipLevels = 1;
  desc.renderTarget = false;

  auto texture_handle = device_->createTexture(desc);

  if (data) {
    // Upload texture data
    auto *cmd = device_->getImmediate();
    cmd->begin();

    // Convert pixel data to span
    size_t data_size = width * height * 4; // RGBA8
    std::span<const std::byte> data_span(
        reinterpret_cast<const std::byte *>(data), data_size);
    cmd->copyToTexture(texture_handle, 0, data_span);

    cmd->end();
  }

  uint32_t id = next_texture_id_++;
  textures_[id] = texture_handle;

  return texture_handle;
}

rhi::TextureHandle Renderer::create_texture_array(int width, int height,
                                                  int layers) {
  rhi::TextureDesc desc;
  desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  desc.format = rhi::Format::RGBA8;
  desc.mipLevels = 1;
  desc.renderTarget = false;
  // TODO: Add array layer count to TextureDesc

  auto texture_handle = device_->createTexture(desc);

  uint32_t id = next_texture_id_++;
  textures_[id] = texture_handle;

  std::cout << "Created texture array: " << width << "x" << height << " with "
            << layers << " layers" << std::endl;

  return texture_handle;
}

rhi::TextureHandle
Renderer::load_texture_array(const std::vector<std::string> &paths) {
  if (paths.empty()) {
    std::cerr << "Empty texture array path list" << std::endl;
    return {0};
  }

  // Load first texture to get dimensions
  int width, height, channels;
  unsigned char *first_data =
      stbi_load(paths[0].c_str(), &width, &height, &channels, 4);

  if (!first_data) {
    std::cerr << "Failed to load first texture: " << paths[0] << std::endl;
    return {0};
  }
  stbi_image_free(first_data);

  // Create texture array
  auto array_id = create_texture_array(width, height, paths.size());

  // Load and upload each texture
  for (size_t i = 0; i < paths.size(); ++i) {
    int w, h, c;
    unsigned char *data = stbi_load(paths[i].c_str(), &w, &h, &c, 4);

    if (!data) {
      std::cerr << "Failed to load texture: " << paths[i] << std::endl;
      continue;
    }

    if (w != width || h != height) {
      std::cerr << "Texture " << paths[i] << " has mismatched dimensions (" << w
                << "x" << h << "), expected (" << width << "x" << height
                << "), skipping" << std::endl;
      stbi_image_free(data);
      continue;
    }

    set_texture_array_layer(array_id, i, data);
    stbi_image_free(data);
  }

  std::cout << "Loaded texture array with " << paths.size() << " textures"
            << std::endl;

  return array_id;
}

void Renderer::set_texture_array_layer(rhi::TextureHandle array_id, int layer,
                                       const uint8_t *data) {
  // TODO: Implement texture array layer upload via RHI
  // This requires extending the CmdList interface with texture copy commands
  auto *cmd = device_->getImmediate();
  cmd->begin();
  // Placeholder for texture array layer upload
  cmd->end();
}

} // namespace pixel::renderer3d
