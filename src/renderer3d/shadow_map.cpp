#include "pixel/renderer3d/shadow_map.hpp"
#include "pixel/renderer3d/clip_space.hpp"
#include "pixel/math/vec3.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace pixel::renderer3d {

namespace {
glm::vec3 to_glm(const Vec3 &v) { return glm::vec3(v.x, v.y, v.z); }
}

bool ShadowMap::initialize(rhi::Device *device, const Settings &settings,
                           const DirectionalLight &light) {
  device_ = device;
  settings_ = settings;
  light_ = light;

  std::cout << "[ShadowMap] Initializing shadow map" << std::endl;
  std::cout << "  Resolution: " << settings_.resolution << "x"
            << settings_.resolution << std::endl;
  std::cout << "  Depth bias (constant/slope): " << settings_.depth_bias_constant
            << " / " << settings_.depth_bias_slope << std::endl;
  std::cout << "  Near/Far planes: " << settings_.near_plane << " / "
            << settings_.far_plane << std::endl;
  std::cout << "  Ortho size: " << settings_.ortho_size << std::endl;

  if (!device_) {
    std::cerr << "[ShadowMap] Initialization failed: device is null"
              << std::endl;
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
    std::cerr << "[ShadowMap] Failed to create depth texture" << std::endl;
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
    std::cerr << "[ShadowMap] Failed to create framebuffer" << std::endl;
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
    std::cerr << "[ShadowMap] Failed to create sampler" << std::endl;
    return false;
  }

  std::cout << "[ShadowMap] Sampler compare support: "
            << (sampler_desc.compareEnable ? "enabled" : "disabled")
            << std::endl;

  rebuild_pass_desc();
  compute_matrices();

  initialized_ = depth_texture_.id != 0 && framebuffer_.id != 0 && sampler_.id != 0;
  std::cout << "[ShadowMap] Initialization "
            << (initialized_ ? "succeeded" : "failed") << std::endl;
  return initialized_;
}

void ShadowMap::update_light(const DirectionalLight &light) {
  light_ = light;
  std::cout << "[ShadowMap] Updating light" << std::endl;
  std::cout << "  Position: (" << light_.position.x << ", " << light_.position.y
            << ", " << light_.position.z << ")" << std::endl;
  std::cout << "  Direction: (" << light_.direction.x << ", "
            << light_.direction.y << ", " << light_.direction.z << ")"
            << std::endl;
  compute_matrices();
}

void ShadowMap::update_settings(const Settings &settings) {
  settings_ = settings;
  std::cout << "[ShadowMap] Updating settings" << std::endl;
  std::cout << "  New resolution: " << settings_.resolution << std::endl;
  std::cout << "  New depth bias (constant/slope): "
            << settings_.depth_bias_constant << " / "
            << settings_.depth_bias_slope << std::endl;
  std::cout << "  New ortho size: " << settings_.ortho_size << std::endl;
  compute_matrices();
  rebuild_pass_desc();
}

void ShadowMap::begin(rhi::CmdList *cmd) {
  if (!initialized_) {
    std::cerr << "[ShadowMap] Cannot begin pass: not initialized" << std::endl;
    return;
  }

  if (!cmd) {
    std::cerr << "[ShadowMap] Cannot begin pass: command list is null"
              << std::endl;
    return;
  }

  std::cout << "[ShadowMap] Beginning shadow pass" << std::endl;
  pass_desc_.depthAttachment.clearDepth = 1.0f;
  cmd->beginRender(pass_desc_);
}

void ShadowMap::end(rhi::CmdList *cmd) {
  if (!initialized_) {
    std::cerr << "[ShadowMap] Cannot end pass: not initialized" << std::endl;
    return;
  }

  if (!cmd) {
    std::cerr << "[ShadowMap] Cannot end pass: command list is null" << std::endl;
    return;
  }

  std::cout << "[ShadowMap] Ending shadow pass" << std::endl;
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
  std::cout << "[ShadowMap] Rebuilding pass description" << std::endl;
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
  std::cout << "[ShadowMap] Computing matrices" << std::endl;
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
  std::cout << "  Ortho bounds: +/-" << ortho << std::endl;
  std::cout << "  Near/Far: " << settings_.near_plane << " / "
            << settings_.far_plane << std::endl;
  if (device_) {
    light_projection_ =
        clip_space_correction_matrix(device_->caps()) * light_projection_;
  }
  light_view_projection_ = light_projection_ * light_view_;
  std::cout << "[ShadowMap] light_view_projection matrix computed" << std::endl;
}

} // namespace pixel::renderer3d
