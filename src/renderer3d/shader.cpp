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

  auto build_desc = [&](const rhi::BlendState &blend) {
    rhi::PipelineDesc desc{};
    desc.vs = shader->vs_;
    desc.fs = shader->fs_;
    desc.colorAttachmentCount = 1;
    desc.colorAttachments[0].format = rhi::Format::BGRA8;
    desc.colorAttachments[0].blend = blend;
    return desc;
  };

  shader->pipelines_[static_cast<size_t>(Material::BlendMode::Alpha)] =
      device->createPipeline(build_desc(rhi::make_alpha_blend_state()));
  shader->pipelines_[static_cast<size_t>(Material::BlendMode::Additive)] =
      device->createPipeline(build_desc(rhi::make_additive_blend_state()));
  shader->pipelines_[static_cast<size_t>(Material::BlendMode::Multiply)] =
      device->createPipeline(build_desc(rhi::make_multiply_blend_state()));
  shader->pipelines_[static_cast<size_t>(Material::BlendMode::Opaque)] =
      device->createPipeline(build_desc(rhi::make_disabled_blend_state()));

  return shader;
}

rhi::PipelineHandle Shader::pipeline(Material::BlendMode mode) const {
  size_t index = static_cast<size_t>(mode);
  if (index >= Material::kBlendModeCount) {
    index = static_cast<size_t>(Material::BlendMode::Alpha);
  }

  const rhi::PipelineHandle handle = pipelines_[index];
  if (handle.id != 0) {
    return handle;
  }

  return pipelines_[static_cast<size_t>(Material::BlendMode::Alpha)];
}

} // namespace pixel::renderer3d
