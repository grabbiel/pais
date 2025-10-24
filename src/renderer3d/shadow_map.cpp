#include "pixel/renderer3d/shadow_map.hpp"
#include "pixel/math/vec3.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace pixel::renderer3d {

namespace {
glm::vec3 to_glm(const Vec3 &v) { return glm::vec3(v.x, v.y, v.z); }
}

bool ShadowMap::initialize(rhi::Device *device, const Settings &settings,
                           const DirectionalLight &light) {
  device_ = device;
  settings_ = settings;
  light_ = light;

  if (!device_) {
    return false;
  }

  rhi::TextureDesc depth_desc{};
  depth_desc.size = {settings_.resolution, settings_.resolution};
  depth_desc.format = rhi::Format::D32F;
  depth_desc.mipLevels = 1;
  depth_desc.layers = 1;
  depth_desc.renderTarget = true;
  depth_texture_ = device_->createTexture(depth_desc);
  if (depth_texture_.id == 0) {
    return false;
  }

  rhi::FramebufferDesc fb_desc{};
  fb_desc.colorAttachmentCount = 0;
  fb_desc.hasDepthAttachment = true;
  fb_desc.depthAttachment.texture = depth_texture_;
  fb_desc.depthAttachment.mipLevel = 0;
  fb_desc.depthAttachment.arraySlice = 0;
  fb_desc.depthAttachment.hasStencil = false;
  framebuffer_ = device_->createFramebuffer(fb_desc);
  if (framebuffer_.id == 0) {
    return false;
  }

  rhi::SamplerDesc sampler_desc{};
  sampler_desc.minFilter = rhi::FilterMode::Linear;
  sampler_desc.magFilter = rhi::FilterMode::Linear;
  sampler_desc.addressU = rhi::AddressMode::ClampToBorder;
  sampler_desc.addressV = rhi::AddressMode::ClampToBorder;
  sampler_desc.addressW = rhi::AddressMode::ClampToBorder;
  sampler_desc.compareEnable = device_->caps().samplerCompare;
  sampler_desc.compareOp = rhi::CompareOp::LessEqual;
  sampler_desc.borderColor[0] = 1.0f;
  sampler_desc.borderColor[1] = 1.0f;
  sampler_desc.borderColor[2] = 1.0f;
  sampler_desc.borderColor[3] = 1.0f;
  sampler_ = device_->createSampler(sampler_desc);
  if (sampler_.id == 0) {
    return false;
  }

  rebuild_pass_desc();
  compute_matrices();

  initialized_ = depth_texture_.id != 0 && framebuffer_.id != 0 && sampler_.id != 0;
  return initialized_;
}

void ShadowMap::update_light(const DirectionalLight &light) {
  light_ = light;
  compute_matrices();
}

void ShadowMap::update_settings(const Settings &settings) {
  settings_ = settings;
  compute_matrices();
  rebuild_pass_desc();
}

void ShadowMap::begin(rhi::CmdList *cmd) {
  if (!initialized_ || !cmd)
    return;

  pass_desc_.depthAttachment.clearDepth = 1.0f;
  cmd->beginRender(pass_desc_);
}

void ShadowMap::end(rhi::CmdList *cmd) {
  if (!initialized_ || !cmd)
    return;

  cmd->endRender();
}

rhi::DepthBiasState ShadowMap::depth_bias_state() const {
  rhi::DepthBiasState state{};
  state.enable = true;
  state.constantFactor = settings_.depth_bias_constant;
  state.slopeFactor = settings_.depth_bias_slope;
  return state;
}

void ShadowMap::rebuild_pass_desc() {
  pass_desc_ = {};
  pass_desc_.framebuffer = framebuffer_;
  pass_desc_.colorAttachmentCount = 0;
  pass_desc_.hasDepthAttachment = true;
  pass_desc_.depthAttachment.texture = depth_texture_;
  pass_desc_.depthAttachment.mipLevel = 0;
  pass_desc_.depthAttachment.arraySlice = 0;
  pass_desc_.depthAttachment.depthLoadOp = rhi::LoadOp::Clear;
  pass_desc_.depthAttachment.depthStoreOp = rhi::StoreOp::Store;
  pass_desc_.depthAttachment.stencilLoadOp = rhi::LoadOp::DontCare;
  pass_desc_.depthAttachment.stencilStoreOp = rhi::StoreOp::DontCare;
  pass_desc_.depthAttachment.clearDepth = 1.0f;
  pass_desc_.depthAttachment.clearStencil = 0;
  pass_desc_.depthAttachment.hasStencil = false;
}

void ShadowMap::compute_matrices() {
  glm::vec3 light_position = to_glm(light_.position);
  glm::vec3 light_direction = glm::normalize(to_glm(light_.direction));
  glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
  if (glm::abs(glm::dot(light_direction, up)) > 0.99f) {
    up = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  light_view_ =
      glm::lookAt(light_position, light_position + light_direction, up);

  float ortho = settings_.ortho_size;
  light_projection_ =
      glm::ortho(-ortho, ortho, -ortho, ortho, settings_.near_plane,
                 settings_.far_plane);
  light_view_projection_ = light_projection_ * light_view_;
}

} // namespace pixel::renderer3d
