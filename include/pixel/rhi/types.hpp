#pragma once
#include <cstddef>
#include <cstdint>
#include <array>

#include "handles.hpp"

namespace pixel::rhi {

enum class Format {
  Unknown,
  RGBA8,
  BGRA8,
  R8,
  R16F,
  RG16F,
  RGBA16F,
  D24S8,
  D32F
};

enum class LoadOp : uint8_t {
  Load,
  Clear,
  DontCare,
};

enum class StoreOp : uint8_t {
  Store,
  DontCare,
};

enum class BlendFactor : uint8_t {
  Zero,
  One,
  SrcColor,
  OneMinusSrcColor,
  DstColor,
  OneMinusDstColor,
  SrcAlpha,
  OneMinusSrcAlpha,
  DstAlpha,
  OneMinusDstAlpha,
  SrcAlphaSaturated,
};

enum class BlendOp : uint8_t {
  Add,
  Subtract,
  ReverseSubtract,
  Min,
  Max,
};

enum class CompareOp {
  Never,
  Less,
  Equal,
  LessEqual,
  Greater,
  NotEqual,
  GreaterEqual,
  Always
};

enum class StencilOp {
  Keep,
  Zero,
  Replace,
  IncrementClamp,
  DecrementClamp,
  Invert,
  IncrementWrap,
  DecrementWrap
};
struct Extent2D {
  uint32_t w{0}, h{0};
};

enum class BufferUsage : uint32_t {
  None = 0,
  Vertex = 1,
  Index = 2,
  Uniform = 4,
  Storage = 8,      // For compute shader read/write
  TransferSrc = 16,
  TransferDst = 32
};
inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
  return BufferUsage(uint32_t(a) | uint32_t(b));
}
inline BufferUsage operator&(BufferUsage a, BufferUsage b) {
  return BufferUsage(uint32_t(a) & uint32_t(b));
}

struct BufferDesc {
  size_t size;
  BufferUsage usage;
  bool hostVisible{false};
};
struct TextureDesc {
  Extent2D size;
  Format format;
  uint32_t mipLevels{1};
  uint32_t layers{1};
  bool renderTarget{false};
};
enum class FilterMode : uint8_t { Nearest, Linear };

enum class AddressMode : uint8_t {
  Repeat,
  ClampToEdge,
  ClampToBorder,
};

struct SamplerDesc {
  FilterMode minFilter{FilterMode::Linear};
  FilterMode magFilter{FilterMode::Linear};
  AddressMode addressU{AddressMode::Repeat};
  AddressMode addressV{AddressMode::Repeat};
  AddressMode addressW{AddressMode::Repeat};
  float mipLodBias{0.0f};
  bool aniso{false};
  float maxAnisotropy{1.0f};
  bool compareEnable{false};
  CompareOp compareOp{CompareOp::LessEqual};
  float borderColor[4]{0.0f, 0.0f, 0.0f, 0.0f};
};

struct BlendState {
  bool enabled{false};
  BlendFactor srcColor{BlendFactor::One};
  BlendFactor dstColor{BlendFactor::Zero};
  BlendOp colorOp{BlendOp::Add};
  BlendFactor srcAlpha{BlendFactor::One};
  BlendFactor dstAlpha{BlendFactor::Zero};
  BlendOp alphaOp{BlendOp::Add};

  bool operator==(const BlendState &) const = default;
};

struct ColorAttachmentDesc {
  Format format{Format::BGRA8};
  BlendState blend{};

  bool operator==(const ColorAttachmentDesc &) const = default;
};

struct DepthStencilState {
  bool depthTestEnable{true};
  bool depthWriteEnable{true};
  CompareOp depthCompare{CompareOp::Less};

  bool stencilEnable{false};
  CompareOp stencilCompare{CompareOp::Always};
  StencilOp stencilFailOp{StencilOp::Keep};
  StencilOp stencilDepthFailOp{StencilOp::Keep};
  StencilOp stencilPassOp{StencilOp::Keep};
  uint32_t stencilReadMask{0xFF};
  uint32_t stencilWriteMask{0xFF};
  uint32_t stencilReference{0};

  bool operator==(const DepthStencilState &) const = default;
};

struct DepthBiasState {
  bool enable{false};
  float constantFactor{0.0f};
  float slopeFactor{0.0f};
};

constexpr size_t kMaxColorAttachments = 4;

inline BlendState make_disabled_blend_state() {
  BlendState state;
  state.enabled = false;
  return state;
}

inline BlendState make_alpha_blend_state() {
  BlendState state;
  state.enabled = true;
  state.srcColor = BlendFactor::SrcAlpha;
  state.dstColor = BlendFactor::OneMinusSrcAlpha;
  state.srcAlpha = BlendFactor::One;
  state.dstAlpha = BlendFactor::OneMinusSrcAlpha;
  return state;
}

inline BlendState make_additive_blend_state() {
  BlendState state;
  state.enabled = true;
  state.srcColor = BlendFactor::SrcAlpha;
  state.dstColor = BlendFactor::One;
  state.srcAlpha = BlendFactor::One;
  state.dstAlpha = BlendFactor::One;
  return state;
}

inline BlendState make_multiply_blend_state() {
  BlendState state;
  state.enabled = true;
  state.srcColor = BlendFactor::DstColor;
  state.dstColor = BlendFactor::Zero;
  state.srcAlpha = BlendFactor::One;
  state.dstAlpha = BlendFactor::OneMinusSrcAlpha;
  return state;
}

enum class QueryType : uint8_t {
  Timestamp,
  TimeElapsed,
};

enum class PipelineStage : uint32_t {
  TopOfPipe,
  VertexShader,
  FragmentShader,
  ComputeShader,
  Transfer,
  BottomOfPipe,
};

enum class ResourceState : uint32_t {
  Undefined,
  General,
  CopySrc,
  CopyDst,
  ShaderRead,
  ShaderWrite,
  RenderTarget,
  DepthStencilRead,
  DepthStencilWrite,
  Present,
};

enum class BarrierType : uint8_t { Buffer, Texture };

struct ResourceBarrierDesc {
  BarrierType type{BarrierType::Buffer};
  PipelineStage srcStage{PipelineStage::TopOfPipe};
  PipelineStage dstStage{PipelineStage::BottomOfPipe};
  ResourceState srcState{ResourceState::Undefined};
  ResourceState dstState{ResourceState::Undefined};
  BufferHandle buffer{};
  TextureHandle texture{};
  uint32_t baseMipLevel{0};
  uint32_t levelCount{1};
  uint32_t baseArrayLayer{0};
  uint32_t layerCount{1};
};

} // namespace pixel::rhi
