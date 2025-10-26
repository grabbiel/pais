#include "pixel/renderer3d/renderer.hpp"
#include "pixel/platform/shader_loader.hpp"
#include "pixel/renderer3d/shader_reflection.hpp"
#include "pixel/renderer3d/shader_variant_system.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace pixel::renderer3d {

namespace {

bool is_vulkan_backend(const rhi::Device *device) {
  if (!device)
    return false;
  const char *backend = device->backend_name();
  if (!backend)
    return false;
  std::string_view backend_name(backend);
  return backend_name.find("Vulkan") != std::string_view::npos;
}

} // namespace

// ============================================================================
// ShaderVariantKey
// ============================================================================

void ShaderVariantKey::set_define(std::string name, std::string value) {
  defines_.insert_or_assign(std::move(name), std::move(value));
}

void ShaderVariantKey::clear_define(std::string_view name) {
  defines_.erase(std::string(name));
}

bool ShaderVariantKey::has_define(std::string_view name) const {
  return defines_.contains(name);
}

std::string ShaderVariantKey::cache_key() const {
  std::string key;
  for (const auto &[define_name, define_value] : defines_) {
    key.append(std::to_string(define_name.size()));
    key.push_back(':');
    key.append(define_name);
    key.push_back('=');
    key.append(std::to_string(define_value.size()));
    key.push_back(':');
    key.append(define_value);
    key.push_back(';');
  }
  return key;
}

ShaderVariantKey ShaderVariantKey::from_defines(
    std::initializer_list<std::pair<std::string, std::string>> defines) {
  ShaderVariantKey key;
  for (const auto &define : defines) {
    key.set_define(define.first, define.second);
  }
  return key;
}

// ============================================================================
// Shader
// ============================================================================

std::unique_ptr<Shader> Shader::create(rhi::Device *device,
                                       const std::string &vert_path,
                                       const std::string &frag_path,
                                       std::optional<std::string> metal_source_path) {
  auto shader = std::unique_ptr<Shader>(new Shader());
  shader->device_ = device;
  shader->vert_path_ = vert_path;
  shader->frag_path_ = frag_path;
  shader->is_vulkan_backend_ = is_vulkan_backend(device);

  if (!shader->is_vulkan_backend_) {
    if (metal_source_path) {
      try {
        shader->metal_source_code_ =
            platform::load_shader_file(*metal_source_path);
        std::cout << "  Loaded Metal shader source: " << *metal_source_path
                  << std::endl;
      } catch (const std::exception &e) {
        std::cerr << "  Warning: Failed to load Metal shader source '"
                  << *metal_source_path << "': " << e.what() << std::endl;
      }
    } else {
      std::cout << "  Metal backend detected without explicit source; using"
                << " default library" << std::endl;
    }
  }

  std::cout << "Shader::create() loading precompiled stages:" << std::endl;
  std::cout << "  Vertex source:   " << vert_path << std::endl;
  std::cout << "  Fragment source: " << frag_path << std::endl;

  const bool is_shadow_shader = vert_path.find("shadow") != std::string::npos ||
                                frag_path.find("shadow") != std::string::npos;
  const bool is_instanced_shader =
      vert_path.find("instanced") != std::string::npos ||
      frag_path.find("instanced") != std::string::npos;

  shader->is_shadow_shader_ = is_shadow_shader;
  shader->is_instanced_shader_ = is_instanced_shader;

  if (is_shadow_shader && is_instanced_shader) {
    shader->vs_stage_ = "vs_shadow_instanced";
    shader->fs_stage_ = "fs_shadow";
  } else if (is_shadow_shader) {
    shader->vs_stage_ = "vs_shadow";
    shader->fs_stage_ = "fs_shadow";
  } else if (is_instanced_shader) {
    shader->vs_stage_ = "vs_instanced";
    shader->fs_stage_ = "fs_instanced";
  } else {
    shader->vs_stage_ = "vs";
    shader->fs_stage_ = "fs";
  }

  std::cout << "  Vertex stage label: " << shader->vs_stage_ << std::endl;
  std::cout << "  Fragment stage label: " << shader->fs_stage_ << std::endl;

  if (shader->is_vulkan_backend_) {
    auto reflection = create_spirv_reflection_system();
    shader->variant_system_ =
        create_spirv_variant_system(std::move(reflection));
  } else {
    auto reflection = create_metal_reflection_system();
    shader->variant_system_ =
        create_metal_variant_system(std::move(reflection));
  }

  ShaderVariantKey default_variant;
  shader->variant_cache_.emplace(default_variant.cache_key(),
                                 shader->build_variant(default_variant));

  std::cout << "  Default shader variant loaded" << std::endl;

  return shader;
}

Shader::VariantData &
Shader::get_or_create_variant(const ShaderVariantKey &variant) const {
  std::string key = variant.cache_key();
  auto it = variant_cache_.find(key);
  if (it != variant_cache_.end()) {
    return it->second;
  }

  VariantData data = build_variant(variant);
  auto [inserted, success] =
      variant_cache_.emplace(std::move(key), std::move(data));
  (void)success;
  return inserted->second;
}

Shader::VariantData Shader::build_variant(const ShaderVariantKey &variant) const {
  if (!device_) {
    throw std::runtime_error("Shader created without a valid device");
  }

  std::string cache_key = variant.cache_key();
  std::cout << "Shader::build_variant()" << std::endl;
  std::cout << "  Variant cache key: '" << cache_key << "'" << std::endl;

  if (!variant_system_) {
    throw std::runtime_error("Shader variant system not initialized");
  }

  VariantData data = variant_system_->build_variant(make_build_context(), variant);
  std::cout << "  Reflection summary: uniforms="
            << data.reflection.uniforms().size() << std::endl;
  return data;
}

ShaderVariantBuildContext Shader::make_build_context() const {
  return ShaderVariantBuildContext(device_, vert_path_, frag_path_, vs_stage_, fs_stage_,
                                   is_vulkan_backend_, is_instanced_shader_,
                                   is_shadow_shader_, metal_source_code_);
}

rhi::PipelineHandle Shader::pipeline(Material::BlendMode mode) const {
  static const ShaderVariantKey kDefaultVariant{};
  return pipeline(kDefaultVariant, mode);
}

rhi::PipelineHandle Shader::pipeline(const ShaderVariantKey &variant,
                                     Material::BlendMode mode) const {
  VariantData &data = get_or_create_variant(variant);
  size_t index = static_cast<size_t>(mode);
  if (index >= Material::kBlendModeCount) {
    index = static_cast<size_t>(Material::BlendMode::Alpha);
  }

  const rhi::PipelineHandle handle = data.pipelines[index];
  if (handle.id != 0) {
    return handle;
  }

  return data.pipelines[static_cast<size_t>(Material::BlendMode::Alpha)];
}

std::pair<rhi::ShaderHandle, rhi::ShaderHandle>
Shader::shader_handles(const ShaderVariantKey &variant) const {
  VariantData &data = get_or_create_variant(variant);
  return {data.vs, data.fs};
}

const ShaderReflection &Shader::reflection() const {
  static const ShaderVariantKey kDefaultVariant{};
  return reflection(kDefaultVariant);
}

const ShaderReflection &
Shader::reflection(const ShaderVariantKey &variant) const {
  VariantData &data = get_or_create_variant(variant);
  return data.reflection;
}

} // namespace pixel::renderer3d
