#include "pixel/renderer3d/shader_variant_system.hpp"

#include "pixel/platform/shader_loader.hpp"
#include "pixel/renderer3d/renderer.hpp"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pixel::renderer3d {

namespace {

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

std::string make_reflection_cache_path(std::string_view source_path,
                                       const ShaderVariantKey &variant) {
  namespace fs = std::filesystem;
  fs::path cache_path = make_spirv_path(source_path, variant);
  cache_path.replace_extension(".reflection.bin");
  return cache_path.generic_string();
}

void ensure_directory_for_file(const std::string &path) {
  namespace fs = std::filesystem;
  fs::path file_path(path);
  fs::path parent = file_path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    fs::create_directories(parent, ec);
  }
}

void write_string(std::ofstream &out, const std::string &value) {
  const uint32_t size = static_cast<uint32_t>(value.size());
  out.write(reinterpret_cast<const char *>(&size), sizeof(size));
  if (size > 0) {
    out.write(value.data(), static_cast<std::streamsize>(size));
  }
}

std::string read_string(std::ifstream &in) {
  uint32_t size = 0;
  in.read(reinterpret_cast<char *>(&size), sizeof(size));
  if (!in)
    throw std::runtime_error("Failed to read string length from reflection cache");
  std::string value(size, '\0');
  if (size > 0) {
    in.read(value.data(), static_cast<std::streamsize>(size));
    if (!in)
      throw std::runtime_error("Failed to read string payload from reflection cache");
  }
  return value;
}

constexpr std::array<char, 8> kReflectionCacheMagic{'P', 'X', 'R', 'E', 'F', 'L', 'C', '1'};
constexpr uint32_t kReflectionCacheVersion = 1;

void write_reflection_cache(const ShaderReflection &reflection,
                            const std::string &path) {
  try {
    ensure_directory_for_file(path);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
      throw std::runtime_error("unable to open file for writing");

    out.write(kReflectionCacheMagic.data(),
              static_cast<std::streamsize>(kReflectionCacheMagic.size()));
    out.write(reinterpret_cast<const char *>(&kReflectionCacheVersion),
              sizeof(kReflectionCacheVersion));

    const auto uniforms = reflection.uniforms();
    const uint32_t uniform_count = static_cast<uint32_t>(uniforms.size());
    out.write(reinterpret_cast<const char *>(&uniform_count), sizeof(uniform_count));
    for (const auto &[name, uniform] : uniforms) {
      write_string(out, name);
      const uint8_t type = static_cast<uint8_t>(uniform.type);
      out.write(reinterpret_cast<const char *>(&type), sizeof(type));
      out.write(reinterpret_cast<const char *>(&uniform.array_size),
                sizeof(uniform.array_size));
      out.write(reinterpret_cast<const char *>(&uniform.stage_mask),
                sizeof(uniform.stage_mask));
      const int32_t binding = uniform.binding ? static_cast<int32_t>(*uniform.binding)
                                              : static_cast<int32_t>(-1);
      out.write(reinterpret_cast<const char *>(&binding), sizeof(binding));
    }

    const auto blocks = reflection.blocks();
    const uint32_t block_count = static_cast<uint32_t>(blocks.size());
    out.write(reinterpret_cast<const char *>(&block_count), sizeof(block_count));
    for (const ShaderBlock &block : blocks) {
      const uint8_t type = static_cast<uint8_t>(block.type);
      out.write(reinterpret_cast<const char *>(&type), sizeof(type));
      write_string(out, block.block_name);
      write_string(out, block.instance_name);
      out.write(reinterpret_cast<const char *>(&block.stage_mask),
                sizeof(block.stage_mask));
      const int32_t binding = block.binding ? static_cast<int32_t>(*block.binding)
                                            : static_cast<int32_t>(-1);
      out.write(reinterpret_cast<const char *>(&binding), sizeof(binding));

      const uint32_t member_count = static_cast<uint32_t>(block.members.size());
      out.write(reinterpret_cast<const char *>(&member_count), sizeof(member_count));
      for (const ShaderBlockMember &member : block.members) {
        write_string(out, member.name);
        const uint8_t member_type = static_cast<uint8_t>(member.type);
        out.write(reinterpret_cast<const char *>(&member_type), sizeof(member_type));
        out.write(reinterpret_cast<const char *>(&member.array_size),
                  sizeof(member.array_size));
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "  Warning: Failed to persist shader reflection cache '" << path
              << "': " << e.what() << std::endl;
  }
}

std::optional<ShaderReflection> load_reflection_cache(const std::string &path) {
  namespace fs = std::filesystem;
  if (!fs::exists(path))
    return std::nullopt;

  try {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      throw std::runtime_error("unable to open file for reading");

    std::array<char, kReflectionCacheMagic.size()> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in)
      throw std::runtime_error("unable to read cache header");
    if (magic != kReflectionCacheMagic)
      throw std::runtime_error("unexpected cache header magic");

    uint32_t version = 0;
    in.read(reinterpret_cast<char *>(&version), sizeof(version));
    if (!in)
      throw std::runtime_error("unable to read cache version");
    if (version != kReflectionCacheVersion)
      throw std::runtime_error("unsupported reflection cache version");

    ShaderReflection reflection;

    uint32_t uniform_count = 0;
    in.read(reinterpret_cast<char *>(&uniform_count), sizeof(uniform_count));
    if (!in)
      throw std::runtime_error("unable to read uniform count");
    for (uint32_t i = 0; i < uniform_count; ++i) {
      ShaderUniform uniform;
      uniform.name = read_string(in);
      uint8_t type = 0;
      in.read(reinterpret_cast<char *>(&type), sizeof(type));
      uniform.type = static_cast<ShaderUniformType>(type);
      in.read(reinterpret_cast<char *>(&uniform.array_size), sizeof(uniform.array_size));
      in.read(reinterpret_cast<char *>(&uniform.stage_mask), sizeof(uniform.stage_mask));
      int32_t binding = -1;
      in.read(reinterpret_cast<char *>(&binding), sizeof(binding));
      if (binding >= 0)
        uniform.binding = static_cast<uint32_t>(binding);
      reflection.add_uniform(std::move(uniform));
    }

    uint32_t block_count = 0;
    in.read(reinterpret_cast<char *>(&block_count), sizeof(block_count));
    if (!in)
      throw std::runtime_error("unable to read block count");
    for (uint32_t i = 0; i < block_count; ++i) {
      ShaderBlock block;
      uint8_t type = 0;
      in.read(reinterpret_cast<char *>(&type), sizeof(type));
      block.type = static_cast<ShaderBlockType>(type);
      block.block_name = read_string(in);
      block.instance_name = read_string(in);
      in.read(reinterpret_cast<char *>(&block.stage_mask), sizeof(block.stage_mask));
      int32_t binding = -1;
      in.read(reinterpret_cast<char *>(&binding), sizeof(binding));
      if (binding >= 0)
        block.binding = static_cast<uint32_t>(binding);

      uint32_t member_count = 0;
      in.read(reinterpret_cast<char *>(&member_count), sizeof(member_count));
      block.members.reserve(member_count);
      for (uint32_t m = 0; m < member_count; ++m) {
        ShaderBlockMember member;
        member.name = read_string(in);
        uint8_t member_type = 0;
        in.read(reinterpret_cast<char *>(&member_type), sizeof(member_type));
        member.type = static_cast<ShaderUniformType>(member_type);
        in.read(reinterpret_cast<char *>(&member.array_size), sizeof(member.array_size));
        block.members.push_back(std::move(member));
      }

      reflection.add_block(std::move(block));
    }

    return reflection;
  } catch (const std::exception &e) {
    std::cerr << "  Warning: Failed to load shader reflection cache '" << path
              << "': " << e.what() << std::endl;
    return std::nullopt;
  }
}

std::span<const uint32_t> span_to_words(std::span<const uint8_t> bytes) {
  if (bytes.empty() || (bytes.size() % sizeof(uint32_t)) != 0) {
    throw std::runtime_error("SPIR-V bytecode must be non-empty and 4-byte aligned");
  }
  return std::span<const uint32_t>(reinterpret_cast<const uint32_t *>(bytes.data()),
                                   bytes.size() / sizeof(uint32_t));
}

std::span<const uint8_t> string_to_span(const std::string &str) {
  return std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(str.data()),
                                  str.size());
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

class SpirvShaderReflectionSystem : public ShaderReflectionSystem {
public:
  ShaderReflection reflect_stage(std::span<const uint8_t> bytecode,
                                 ShaderStage stage) const override {
    if (bytecode.empty())
      return ShaderReflection{};
    return reflect_spirv(span_to_words(bytecode), stage);
  }

  void finalize_reflection(const ShaderVariantBuildContext &, const ShaderVariantKey &,
                           ShaderReflection &) const override {}
};

class MetalShaderReflectionSystem : public ShaderReflectionSystem {
public:
  ShaderReflection reflect_stage(std::span<const uint8_t> bytecode,
                                 ShaderStage stage) const override {
    return spirv_reflection_.reflect_stage(bytecode, stage);
  }

  void finalize_reflection(const ShaderVariantBuildContext &context,
                           const ShaderVariantKey &,
                           ShaderReflection &reflection) const override {
    (void)context;
    if (reflection.uniforms().empty() && reflection.blocks().empty()) {
      throw std::runtime_error(
          "Metal shader reflection was empty; ensure reflection metadata is available");
    }
  }

private:
  SpirvShaderReflectionSystem spirv_reflection_{};
};

class SpirvShaderVariantSystem : public ShaderVariantSystem {
public:
  explicit SpirvShaderVariantSystem(std::unique_ptr<ShaderReflectionSystem> reflection)
      : reflection_(std::move(reflection)) {}

  ShaderVariantData build_variant(const ShaderVariantBuildContext &context,
                                  const ShaderVariantKey &variant) const override {
    if (!context.device)
      throw std::runtime_error("Cannot build shader variant without device");

    ShaderVariantData data{};

    std::string vert_spirv_path = make_spirv_path(context.vert_path, variant);
    std::string frag_spirv_path = make_spirv_path(context.frag_path, variant);

    std::cout << "  Vertex SPIR-V:   " << vert_spirv_path << std::endl;
    std::cout << "  Fragment SPIR-V: " << frag_spirv_path << std::endl;

    std::vector<uint8_t> vert_bytes =
        platform::load_shader_bytecode(vert_spirv_path);
    std::vector<uint8_t> frag_bytes =
        platform::load_shader_bytecode(frag_spirv_path);

    auto vert_span = std::span<const uint8_t>(vert_bytes.data(), vert_bytes.size());
    auto frag_span = std::span<const uint8_t>(frag_bytes.data(), frag_bytes.size());

    data.vs = context.device->createShaderFromBytecode(context.vs_stage, vert_span);
    if (data.vs.id == 0) {
      throw std::runtime_error("Failed to create vertex shader from SPIR-V bytecode");
    }

    data.fs = context.device->createShaderFromBytecode(context.fs_stage, frag_span);
    if (data.fs.id == 0) {
      throw std::runtime_error("Failed to create fragment shader from SPIR-V bytecode");
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
        context.device->createPipeline(build_desc(rhi::make_alpha_blend_state()));
    data.pipelines[static_cast<size_t>(Material::BlendMode::Additive)] =
        context.device->createPipeline(build_desc(rhi::make_additive_blend_state()));
    data.pipelines[static_cast<size_t>(Material::BlendMode::Multiply)] =
        context.device->createPipeline(build_desc(rhi::make_multiply_blend_state()));
    data.pipelines[static_cast<size_t>(Material::BlendMode::Opaque)] =
        context.device->createPipeline(build_desc(rhi::make_disabled_blend_state()));

    ShaderReflection vert_reflection =
        reflection_->reflect_stage(vert_span, ShaderStage::Vertex);
    ShaderReflection frag_reflection =
        reflection_->reflect_stage(frag_span, ShaderStage::Fragment);
    data.reflection = std::move(vert_reflection);
    data.reflection.merge(frag_reflection);
    reflection_->finalize_reflection(context, variant, data.reflection);

    write_reflection_cache(data.reflection,
                           make_reflection_cache_path(context.vert_path, variant));

    return data;
  }

private:
  std::unique_ptr<ShaderReflectionSystem> reflection_;
};

class MetalShaderVariantSystem : public ShaderVariantSystem {
public:
  explicit MetalShaderVariantSystem(std::unique_ptr<ShaderReflectionSystem> reflection)
      : reflection_(std::move(reflection)) {}

  ShaderVariantData build_variant(const ShaderVariantBuildContext &context,
                                  const ShaderVariantKey &variant) const override {
    if (!context.device)
      throw std::runtime_error("Cannot build shader variant without device");

    ShaderVariantData data{};

    std::cout << "  Using Metal shader compilation path" << std::endl;

    std::span<const uint8_t> source_span{};
    std::string variant_source_storage;
    if (!context.metal_source_code.empty()) {
      if (variant.empty()) {
        source_span = string_to_span(context.metal_source_code);
      } else {
        variant_source_storage = apply_variant_defines(context.metal_source_code, variant);
        source_span = string_to_span(variant_source_storage);
      }
    }

    data.vs = context.device->createShader(context.vs_stage, source_span);
    if (data.vs.id == 0) {
      throw std::runtime_error("Failed to create vertex shader for Metal backend");
    }

    data.fs = context.device->createShader(context.fs_stage, source_span);
    if (data.fs.id == 0) {
      throw std::runtime_error("Failed to create fragment shader for Metal backend");
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
        context.device->createPipeline(build_desc(rhi::make_alpha_blend_state()));
    data.pipelines[static_cast<size_t>(Material::BlendMode::Additive)] =
        context.device->createPipeline(build_desc(rhi::make_additive_blend_state()));
    data.pipelines[static_cast<size_t>(Material::BlendMode::Multiply)] =
        context.device->createPipeline(build_desc(rhi::make_multiply_blend_state()));
    data.pipelines[static_cast<size_t>(Material::BlendMode::Opaque)] =
        context.device->createPipeline(build_desc(rhi::make_disabled_blend_state()));

    bool reflection_from_spirv = false;
    bool reflection_loaded = false;
    const std::string cache_path =
        make_reflection_cache_path(context.vert_path, variant);

    try {
      std::string vert_spirv_path = make_spirv_path(context.vert_path, variant);
      std::string frag_spirv_path = make_spirv_path(context.frag_path, variant);
      std::cout << "  Attempting to load SPIR-V reflection data from:"
                << "\n    VS: " << vert_spirv_path << "\n    FS: " << frag_spirv_path
                << std::endl;

      std::vector<uint8_t> vert_bytes =
          platform::load_shader_bytecode(vert_spirv_path);
      std::vector<uint8_t> frag_bytes =
          platform::load_shader_bytecode(frag_spirv_path);

      auto vert_span = std::span<const uint8_t>(vert_bytes.data(), vert_bytes.size());
      auto frag_span = std::span<const uint8_t>(frag_bytes.data(), frag_bytes.size());

      ShaderReflection vert_reflection =
          reflection_->reflect_stage(vert_span, ShaderStage::Vertex);
      ShaderReflection frag_reflection =
          reflection_->reflect_stage(frag_span, ShaderStage::Fragment);
      data.reflection = std::move(vert_reflection);
      data.reflection.merge(frag_reflection);
      reflection_from_spirv = true;
      reflection_loaded = true;
    } catch (const std::exception &e) {
      std::cerr << "  Warning: Metal shader reflection SPIR-V load failed: " << e.what()
                << std::endl;
    }

    if (!reflection_loaded) {
      if (auto cached = load_reflection_cache(cache_path)) {
        std::cout << "  Loaded cached reflection data from " << cache_path << std::endl;
        data.reflection = std::move(*cached);
        reflection_loaded = true;
      }
    }

    if (!reflection_loaded) {
      throw std::runtime_error(
          "Failed to obtain Metal shader reflection data; expected SPIR-V or cache file");
    }

    reflection_->finalize_reflection(context, variant, data.reflection);

    if (reflection_from_spirv) {
      write_reflection_cache(data.reflection, cache_path);
    }

    return data;
  }

private:
  std::unique_ptr<ShaderReflectionSystem> reflection_;
};

} // namespace

std::unique_ptr<ShaderReflectionSystem> create_spirv_reflection_system() {
  return std::make_unique<SpirvShaderReflectionSystem>();
}

std::unique_ptr<ShaderReflectionSystem> create_metal_reflection_system() {
  return std::make_unique<MetalShaderReflectionSystem>();
}

std::unique_ptr<ShaderVariantSystem>
create_spirv_variant_system(std::unique_ptr<ShaderReflectionSystem> reflection) {
  return std::make_unique<SpirvShaderVariantSystem>(std::move(reflection));
}

std::unique_ptr<ShaderVariantSystem>
create_metal_variant_system(std::unique_ptr<ShaderReflectionSystem> reflection) {
  return std::make_unique<MetalShaderVariantSystem>(std::move(reflection));
}

} // namespace pixel::renderer3d

