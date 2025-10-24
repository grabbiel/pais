#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pixel::renderer3d {

enum class ShaderStage : uint8_t {
  Vertex = 0,
  Fragment = 1,
  Compute = 2,
  Unknown = 3,
};

enum class ShaderLanguage : uint8_t {
  GLSL = 0,
  Metal = 1,
};

enum class ShaderUniformType : uint8_t {
  Float,
  Vec2,
  Vec3,
  Vec4,
  Mat3,
  Mat4,
  Int,
  UInt,
  Bool,
  Sampler1D,
  Sampler2D,
  Sampler2DArray,
  Sampler3D,
  SamplerCube,
  Sampler2DShadow,
  Image2D,
  Unknown,
};

enum class ShaderBlockType : uint8_t {
  Uniform,
  Storage,
};

struct ShaderUniform {
  std::string name;
  ShaderUniformType type{ShaderUniformType::Unknown};
  uint32_t array_size{1};
  uint32_t stage_mask{0};
  std::optional<uint32_t> binding;

  void add_stage(ShaderStage stage);
  bool uses_stage(ShaderStage stage) const;
  bool is_sampler() const;
};

struct ShaderBlockMember {
  std::string name;
  ShaderUniformType type{ShaderUniformType::Unknown};
  uint32_t array_size{1};
};

struct ShaderBlock {
  ShaderBlockType type{ShaderBlockType::Uniform};
  std::string block_name;
  std::string instance_name;
  uint32_t stage_mask{0};
  std::optional<uint32_t> binding;
  std::vector<ShaderBlockMember> members;

  void add_stage(ShaderStage stage);
  bool uses_stage(ShaderStage stage) const;
  bool is_uniform() const { return type == ShaderBlockType::Uniform; }
  bool is_storage() const { return type == ShaderBlockType::Storage; }
};

class ShaderReflection {
public:
  void merge(const ShaderReflection &other);

  bool has_uniform(std::string_view name) const;
  bool has_sampler(std::string_view name) const;
  const ShaderUniform *find_uniform(std::string_view name) const;

  const ShaderBlock *find_block(std::string_view name) const;
  const ShaderBlock *find_block(std::string_view name,
                                ShaderBlockType type) const;
  std::optional<uint32_t> binding_for_block(std::string_view name) const;
  std::optional<uint32_t> binding_for_block(std::string_view name,
                                            ShaderBlockType type) const;

  std::vector<ShaderBlock> blocks() const { return blocks_order_; }
  std::unordered_map<std::string, ShaderUniform> uniforms() const {
    return uniforms_;
  }

  void add_uniform(ShaderUniform uniform);
  void add_block(ShaderBlock block);

private:
  static uint32_t stage_bit(ShaderStage stage);

  std::unordered_map<std::string, ShaderUniform> uniforms_;
  std::vector<ShaderBlock> blocks_order_;
  std::unordered_map<std::string, size_t> block_lookup_;
};

ShaderLanguage detect_shader_language(std::string_view source);

ShaderReflection reflect_shader(std::string_view source, ShaderStage stage,
                                ShaderLanguage language);

ShaderReflection reflect_shader(std::string_view source, ShaderStage stage);

ShaderReflection reflect_glsl(std::string_view source, ShaderStage stage);

ShaderReflection reflect_metal(std::string_view source, ShaderStage stage);

} // namespace pixel::renderer3d
