#pragma once

#include "pixel/renderer3d/shader_reflection.hpp"
#include "pixel/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <span>
#include <string>

namespace pixel::renderer3d {

class ShaderVariantKey;

struct ShaderVariantData {
  static constexpr size_t kPipelineCount = 4;
  std::array<rhi::PipelineHandle, kPipelineCount> pipelines{};
  rhi::ShaderHandle vs{0};
  rhi::ShaderHandle fs{0};
  ShaderReflection reflection{};
};

struct ShaderVariantBuildContext {
  ShaderVariantBuildContext(rhi::Device *device, const std::string &vert_path,
                            const std::string &frag_path, const std::string &vs_stage,
                            const std::string &fs_stage, bool is_vulkan_backend,
                            bool is_instanced_shader, bool is_shadow_shader,
                            const std::string &metal_source_code)
      : device(device), vert_path(vert_path), frag_path(frag_path),
        vs_stage(vs_stage), fs_stage(fs_stage),
        is_vulkan_backend(is_vulkan_backend), is_instanced_shader(is_instanced_shader),
        is_shadow_shader(is_shadow_shader), metal_source_code(metal_source_code) {}

  rhi::Device *device{nullptr};
  const std::string &vert_path;
  const std::string &frag_path;
  const std::string &vs_stage;
  const std::string &fs_stage;
  bool is_vulkan_backend{false};
  bool is_instanced_shader{false};
  bool is_shadow_shader{false};
  const std::string &metal_source_code;
};

class ShaderReflectionSystem {
public:
  virtual ~ShaderReflectionSystem() = default;

  virtual ShaderReflection reflect_stage(std::span<const uint8_t> bytecode,
                                         ShaderStage stage) const = 0;
  virtual void finalize_reflection(const ShaderVariantBuildContext &context,
                                   const ShaderVariantKey &variant,
                                   ShaderReflection &reflection) const = 0;
};

class ShaderVariantSystem {
public:
  virtual ~ShaderVariantSystem() = default;

  virtual ShaderVariantData build_variant(const ShaderVariantBuildContext &context,
                                          const ShaderVariantKey &variant) const = 0;
};

std::unique_ptr<ShaderReflectionSystem> create_spirv_reflection_system();
std::unique_ptr<ShaderReflectionSystem> create_metal_reflection_system();

std::unique_ptr<ShaderVariantSystem>
create_spirv_variant_system(std::unique_ptr<ShaderReflectionSystem> reflection);
std::unique_ptr<ShaderVariantSystem>
create_metal_variant_system(std::unique_ptr<ShaderReflectionSystem> reflection);

} // namespace pixel::renderer3d

