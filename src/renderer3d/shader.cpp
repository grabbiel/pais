#include "pixel/renderer3d/renderer.hpp"
#include "pixel/platform/shader_loader.hpp"
#include "pixel/renderer3d/shader_reflection.hpp"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pixel::renderer3d {

namespace {

ShaderReflection make_default_metal_reflection(bool instanced) {
  ShaderReflection reflection;

  ShaderBlock pixel_block;
  pixel_block.type = ShaderBlockType::Uniform;
  pixel_block.block_name = "PixelUniforms";
  pixel_block.instance_name = "PixelUniforms";
  pixel_block.binding = 1;
  pixel_block.add_stage(ShaderStage::Vertex);
  pixel_block.add_stage(ShaderStage::Fragment);

  auto add_uniform = [&](std::string name, ShaderUniformType type) {
    ShaderBlockMember member;
    member.name = name;
    member.type = type;
    member.array_size = 1;
    pixel_block.members.push_back(member);

    ShaderUniform uniform;
    uniform.name = std::move(name);
    uniform.type = type;
    uniform.array_size = 1;
    uniform.binding = 1;
    uniform.add_stage(ShaderStage::Vertex);
    uniform.add_stage(ShaderStage::Fragment);
    reflection.add_uniform(std::move(uniform));
  };

  add_uniform("model", ShaderUniformType::Mat4);
  add_uniform("view", ShaderUniformType::Mat4);
  add_uniform("projection", ShaderUniformType::Mat4);
  add_uniform("normalMatrix", ShaderUniformType::Mat4);
  add_uniform("lightViewProj", ShaderUniformType::Mat4);
  add_uniform("materialColor", ShaderUniformType::Vec4);
  add_uniform("lightPos", ShaderUniformType::Vec3);
  add_uniform("alphaCutoff", ShaderUniformType::Float);
  add_uniform("viewPos", ShaderUniformType::Vec3);
  add_uniform("baseAlpha", ShaderUniformType::Float);
  add_uniform("lightColor", ShaderUniformType::Vec3);
  add_uniform("shadowBias", ShaderUniformType::Float);
  add_uniform("uTime", ShaderUniformType::Float);
  add_uniform("ditherScale", ShaderUniformType::Float);
  add_uniform("crossfadeDuration", ShaderUniformType::Float);
  add_uniform("_padMisc", ShaderUniformType::Float);
  add_uniform("useTexture", ShaderUniformType::Int);
  add_uniform("useTextureArray", ShaderUniformType::Int);
  add_uniform("uDitherEnabled", ShaderUniformType::Int);
  add_uniform("shadowsEnabled", ShaderUniformType::Int);

  reflection.add_block(std::move(pixel_block));

  auto add_sampler = [&](std::string name, ShaderUniformType type,
                         uint32_t binding) {
    ShaderUniform sampler;
    sampler.name = std::move(name);
    sampler.type = type;
    sampler.array_size = 1;
    sampler.binding = binding;
    sampler.add_stage(ShaderStage::Fragment);
    reflection.add_uniform(std::move(sampler));
  };

  add_sampler("uTexture", ShaderUniformType::Sampler2D, 0);
  if (instanced) {
    add_sampler("uTextureArray", ShaderUniformType::Sampler2DArray, 0);
  }
  add_sampler("shadowMap", ShaderUniformType::Sampler2DShadow, 2);

  return reflection;
}

bool is_vulkan_backend(const rhi::Device *device) {
  if (!device)
    return false;
  const char *backend = device->backend_name();
  if (!backend)
    return false;
  std::string_view backend_name(backend);
  return backend_name.find("Vulkan") != std::string_view::npos;
}

std::string sanitize_token(std::string_view token) {
  std::string sanitized;
  sanitized.reserve(token.size());
  for (char c : token) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) || c == '_') {
      sanitized.push_back(c);
    } else {
      sanitized.push_back('_');
    }
  }
  if (sanitized.empty())
    sanitized = "0";
  return sanitized;
}

std::string make_variant_suffix(const ShaderVariantKey &variant) {
  if (variant.empty())
    return {};

  std::string suffix;
  bool first = true;
  for (const auto &[name, value] : variant.defines()) {
    if (!first)
      suffix.append("__");
    first = false;
    suffix.append(sanitize_token(name));
    suffix.push_back('_');
    suffix.append(sanitize_token(value));
  }
  return suffix;
}

std::string make_spirv_path(std::string_view source_path,
                            const ShaderVariantKey &variant) {
  namespace fs = std::filesystem;
  fs::path relative(source_path);
  fs::path spirv_dir = relative.parent_path() / "spirv";
  std::string base = relative.filename().string();
  std::string suffix = make_variant_suffix(variant);
  if (!suffix.empty()) {
    base += "__";
    base += suffix;
  }
  base += ".spv";
  return (spirv_dir / base).generic_string();
}

std::span<const uint32_t> bytes_to_words(const std::vector<uint8_t> &bytes) {
  if (bytes.empty() || (bytes.size() % sizeof(uint32_t)) != 0) {
    throw std::runtime_error("SPIR-V bytecode must be non-empty and 4-byte aligned");
  }
  return std::span<const uint32_t>(
      reinterpret_cast<const uint32_t *>(bytes.data()),
      bytes.size() / sizeof(uint32_t));
}

std::string apply_variant_defines(const std::string &source,
                                  const ShaderVariantKey &variant) {
  if (variant.empty()) {
    return source;
  }

  std::string with_defines;
  with_defines.reserve(source.size() + 128);
  with_defines.append("// Auto-generated variant defines\n");
  for (const auto &[name, value] : variant.defines()) {
    with_defines.append("#define ");
    with_defines.append(name);
    if (!value.empty()) {
      with_defines.push_back(' ');
      with_defines.append(value);
    }
    with_defines.push_back('\n');
  }
  with_defines.push_back('\n');
  with_defines.append(source);
  return with_defines;
}

std::span<const uint8_t> string_to_span(const std::string &str) {
  return std::span<const uint8_t>(
      reinterpret_cast<const uint8_t *>(str.data()), str.size());
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
  VariantData data{};

  if (is_vulkan_backend_) {
    std::string vert_spirv_path = make_spirv_path(vert_path_, variant);
    std::string frag_spirv_path = make_spirv_path(frag_path_, variant);

    std::cout << "  Vertex SPIR-V:   " << vert_spirv_path << std::endl;
    std::cout << "  Fragment SPIR-V: " << frag_spirv_path << std::endl;

    std::vector<uint8_t> vert_bytes =
        platform::load_shader_bytecode(vert_spirv_path);
    std::vector<uint8_t> frag_bytes =
        platform::load_shader_bytecode(frag_spirv_path);

    auto vert_span =
        std::span<const uint8_t>(vert_bytes.data(), vert_bytes.size());
    auto frag_span =
        std::span<const uint8_t>(frag_bytes.data(), frag_bytes.size());

    data.vs = device_->createShaderFromBytecode(vs_stage_, vert_span);
    if (data.vs.id == 0) {
      throw std::runtime_error(
          "Failed to create vertex shader from SPIR-V bytecode");
    }

    data.fs = device_->createShaderFromBytecode(fs_stage_, frag_span);
    if (data.fs.id == 0) {
      throw std::runtime_error(
          "Failed to create fragment shader from SPIR-V bytecode");
    }

    auto build_desc = [&](const rhi::BlendState &blend) {
      rhi::PipelineDesc desc{};
      desc.vs = data.vs;
      desc.fs = data.fs;
      desc.colorAttachmentCount = 1;
      desc.colorAttachments[0].format = rhi::Format::BGRA8;
      desc.colorAttachments[0].blend = blend;
      return desc;
    };

    data.pipelines[static_cast<size_t>(Material::BlendMode::Alpha)] =
        device_->createPipeline(build_desc(rhi::make_alpha_blend_state()));
    data.pipelines[static_cast<size_t>(Material::BlendMode::Additive)] =
        device_->createPipeline(build_desc(rhi::make_additive_blend_state()));
    data.pipelines[static_cast<size_t>(Material::BlendMode::Multiply)] =
        device_->createPipeline(build_desc(rhi::make_multiply_blend_state()));
    data.pipelines[static_cast<size_t>(Material::BlendMode::Opaque)] =
        device_->createPipeline(build_desc(rhi::make_disabled_blend_state()));

    ShaderReflection vert_reflection =
        reflect_spirv(bytes_to_words(vert_bytes), ShaderStage::Vertex);
    ShaderReflection frag_reflection =
        reflect_spirv(bytes_to_words(frag_bytes), ShaderStage::Fragment);
    data.reflection = std::move(vert_reflection);
    data.reflection.merge(frag_reflection);
    std::cout << "  Reflection summary: uniforms="
              << data.reflection.uniforms().size() << std::endl;
    return data;
  }

  std::cout << "  Using Metal shader compilation path" << std::endl;

  std::span<const uint8_t> source_span{};
  std::string variant_source_storage;
  if (!metal_source_code_.empty()) {
    if (variant.empty()) {
      source_span = string_to_span(metal_source_code_);
    } else {
      variant_source_storage = apply_variant_defines(metal_source_code_, variant);
      source_span = string_to_span(variant_source_storage);
    }
  }

  data.vs = device_->createShader(vs_stage_, source_span);
  if (data.vs.id == 0) {
    throw std::runtime_error("Failed to create vertex shader for Metal backend");
  }

  data.fs = device_->createShader(fs_stage_, source_span);
  if (data.fs.id == 0) {
    throw std::runtime_error(
        "Failed to create fragment shader for Metal backend");
  }

  auto build_desc = [&](const rhi::BlendState &blend) {
    rhi::PipelineDesc desc{};
    desc.vs = data.vs;
    desc.fs = data.fs;
    desc.colorAttachmentCount = 1;
    desc.colorAttachments[0].format = rhi::Format::BGRA8;
    desc.colorAttachments[0].blend = blend;
    return desc;
  };

  data.pipelines[static_cast<size_t>(Material::BlendMode::Alpha)] =
      device_->createPipeline(build_desc(rhi::make_alpha_blend_state()));
  data.pipelines[static_cast<size_t>(Material::BlendMode::Additive)] =
      device_->createPipeline(build_desc(rhi::make_additive_blend_state()));
  data.pipelines[static_cast<size_t>(Material::BlendMode::Multiply)] =
      device_->createPipeline(build_desc(rhi::make_multiply_blend_state()));
  data.pipelines[static_cast<size_t>(Material::BlendMode::Opaque)] =
      device_->createPipeline(build_desc(rhi::make_disabled_blend_state()));

  try {
    std::string vert_spirv_path = make_spirv_path(vert_path_, variant);
    std::string frag_spirv_path = make_spirv_path(frag_path_, variant);
    std::cout << "  Attempting to load SPIR-V reflection data from:"
              << "\n    VS: " << vert_spirv_path << "\n    FS: " << frag_spirv_path
              << std::endl;

    std::vector<uint8_t> vert_bytes =
        platform::load_shader_bytecode(vert_spirv_path);
    std::vector<uint8_t> frag_bytes =
        platform::load_shader_bytecode(frag_spirv_path);

    ShaderReflection vert_reflection =
        reflect_spirv(bytes_to_words(vert_bytes), ShaderStage::Vertex);
    ShaderReflection frag_reflection =
        reflect_spirv(bytes_to_words(frag_bytes), ShaderStage::Fragment);
    data.reflection = std::move(vert_reflection);
    data.reflection.merge(frag_reflection);
  } catch (const std::exception &e) {
    std::cerr << "  Warning: Metal shader reflection unavailable: "
              << e.what() << std::endl;
  }

  auto begins_with = [](const std::string &stage, std::string_view prefix) {
    return stage.rfind(prefix.data(), 0) == 0;
  };

  const bool graphics_stage =
      begins_with(vs_stage_, "vs") || begins_with(fs_stage_, "fs");
  if (graphics_stage) {
    const bool instanced = fs_stage_ == "fs_instanced" ||
                           fs_stage_ == "fs_shadow_instanced" ||
                           vs_stage_ == "vs_instanced" ||
                           vs_stage_ == "vs_shadow_instanced";
    ShaderReflection fallback = make_default_metal_reflection(instanced);
    data.reflection.merge(fallback);
  }

  std::cout << "  Reflection summary: uniforms="
            << data.reflection.uniforms().size() << std::endl;

  return data;
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
