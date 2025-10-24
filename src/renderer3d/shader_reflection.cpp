#include "pixel/renderer3d/shader_reflection.hpp"

#include <spirv_reflect.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pixel::renderer3d {

namespace {

constexpr uint32_t stage_bit_value(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return 1u << 0;
  case ShaderStage::Fragment:
    return 1u << 1;
  case ShaderStage::Compute:
    return 1u << 2;
  default:
    return 0u;
  }
}

ShaderUniformType uniform_type_from_description(
    const SpvReflectTypeDescription &type) {
  if ((type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX) != 0) {
    const uint32_t columns = type.traits.numeric.matrix.column_count;
    const uint32_t rows = type.traits.numeric.matrix.row_count;
    if (columns == 4 && rows == 4)
      return ShaderUniformType::Mat4;
    if (columns == 3 && rows == 3)
      return ShaderUniformType::Mat3;
  }

  if ((type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR) != 0) {
    const uint32_t components = type.traits.numeric.vector.component_count;
    if ((type.type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0) {
      switch (components) {
      case 2:
        return ShaderUniformType::Vec2;
      case 3:
        return ShaderUniformType::Vec3;
      case 4:
        return ShaderUniformType::Vec4;
      default:
        break;
      }
    }
  }

  if ((type.type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0)
    return ShaderUniformType::Float;
  if ((type.type_flags & SPV_REFLECT_TYPE_FLAG_INT) != 0) {
    return type.traits.numeric.scalar.signedness ? ShaderUniformType::Int
                                                 : ShaderUniformType::UInt;
  }
  if ((type.type_flags & SPV_REFLECT_TYPE_FLAG_BOOL) != 0)
    return ShaderUniformType::Bool;

  return ShaderUniformType::Unknown;
}

uint32_t array_size_from(const SpvReflectBlockVariable &var) {
  if (var.array.dims_count == 0)
    return 1;
  uint32_t size = 1;
  for (uint32_t i = 0; i < var.array.dims_count; ++i) {
    const uint32_t dim = var.array.dims[i];
    size *= dim == 0 ? 1 : dim;
  }
  return size == 0 ? 1 : size;
}

void append_block_member(const SpvReflectBlockVariable &member,
                         const std::string &prefix, ShaderStage stage,
                         uint32_t binding, bool emit_uniforms,
                         ShaderBlock &block, ShaderReflection &reflection,
                         uint32_t ordinal = 0) {
  std::string member_name = member.name ? member.name : std::string();
  if (member_name.empty()) {
    member_name = "member" + std::to_string(ordinal);
  }

  std::string full_name = prefix.empty() ? member_name : prefix + "." + member_name;

  if (member.member_count > 0) {
    for (uint32_t i = 0; i < member.member_count; ++i) {
      append_block_member(member.members[i], full_name, stage, binding,
                          emit_uniforms, block, reflection, i);
    }
    return;
  }

  ShaderBlockMember block_member;
  block_member.name = full_name;
  block_member.type =
      member.type_description ? uniform_type_from_description(*member.type_description)
                              : ShaderUniformType::Unknown;
  block_member.array_size = array_size_from(member);
  block.members.push_back(block_member);

  if (!emit_uniforms)
    return;

  ShaderUniform uniform;
  uniform.name = full_name;
  uniform.type = block_member.type;
  uniform.array_size = block_member.array_size;
  uniform.binding = binding;
  uniform.add_stage(stage);
  reflection.add_uniform(std::move(uniform));
}

ShaderUniformType sampler_type_from_binding(
    const SpvReflectDescriptorBinding &binding) {
  switch (binding.descriptor_type) {
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    return ShaderUniformType::Image2D;
  case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
  case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
  case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
    break;
  default:
    return ShaderUniformType::Unknown;
  }

  const bool is_arrayed = binding.image.arrayed != 0;
  switch (binding.image.dim) {
  case SpvDim1D:
    return ShaderUniformType::Sampler1D;
  case SpvDim2D:
    if (binding.image.depth)
      return ShaderUniformType::Sampler2DShadow;
    return is_arrayed ? ShaderUniformType::Sampler2DArray
                      : ShaderUniformType::Sampler2D;
  case SpvDim3D:
    return ShaderUniformType::Sampler3D;
  case SpvDimCube:
    return ShaderUniformType::SamplerCube;
  default:
    break;
  }
  return ShaderUniformType::Unknown;
}

} // namespace

void ShaderUniform::add_stage(ShaderStage stage) {
  stage_mask |= stage_bit_value(stage);
}

bool ShaderUniform::uses_stage(ShaderStage stage) const {
  return (stage_mask & stage_bit_value(stage)) != 0;
}

bool ShaderUniform::is_sampler() const {
  switch (type) {
  case ShaderUniformType::Sampler1D:
  case ShaderUniformType::Sampler2D:
  case ShaderUniformType::Sampler2DArray:
  case ShaderUniformType::Sampler3D:
  case ShaderUniformType::SamplerCube:
  case ShaderUniformType::Sampler2DShadow:
    return true;
  default:
    return false;
  }
}

void ShaderBlock::add_stage(ShaderStage stage) {
  stage_mask |= stage_bit_value(stage);
}

bool ShaderBlock::uses_stage(ShaderStage stage) const {
  return (stage_mask & stage_bit_value(stage)) != 0;
}

uint32_t ShaderReflection::stage_bit(ShaderStage stage) {
  return stage_bit_value(stage);
}

void ShaderReflection::add_uniform(ShaderUniform uniform) {
  auto it = uniforms_.find(uniform.name);
  if (it == uniforms_.end()) {
    uniforms_.emplace(uniform.name, std::move(uniform));
    return;
  }

  ShaderUniform &existing = it->second;
  existing.stage_mask |= uniform.stage_mask;
  if (existing.type == ShaderUniformType::Unknown)
    existing.type = uniform.type;
  if (existing.array_size < uniform.array_size)
    existing.array_size = uniform.array_size;
  if (!existing.binding && uniform.binding)
    existing.binding = uniform.binding;
}

void ShaderReflection::add_block(ShaderBlock block) {
  auto add_lookup = [&](const std::string &key, size_t index) {
    if (!key.empty())
      block_lookup_[key] = index;
  };

  auto find_existing = [&](std::string_view key) -> ShaderBlock * {
    auto lookup = block_lookup_.find(std::string(key));
    if (lookup == block_lookup_.end())
      return nullptr;
    return &blocks_order_[lookup->second];
  };

  if (auto existing = find_existing(block.block_name); existing != nullptr) {
    existing->stage_mask |= block.stage_mask;
    if (!existing->binding && block.binding)
      existing->binding = block.binding;
    if (existing->members.empty() && !block.members.empty())
      existing->members = block.members;
    if (existing->instance_name.empty() && !block.instance_name.empty())
      existing->instance_name = block.instance_name;
    add_lookup(block.instance_name,
               static_cast<size_t>(existing - blocks_order_.data()));
    return;
  }

  if (auto existing = find_existing(block.instance_name); existing != nullptr) {
    existing->stage_mask |= block.stage_mask;
    if (!existing->binding && block.binding)
      existing->binding = block.binding;
    if (existing->members.empty() && !block.members.empty())
      existing->members = block.members;
    if (existing->block_name.empty() && !block.block_name.empty())
      existing->block_name = block.block_name;
    add_lookup(block.block_name,
               static_cast<size_t>(existing - blocks_order_.data()));
    return;
  }

  size_t index = blocks_order_.size();
  blocks_order_.push_back(std::move(block));
  add_lookup(blocks_order_.back().block_name, index);
  add_lookup(blocks_order_.back().instance_name, index);
}

void ShaderReflection::merge(const ShaderReflection &other) {
  for (const auto &[name, uniform] : other.uniforms_) {
    add_uniform(uniform);
  }

  for (const auto &block : other.blocks_order_) {
    add_block(block);
  }
}

bool ShaderReflection::has_uniform(std::string_view name) const {
  return uniforms_.find(std::string(name)) != uniforms_.end();
}

bool ShaderReflection::has_sampler(std::string_view name) const {
  auto it = uniforms_.find(std::string(name));
  if (it == uniforms_.end())
    return false;
  return it->second.is_sampler();
}

const ShaderUniform *ShaderReflection::find_uniform(std::string_view name) const {
  auto it = uniforms_.find(std::string(name));
  if (it == uniforms_.end())
    return nullptr;
  return &it->second;
}

const ShaderBlock *ShaderReflection::find_block(std::string_view name) const {
  auto it = block_lookup_.find(std::string(name));
  if (it == block_lookup_.end())
    return nullptr;
  if (it->second >= blocks_order_.size())
    return nullptr;
  return &blocks_order_[it->second];
}

const ShaderBlock *ShaderReflection::find_block(std::string_view name,
                                                ShaderBlockType type) const {
  const ShaderBlock *block = find_block(name);
  if (block && block->type == type)
    return block;
  return nullptr;
}

std::optional<uint32_t>
ShaderReflection::binding_for_block(std::string_view name) const {
  const ShaderBlock *block = find_block(name);
  if (!block)
    return std::nullopt;
  return block->binding;
}

std::optional<uint32_t> ShaderReflection::binding_for_block(
    std::string_view name, ShaderBlockType type) const {
  const ShaderBlock *block = find_block(name, type);
  if (!block)
    return std::nullopt;
  return block->binding;
}

ShaderReflection reflect_spirv(std::span<const uint32_t> words,
                               ShaderStage stage) {
  ShaderReflection reflection;
  if (words.empty())
    return reflection;

  SpvReflectShaderModule module{};
  SpvReflectResult result =
      spvReflectCreateShaderModule(words.size_bytes(), words.data(), &module);
  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    throw std::runtime_error("Failed to create SPIR-V reflection module");
  }

  auto destroy_module = [&]() { spvReflectDestroyShaderModule(&module); };

  uint32_t binding_count = 0;
  result = spvReflectEnumerateDescriptorBindings(&module, &binding_count, nullptr);
  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    destroy_module();
    throw std::runtime_error("Failed to enumerate SPIR-V descriptor bindings");
  }

  std::vector<SpvReflectDescriptorBinding *> bindings(binding_count);
  if (binding_count > 0) {
    result = spvReflectEnumerateDescriptorBindings(&module, &binding_count,
                                                   bindings.data());
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
      destroy_module();
      throw std::runtime_error("Failed to gather SPIR-V descriptor bindings");
    }
  }

  for (SpvReflectDescriptorBinding *binding : bindings) {
    if (!binding)
      continue;

    const uint32_t binding_index = binding->binding;
    const char *binding_name = binding->name;
    std::string resolved_name = binding_name ? std::string(binding_name)
                                             : ("binding_" + std::to_string(binding_index));

    switch (binding->descriptor_type) {
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: {
      ShaderBlock block;
      block.type = ShaderBlockType::Uniform;
      block.block_name = binding->block.name ? binding->block.name : resolved_name;
      block.instance_name = resolved_name;
      block.binding = binding_index;
      block.add_stage(stage);

      for (uint32_t i = 0; i < binding->block.member_count; ++i) {
        append_block_member(binding->block.members[i], std::string(), stage,
                            binding_index, true, block, reflection, i);
      }

      reflection.add_block(block);
      break;
    }
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
      ShaderBlock block;
      block.type = ShaderBlockType::Storage;
      block.block_name = binding->block.name ? binding->block.name : resolved_name;
      block.instance_name = resolved_name;
      block.binding = binding_index;
      block.add_stage(stage);

      for (uint32_t i = 0; i < binding->block.member_count; ++i) {
        append_block_member(binding->block.members[i], std::string(), stage,
                            binding_index, false, block, reflection, i);
      }

      reflection.add_block(block);
      break;
    }
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
      ShaderUniform uniform;
      uniform.name = resolved_name;
      uniform.type = sampler_type_from_binding(*binding);
      uniform.array_size = binding->count > 0 ? binding->count : 1;
      uniform.binding = binding_index;
      uniform.add_stage(stage);
      reflection.add_uniform(std::move(uniform));
      break;
    }
    default:
      break;
    }
  }

  destroy_module();
  return reflection;
}

} // namespace pixel::renderer3d
