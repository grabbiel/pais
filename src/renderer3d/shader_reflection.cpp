#include "pixel/renderer3d/shader_reflection.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <regex>

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

std::string to_lower(std::string_view value) {
  std::string result(value.begin(), value.end());
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

std::string_view trim_view(std::string_view value) {
  const char *begin = value.data();
  const char *end = begin + value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }
  return std::string_view(begin, static_cast<size_t>(end - begin));
}

std::string trim_copy(std::string_view value) {
  return std::string(trim_view(value));
}

std::string remove_comments(std::string_view source) {
  std::string output;
  output.reserve(source.size());

  bool in_single = false;
  bool in_multi = false;

  for (size_t i = 0; i < source.size(); ++i) {
    char c = source[i];
    if (in_single) {
      if (c == '\n') {
        in_single = false;
        output.push_back(c);
      }
      continue;
    }
    if (in_multi) {
      if (c == '*' && i + 1 < source.size() && source[i + 1] == '/') {
        in_multi = false;
        ++i;
      }
      continue;
    }

    if (c == '/' && i + 1 < source.size()) {
      char next = source[i + 1];
      if (next == '/') {
        in_single = true;
        ++i;
        continue;
      }
      if (next == '*') {
        in_multi = true;
        ++i;
        continue;
      }
    }

    output.push_back(c);
  }

  return output;
}

std::optional<uint32_t> parse_binding(std::string_view layout_str) {
  size_t pos = layout_str.find("binding");
  if (pos == std::string_view::npos)
    return std::nullopt;

  pos = layout_str.find('=', pos);
  if (pos == std::string_view::npos)
    return std::nullopt;
  ++pos;

  while (pos < layout_str.size() &&
         std::isspace(static_cast<unsigned char>(layout_str[pos]))) {
    ++pos;
  }

  size_t end = pos;
  while (end < layout_str.size() &&
         std::isdigit(static_cast<unsigned char>(layout_str[end]))) {
    ++end;
  }

  if (end == pos)
    return std::nullopt;

  uint32_t value = 0;
  auto result = std::from_chars(layout_str.data() + pos,
                                layout_str.data() + end, value);
  if (result.ec != std::errc{})
    return std::nullopt;
  return value;
}

bool is_qualifier(std::string_view token) {
  static constexpr std::string_view qualifiers[] = {
      "const",   "in",     "out",     "inout",   "centroid",
      "flat",    "smooth", "noperspective", "patch",   "sample",
      "uniform", "buffer", "shared",  "coherent", "volatile",
      "restrict","readonly","writeonly", "precise",  "highp",
      "mediump", "lowp"};

  std::string lower = to_lower(token);
  for (auto qualifier : qualifiers) {
    if (lower == qualifier)
      return true;
  }
  return false;
}

ShaderUniformType to_uniform_type(std::string_view type_name) {
  std::string lower = to_lower(type_name);
  if (lower == "float")
    return ShaderUniformType::Float;
  if (lower == "vec2")
    return ShaderUniformType::Vec2;
  if (lower == "vec3")
    return ShaderUniformType::Vec3;
  if (lower == "vec4")
    return ShaderUniformType::Vec4;
  if (lower == "mat3")
    return ShaderUniformType::Mat3;
  if (lower == "mat4")
    return ShaderUniformType::Mat4;
  if (lower == "int")
    return ShaderUniformType::Int;
  if (lower == "uint")
    return ShaderUniformType::UInt;
  if (lower == "bool")
    return ShaderUniformType::Bool;
  if (lower == "sampler1d")
    return ShaderUniformType::Sampler1D;
  if (lower == "sampler2d")
    return ShaderUniformType::Sampler2D;
  if (lower == "sampler2darray")
    return ShaderUniformType::Sampler2DArray;
  if (lower == "sampler3d")
    return ShaderUniformType::Sampler3D;
  if (lower == "samplercube")
    return ShaderUniformType::SamplerCube;
  if (lower == "sampler2dshadow")
    return ShaderUniformType::Sampler2DShadow;
  if (lower == "image2d")
    return ShaderUniformType::Image2D;
  return ShaderUniformType::Unknown;
}

struct DeclarationParseResult {
  std::string type;
  std::string name;
  uint32_t array_size{1};
};

std::optional<DeclarationParseResult> parse_declaration(std::string_view decl) {
  std::string trimmed = trim_copy(decl);
  if (trimmed.empty())
    return std::nullopt;

  auto assign_pos = trimmed.find('=');
  if (assign_pos != std::string::npos) {
    trimmed = trim_copy(std::string_view(trimmed.data(), assign_pos));
  }

  size_t bracket = trimmed.find('[');
  uint32_t array_size = 1;
  if (bracket != std::string::npos) {
    size_t closing = trimmed.find(']', bracket);
    if (closing != std::string::npos) {
      std::string_view number(trimmed.data() + bracket + 1,
                              closing - bracket - 1);
      number = trim_view(number);
      if (!number.empty()) {
        uint32_t value = 0;
        auto conv = std::from_chars(number.data(),
                                    number.data() + number.size(), value);
        if (conv.ec == std::errc{})
          array_size = value;
      }
      trimmed.erase(bracket, closing - bracket + 1);
      trimmed = trim_copy(trimmed);
    }
  }

  size_t last_space = trimmed.find_last_of(" \t\r\n");
  if (last_space == std::string::npos)
    return std::nullopt;

  std::string name = trim_copy(trimmed.substr(last_space + 1));
  std::string before_name = trim_copy(trimmed.substr(0, last_space));

  if (name.empty() || before_name.empty())
    return std::nullopt;

  std::vector<std::string> tokens;
  size_t start = 0;
  while (start < before_name.size()) {
    size_t end = before_name.find_first_of(" \t\r\n", start);
    if (end == std::string::npos)
      end = before_name.size();
    std::string token = before_name.substr(start, end - start);
    if (!token.empty())
      tokens.push_back(token);
    start = end + 1;
  }

  while (!tokens.empty() && is_qualifier(tokens.front())) {
    tokens.erase(tokens.begin());
  }

  if (tokens.empty())
    return std::nullopt;

  std::string type = tokens.back();

  return DeclarationParseResult{type, name, array_size};
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

  if (auto existing = find_existing(block.block_name);
      existing != nullptr) {
    existing->stage_mask |= block.stage_mask;
    if (!existing->binding && block.binding)
      existing->binding = block.binding;
    if (existing->members.empty() && !block.members.empty())
      existing->members = block.members;
    if (existing->instance_name.empty() && !block.instance_name.empty())
      existing->instance_name = block.instance_name;
    add_lookup(block.instance_name, static_cast<size_t>(existing - blocks_order_.data()));
    return;
  }

  if (auto existing = find_existing(block.instance_name);
      existing != nullptr) {
    existing->stage_mask |= block.stage_mask;
    if (!existing->binding && block.binding)
      existing->binding = block.binding;
    if (existing->members.empty() && !block.members.empty())
      existing->members = block.members;
    if (existing->block_name.empty() && !block.block_name.empty())
      existing->block_name = block.block_name;
    add_lookup(block.block_name, static_cast<size_t>(existing - blocks_order_.data()));
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

std::optional<uint32_t>
ShaderReflection::binding_for_block(std::string_view name,
                                    ShaderBlockType type) const {
  const ShaderBlock *block = find_block(name, type);
  if (!block)
    return std::nullopt;
  return block->binding;
}

ShaderReflection reflect_glsl(std::string_view source, ShaderStage stage) {
  ShaderReflection reflection;
  std::string no_comments = remove_comments(source);
  std::string sanitized = no_comments;

  std::regex block_regex(
      R"((layout\s*\(([^)]*)\)\s*)?(uniform|buffer)\s+([A-Za-z_]\w*)\s*\{([^}]*)\}\s*([A-Za-z_]\w*)?\s*;)",
      std::regex::optimize);

  std::vector<std::pair<size_t, size_t>> block_ranges;
  for (auto it = std::sregex_iterator(no_comments.begin(), no_comments.end(),
                                      block_regex);
       it != std::sregex_iterator(); ++it) {
    const std::smatch &match = *it;
    ShaderBlock block;
    block.type = (match[3].str() == "buffer") ? ShaderBlockType::Storage
                                               : ShaderBlockType::Uniform;
    block.block_name = match[4].str();
    block.instance_name = match[6].matched ? match[6].str() : std::string();
    block.add_stage(stage);
    if (match[2].matched) {
      if (auto binding = parse_binding(match[2].str())) {
        block.binding = binding;
      }
    }

    std::string members_src = match[5].str();
    size_t cursor = 0;
    while (cursor < members_src.size()) {
      size_t semicolon = members_src.find(';', cursor);
      if (semicolon == std::string::npos)
        break;
      std::string_view decl(members_src.data() + cursor,
                            semicolon - cursor);
      cursor = semicolon + 1;
      auto parsed = parse_declaration(decl);
      if (!parsed)
        continue;
      ShaderBlockMember member;
      member.name = parsed->name;
      member.type = to_uniform_type(parsed->type);
      member.array_size = parsed->array_size;
      block.members.push_back(std::move(member));
    }

    reflection.add_block(block);
    block_ranges.emplace_back(static_cast<size_t>(match.position()),
                              static_cast<size_t>(match.length()));
  }

  for (auto it = block_ranges.rbegin(); it != block_ranges.rend(); ++it) {
    sanitized.replace(it->first, it->second, it->second, ' ');
  }

  size_t pos = 0;
  while ((pos = sanitized.find("uniform", pos)) != std::string::npos) {
    if (pos > 0) {
      char prev = sanitized[pos - 1];
      if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_') {
        pos += 7;
        continue;
      }
    }

    size_t start = pos + 7;
    size_t end = sanitized.find(';', start);
    if (end == std::string::npos)
      break;

    std::string declaration = sanitized.substr(start, end - start);
    pos = end + 1;

    std::optional<uint32_t> binding;
    std::string trimmed = trim_copy(declaration);
    if (trimmed.rfind("layout", 0) == 0) {
      size_t close = trimmed.find(')');
      if (close != std::string::npos) {
        std::string layout = trimmed.substr(0, close + 1);
        if (auto parsed_binding = parse_binding(layout))
          binding = parsed_binding;
        trimmed.erase(0, close + 1);
      }
    }

    auto parsed = parse_declaration(trimmed);
    if (!parsed)
      continue;

    ShaderUniform uniform;
    uniform.name = parsed->name;
    uniform.type = to_uniform_type(parsed->type);
    uniform.array_size = parsed->array_size;
    uniform.binding = binding;
    uniform.add_stage(stage);
    reflection.add_uniform(std::move(uniform));
  }

  return reflection;
}

} // namespace pixel::renderer3d

