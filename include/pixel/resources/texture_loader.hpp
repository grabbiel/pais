#pragma once

#include "pixel/rhi/handles.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace pixel::rhi {
class Device;
}

namespace pixel::resources {

/**
 * @brief TextureLoader handles loading and caching of texture resources
 *
 * Separates resource management from rendering concerns, providing:
 * - Texture loading from files (via STB Image)
 * - Texture creation from raw data
 * - Automatic caching to avoid duplicate loads
 * - Support for texture arrays
 */
class TextureLoader {
public:
  /**
   * @brief Construct a TextureLoader with a RHI device
   * @param device The RHI device to use for texture creation
   */
  explicit TextureLoader(rhi::Device* device);

  /**
   * @brief Load a texture from a file path
   *
   * Automatically caches textures - subsequent calls with the same path
   * will return the cached texture handle.
   *
   * @param path File path to the texture image
   * @return Handle to the loaded texture, or invalid handle on failure
   */
  rhi::TextureHandle load(const std::string& path);

  /**
   * @brief Create a texture from raw pixel data
   *
   * @param width Width of the texture in pixels
   * @param height Height of the texture in pixels
   * @param data Pointer to RGBA8 pixel data (can be null for empty texture)
   * @return Handle to the created texture
   */
  rhi::TextureHandle create(int width, int height, const uint8_t* data);

  /**
   * @brief Create an empty texture array
   *
   * @param width Width of each texture layer
   * @param height Height of each texture layer
   * @param layers Number of layers in the array
   * @return Handle to the created texture array
   */
  rhi::TextureHandle create_array(int width, int height, int layers);

  /**
   * @brief Load a texture array from multiple image files
   *
   * All images must have the same dimensions. The first image's dimensions
   * determine the expected size.
   *
   * @param paths Vector of file paths to load
   * @return Handle to the loaded texture array, or invalid handle on failure
   */
  rhi::TextureHandle load_array(const std::vector<std::string>& paths);

  /**
   * @brief Set data for a specific layer in a texture array
   *
   * @param array_handle Handle to the texture array
   * @param layer Layer index to update
   * @param width Width of the data
   * @param height Height of the data
   * @param data Pointer to RGBA8 pixel data
   */
  void set_array_layer(rhi::TextureHandle array_handle, int layer,
                       int width, int height, const uint8_t* data);

  /**
   * @brief Clear the texture cache
   *
   * Note: This does not destroy the textures, only clears the path->handle mapping
   */
  void clear_cache();

  /**
   * @brief Get the RHI device used by this loader
   * @return Pointer to the RHI device
   */
  rhi::Device* device() const { return device_; }

private:
  rhi::Device* device_;
  std::unordered_map<std::string, rhi::TextureHandle> cache_;
};

} // namespace pixel::resources
