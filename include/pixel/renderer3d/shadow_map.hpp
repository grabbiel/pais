#pragma once

#include "pixel/renderer3d/types.hpp"
#include "pixel/rhi/rhi.hpp"
#include <glm/glm.hpp>

namespace pixel::renderer3d {

struct DirectionalLight {
  Vec3 direction{-0.2f, -1.0f, -0.3f};
  Vec3 position{10.0f, 10.0f, 10.0f};
  Color color = Color::White();
  float intensity{1.0f};
};

class ShadowMap {
public:
  struct Settings {
    uint32_t resolution{2048};
    float near_plane{1.0f};
    float far_plane{100.0f};
    float ortho_size{25.0f};
    float depth_bias_constant{1.5f};
    float depth_bias_slope{1.0f};
    float shadow_bias{0.005f};
  };

  ShadowMap() = default;

  bool initialize(rhi::Device *device, const Settings &settings,
                  const DirectionalLight &light);

  void update_light(const DirectionalLight &light);
  void update_settings(const Settings &settings);

  void begin(rhi::CmdList *cmd);
  void end(rhi::CmdList *cmd);

  const glm::mat4 &light_view() const { return light_view_; }
  const glm::mat4 &light_projection() const { return light_projection_; }
  const glm::mat4 &light_view_projection() const { return light_view_projection_; }

  rhi::TextureHandle texture() const { return depth_texture_; }
  rhi::FramebufferHandle framebuffer() const { return framebuffer_; }
  rhi::RenderPassDesc render_pass_desc() const { return pass_desc_; }
  rhi::SamplerHandle sampler() const { return sampler_; }

  const Settings &settings() const { return settings_; }
  const DirectionalLight &light() const { return light_; }

  rhi::DepthBiasState depth_bias_state() const;

private:
  void rebuild_pass_desc();
  void compute_matrices();

  rhi::Device *device_{nullptr};
  Settings settings_{};
  DirectionalLight light_{};

  glm::mat4 light_view_{1.0f};
  glm::mat4 light_projection_{1.0f};
  glm::mat4 light_view_projection_{1.0f};

  rhi::TextureHandle depth_texture_{};
  rhi::FramebufferHandle framebuffer_{};
  rhi::SamplerHandle sampler_{};
  rhi::RenderPassDesc pass_desc_{};

  bool initialized_{false};
  bool depth_initialized_{false};
  bool depth_ready_for_sampling_{false};
};

} // namespace pixel::renderer3d
