#include "pixel/renderer3d/renderer.hpp"
#include "pixel/platform/shader_loader.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace pixel::renderer3d {

std::unique_ptr<Shader> Shader::create(rhi::Device *device,
                                       const std::string &vert_path,
                                       const std::string &frag_path) {
  auto shader = std::unique_ptr<Shader>(new Shader());

  // Load shader source files from disk
  auto [vert_src, frag_src] = platform::load_shader_pair(vert_path, frag_path);

  // Create vertex shader
  std::span<const uint8_t> vs_bytes(
      reinterpret_cast<const uint8_t *>(vert_src.data()), vert_src.size());
  shader->vs_ = device->createShader("vs", vs_bytes);

  // Create fragment shader
  std::span<const uint8_t> fs_bytes(
      reinterpret_cast<const uint8_t *>(frag_src.data()), frag_src.size());
  shader->fs_ = device->createShader("fs", fs_bytes);

  // Create pipeline
  rhi::PipelineDesc pipeline_desc;
  pipeline_desc.vs = shader->vs_;
  pipeline_desc.fs = shader->fs_;
  shader->pipeline_ = device->createPipeline(pipeline_desc);

  return shader;
}

} // namespace pixel::renderer3d
