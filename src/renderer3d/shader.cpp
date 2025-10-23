#include "pixel/renderer3d/renderer.hpp"
#include "pixel/platform/shader_loader.hpp"
#include "pixel/renderer3d/shader_reflection.hpp"
#include <cctype>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <iostream>

namespace pixel::renderer3d {

namespace {

std::string_view ltrim_view(std::string_view value) {
  size_t index = 0;
  while (index < value.size() &&
         std::isspace(static_cast<unsigned char>(value[index]))) {
    ++index;
  }
  return value.substr(index);
}

std::string_view rtrim_view(std::string_view value) {
  size_t end = value.size();
  while (end > 0 && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(0, end);
}

std::string_view trim_view(std::string_view value) {
  return rtrim_view(ltrim_view(value));
}

bool is_identifier_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_identifier_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool string_to_bool(std::string_view value) {
  value = trim_view(value);
  if (value.empty()) {
    return false;
  }
  bool all_digits = true;
  for (char c : value) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      all_digits = false;
      break;
    }
  }
  if (all_digits) {
    int result = 0;
    for (char c : value) {
      result = result * 10 + (c - '0');
      if (result != 0) {
        // Early out once we know the value is non-zero
        return true;
      }
    }
    return result != 0;
  }

  std::string lowercase;
  lowercase.reserve(value.size());
  for (char c : value) {
    lowercase.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }

  if (lowercase == "false" || lowercase == "off") {
    return false;
  }
  return true;
}

class ExpressionParser {
public:
  ExpressionParser(std::string_view expression,
                   const ShaderVariantKey::DefineMap &defines)
      : expr_(expression), defines_(defines) {}

  bool parse() {
    pos_ = 0;
    bool value = parse_or();
    skip_whitespace();
    return value;
  }

private:
  bool parse_or() {
    bool value = parse_and();
    while (true) {
      skip_whitespace();
      if (match("||")) {
        bool rhs = parse_and();
        value = value || rhs;
      } else {
        break;
      }
    }
    return value;
  }

  bool parse_and() {
    bool value = parse_unary();
    while (true) {
      skip_whitespace();
      if (match("&&")) {
        bool rhs = parse_unary();
        value = value && rhs;
      } else {
        break;
      }
    }
    return value;
  }

  bool parse_unary() {
    skip_whitespace();
    if (match("!")) {
      return !parse_unary();
    }
    return parse_primary();
  }

  bool parse_primary() {
    skip_whitespace();
    if (match("(")) {
      bool value = parse_or();
      skip_whitespace();
      match(")");
      return value;
    }

    size_t saved_pos = pos_;
    std::string_view identifier = read_identifier();
    if (!identifier.empty()) {
      if (identifier == "defined") {
        skip_whitespace();
        bool has_paren = match("(");
        skip_whitespace();
        std::string_view target = read_identifier();
        bool result = false;
        if (!target.empty()) {
          result = defines_.contains(target);
        }
        skip_whitespace();
        if (has_paren) {
          match(")");
        }
        return result;
      }

      return identifier_value(identifier);
    }

    pos_ = saved_pos;
    if (auto number = read_number(); number.has_value()) {
      return number.value();
    }

    return false;
  }

  std::optional<bool> read_number() {
    skip_whitespace();
    size_t start = pos_;
    bool has_digits = false;
    while (pos_ < expr_.size() &&
           std::isdigit(static_cast<unsigned char>(expr_[pos_]))) {
      has_digits = true;
      ++pos_;
    }
    if (!has_digits) {
      pos_ = start;
      return std::nullopt;
    }

    int value = 0;
    for (size_t i = start; i < pos_; ++i) {
      value = value * 10 + (expr_[i] - '0');
    }
    return value != 0;
  }

  std::string_view read_identifier() {
    size_t start = pos_;
    if (start >= expr_.size() || !is_identifier_start(expr_[start])) {
      return {};
    }
    ++pos_;
    while (pos_ < expr_.size() && is_identifier_char(expr_[pos_])) {
      ++pos_;
    }
    return expr_.substr(start, pos_ - start);
  }

  bool identifier_value(std::string_view identifier) const {
    auto it = defines_.find(identifier);
    if (it == defines_.end()) {
      return false;
    }
    return string_to_bool(it->second);
  }

  bool match(std::string_view token) {
    if (expr_.substr(pos_).starts_with(token)) {
      pos_ += token.size();
      return true;
    }
    return false;
  }

  void skip_whitespace() {
    while (pos_ < expr_.size() &&
           std::isspace(static_cast<unsigned char>(expr_[pos_]))) {
      ++pos_;
    }
  }

  std::string_view expr_;
  const ShaderVariantKey::DefineMap &defines_;
  size_t pos_{0};
};

class ShaderPreprocessor {
public:
  explicit ShaderPreprocessor(const ShaderVariantKey &variant)
      : defines_(variant.defines()) {}

  std::string process(const std::string &source) const {
    std::istringstream stream(source);
    std::string line;
    std::vector<std::string> output_lines;

    struct ConditionalState {
      bool parent_active;
      bool branch_taken;
      bool active;
    };

    std::vector<ConditionalState> stack;
    stack.push_back({true, true, true});

    while (std::getline(stream, line)) {
      std::string_view trimmed = ltrim_view(line);

      if (trimmed.starts_with("#if ") || trimmed == "#if") {
        bool condition = evaluate_expression(trimmed.substr(3));
        const bool parent_active = stack.back().active;
        stack.push_back({parent_active, parent_active && condition,
                         parent_active && condition});
        continue;
      }

      if (trimmed.starts_with("#ifdef")) {
        std::string_view identifier = trim_view(trimmed.substr(6));
        bool condition = defines_.contains(identifier);
        const bool parent_active = stack.back().active;
        stack.push_back({parent_active, parent_active && condition,
                         parent_active && condition});
        continue;
      }

      if (trimmed.starts_with("#ifndef")) {
        std::string_view identifier = trim_view(trimmed.substr(7));
        bool condition = !defines_.contains(identifier);
        const bool parent_active = stack.back().active;
        stack.push_back({parent_active, parent_active && condition,
                         parent_active && condition});
        continue;
      }

      if (trimmed.starts_with("#elif")) {
        if (stack.size() <= 1) {
          throw std::runtime_error("Encountered #elif without matching #if");
        }
        auto &state = stack.back();
        const bool parent_active = state.parent_active;
        if (!parent_active || state.branch_taken) {
          state.active = false;
          continue;
        }
        bool condition = evaluate_expression(trimmed.substr(5));
        state.branch_taken = parent_active && condition;
        state.active = parent_active && condition;
        continue;
      }

      if (trimmed.starts_with("#else")) {
        if (stack.size() <= 1) {
          throw std::runtime_error("Encountered #else without matching #if");
        }
        auto &state = stack.back();
        const bool parent_active = state.parent_active;
        if (!parent_active || state.branch_taken) {
          state.active = false;
        } else {
          state.branch_taken = true;
          state.active = true;
        }
        continue;
      }

      if (trimmed.starts_with("#endif")) {
        if (stack.size() <= 1) {
          throw std::runtime_error("Encountered #endif without matching #if");
        }
        stack.pop_back();
        continue;
      }

      if (stack.back().active) {
        output_lines.push_back(line);
      }
    }

    if (stack.size() != 1) {
      throw std::runtime_error("Unmatched #if/#endif directives in shader");
    }

    std::string processed;
    if (!output_lines.empty()) {
      size_t total_size = 0;
      for (const auto &out_line : output_lines) {
        total_size += out_line.size() + 1;
      }
      processed.reserve(total_size);
      for (size_t i = 0; i < output_lines.size(); ++i) {
        processed.append(output_lines[i]);
        if (i + 1 < output_lines.size()) {
          processed.push_back('\n');
        }
      }
    }

    return apply_defines(processed);
  }

private:
  bool evaluate_expression(std::string_view expression) const {
    expression = trim_view(expression);
    ExpressionParser parser(expression, defines_);
    return parser.parse();
  }

  std::string apply_defines(const std::string &source) const {
    if (defines_.empty() || source.empty()) {
      return source;
    }

    std::string result;
    result.reserve(source.size());

    bool in_line_comment = false;
    bool in_block_comment = false;
    bool in_string = false;
    char string_delimiter = '\0';

    for (size_t i = 0; i < source.size();) {
      char c = source[i];

      if (in_line_comment) {
        result.push_back(c);
        ++i;
        if (c == '\n') {
          in_line_comment = false;
        }
        continue;
      }

      if (in_block_comment) {
        if (c == '*' && i + 1 < source.size() && source[i + 1] == '/') {
          result.push_back(c);
          result.push_back('/');
          i += 2;
          in_block_comment = false;
        } else {
          result.push_back(c);
          ++i;
        }
        continue;
      }

      if (in_string) {
        result.push_back(c);
        ++i;
        if (c == '\\' && i < source.size()) {
          result.push_back(source[i]);
          ++i;
          continue;
        }
        if (c == string_delimiter) {
          in_string = false;
        }
        continue;
      }

      if (c == '/' && i + 1 < source.size()) {
        char next = source[i + 1];
        if (next == '/') {
          result.push_back(c);
          result.push_back(next);
          i += 2;
          in_line_comment = true;
          continue;
        }
        if (next == '*') {
          result.push_back(c);
          result.push_back(next);
          i += 2;
          in_block_comment = true;
          continue;
        }
      }

      if (c == '"' || c == '\x27') {
        in_string = true;
        string_delimiter = c;
        result.push_back(c);
        ++i;
        continue;
      }

      if (is_identifier_start(c)) {
        size_t start = i;
        ++i;
        while (i < source.size() && is_identifier_char(source[i])) {
          ++i;
        }
        std::string_view token(&source[start], i - start);
        auto it = defines_.find(token);
        if (it != defines_.end()) {
          result.append(it->second);
        } else {
          result.append(token);
        }
        continue;
      }

      result.push_back(c);
      ++i;
    }

    return result;
  }

  const ShaderVariantKey::DefineMap &defines_;
};

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
                                       const std::string &frag_path) {
  auto shader = std::unique_ptr<Shader>(new Shader());
  shader->device_ = device;

  std::cout << "Shader::create() loading files:" << std::endl;
  std::cout << "  Vertex:   " << vert_path << std::endl;
  std::cout << "  Fragment: " << frag_path << std::endl;

  auto [vert_src, frag_src] = platform::load_shader_pair(vert_path, frag_path);
  shader->vert_source_ = std::move(vert_src);
  shader->frag_source_ = std::move(frag_src);

  std::cout << "  Vertex source size: " << shader->vert_source_.size()
            << " bytes" << std::endl;
  std::cout << "  Fragment source size: " << shader->frag_source_.size()
            << " bytes" << std::endl;

  const bool is_instanced_shader =
      vert_path.find("instanced") != std::string::npos ||
      frag_path.find("instanced") != std::string::npos;

  shader->vs_stage_ = is_instanced_shader ? "vs_instanced" : "vs";
  shader->fs_stage_ = is_instanced_shader ? "fs_instanced" : "fs";

  std::cout << "  Vertex stage label: " << shader->vs_stage_ << std::endl;
  std::cout << "  Fragment stage label: " << shader->fs_stage_ << std::endl;

  ShaderVariantKey default_variant;
  shader->variant_cache_.emplace(default_variant.cache_key(),
                                 shader->build_variant(default_variant));

  std::cout << "  Default shader variant compiled and cached" << std::endl;

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
  return inserted->second;
}

Shader::VariantData
Shader::build_variant(const ShaderVariantKey &variant) const {
  if (!device_) {
    throw std::runtime_error("Shader created without a valid device");
  }

  std::string cache_key = variant.cache_key();
  std::cout << "Shader::build_variant()" << std::endl;
  std::cout << "  Variant cache key: '" << cache_key << "'" << std::endl;

  ShaderPreprocessor preprocessor(variant);
  std::string processed_vert = preprocessor.process(vert_source_);
  std::string processed_frag = preprocessor.process(frag_source_);

  if (processed_vert.empty()) {
    throw std::runtime_error("Vertex shader source empty after preprocessing");
  }
  if (processed_frag.empty()) {
    throw std::runtime_error(
        "Fragment shader source empty after preprocessing");
  }

  VariantData data{};

  std::span<const uint8_t> vs_bytes(
      reinterpret_cast<const uint8_t *>(processed_vert.data()),
      processed_vert.size());
  data.vs = device_->createShader(vs_stage_, vs_bytes);
  if (data.vs.id == 0) {
    std::cerr << "  ERROR: createShader returned invalid vertex shader handle"
              << std::endl;
  } else {
    std::cout << "  Vertex shader handle: " << data.vs.id << std::endl;
  }

  std::span<const uint8_t> fs_bytes(
      reinterpret_cast<const uint8_t *>(processed_frag.data()),
      processed_frag.size());
  data.fs = device_->createShader(fs_stage_, fs_bytes);
  if (data.fs.id == 0) {
    std::cerr << "  ERROR: createShader returned invalid fragment shader handle"
              << std::endl;
  } else {
    std::cout << "  Fragment shader handle: " << data.fs.id << std::endl;
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
  std::cout << "  Pipeline (Alpha) handle: "
            << data.pipelines[static_cast<size_t>(Material::BlendMode::Alpha)].id
            << std::endl;
  data.pipelines[static_cast<size_t>(Material::BlendMode::Additive)] =
      device_->createPipeline(build_desc(rhi::make_additive_blend_state()));
  std::cout << "  Pipeline (Additive) handle: "
            << data
                   .pipelines[static_cast<size_t>(Material::BlendMode::Additive)]
                   .id
            << std::endl;
  data.pipelines[static_cast<size_t>(Material::BlendMode::Multiply)] =
      device_->createPipeline(build_desc(rhi::make_multiply_blend_state()));
  std::cout << "  Pipeline (Multiply) handle: "
            << data
                   .pipelines[static_cast<size_t>(Material::BlendMode::Multiply)]
                   .id
            << std::endl;
  data.pipelines[static_cast<size_t>(Material::BlendMode::Opaque)] =
      device_->createPipeline(build_desc(rhi::make_disabled_blend_state()));
  std::cout << "  Pipeline (Opaque) handle: "
            << data.pipelines[static_cast<size_t>(Material::BlendMode::Opaque)].id
            << std::endl;

  ShaderReflection vert_reflection =
      reflect_shader(processed_vert, ShaderStage::Vertex);
  ShaderReflection frag_reflection =
      reflect_shader(processed_frag, ShaderStage::Fragment);
  data.reflection = std::move(vert_reflection);
  data.reflection.merge(frag_reflection);
  std::cout << "  Reflection summary: uniforms="
            << data.reflection.uniforms().size()
            << " samplers=" << data.reflection.samplers().size() << std::endl;

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
