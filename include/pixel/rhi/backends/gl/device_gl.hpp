#pragma once

#include "pixel/rhi/rhi.hpp"

#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pixel::rhi::gl {

class GLQuery {
public:
  GLQuery();
  ~GLQuery();

  void begin_time_elapsed();
  void end_time_elapsed();
  bool is_result_available();
  uint64_t get_result();
  void timestamp();

private:
  GLuint query_id_{};
};

GLenum to_gl_compare(CompareOp op);
GLenum to_gl_stencil_op(StencilOp op);

struct GLBuffer {
  GLuint id = 0;
  GLenum target = GL_ARRAY_BUFFER;
  size_t size = 0;
  bool host_visible = false;
};

struct GLTexture {
  GLuint id = 0;
  GLenum target = GL_TEXTURE_2D;
  int width = 0;
  int height = 0;
  int layers = 1;
  Format format = Format::Unknown;
};

struct GLSampler {
  GLuint id = 0;
};

struct GLShader {
  GLuint shader_id = 0;
  GLenum shader_type = 0;
  std::string stage;
};

struct GLPipeline {
  GLuint program = 0;
  GLuint vao = 0;
  ShaderHandle vs{0};
  ShaderHandle fs{0};
  ShaderHandle cs{0};
  std::unordered_map<std::string, GLint> uniform_locations;
};

struct GLFramebuffer {
  GLuint id = 0;
  FramebufferDesc desc{};
  int width = 0;
  int height = 0;
  std::vector<GLenum> draw_buffers;
};

struct GLQueryObject {
  QueryType type{QueryType::TimeElapsed};
  std::unique_ptr<GLQuery> query;
};

struct GLFence {
  GLsync sync{nullptr};
  bool signaled{false};
};

class GLCmdList : public CmdList {
public:
  GLCmdList(std::unordered_map<uint32_t, GLBuffer> *buffers,
            std::unordered_map<uint32_t, GLTexture> *textures,
            std::unordered_map<uint32_t, GLPipeline> *pipelines,
            std::unordered_map<uint32_t, GLFramebuffer> *framebuffers,
            std::unordered_map<uint32_t, GLQueryObject> *queries,
            std::unordered_map<uint32_t, GLFence> *fences, GLFWwindow *window);

  void begin() override;
  void beginRender(const RenderPassDesc &desc) override;
  void setPipeline(PipelineHandle) override;
  void setVertexBuffer(BufferHandle, size_t offset = 0) override;
  void setIndexBuffer(BufferHandle, size_t offset = 0) override;
  void setInstanceBuffer(BufferHandle, size_t stride,
                         size_t offset = 0) override;
  void setDepthStencilState(const DepthStencilState &state) override;
  void setDepthBias(const DepthBiasState &state) override;
  void setUniformMat4(const char *name, const float *mat4x4) override;
  void setUniformVec3(const char *name, const float *vec3) override;
  void setUniformVec4(const char *name, const float *vec4) override;
  void setUniformInt(const char *name, int value) override;
  void setUniformFloat(const char *name, float value) override;
  void setUniformBuffer(uint32_t binding, BufferHandle buffer,
                        size_t offset = 0, size_t size = 0) override;
  void setTexture(const char *name, TextureHandle texture,
                  uint32_t slot = 0) override;
  void copyToTexture(TextureHandle texture, uint32_t mipLevel,
                     std::span<const std::byte> data) override;
  void copyToTextureLayer(TextureHandle texture, uint32_t layer,
                          uint32_t mipLevel,
                          std::span<const std::byte> data) override;
  void setComputePipeline(PipelineHandle) override;
  void setStorageBuffer(uint32_t binding, BufferHandle buffer,
                        size_t offset = 0, size_t size = 0) override;
  void dispatch(uint32_t groupCountX, uint32_t groupCountY = 1,
                uint32_t groupCountZ = 1) override;
  void memoryBarrier() override;
  void resourceBarrier(std::span<const ResourceBarrierDesc> barriers) override;
  void beginQuery(QueryHandle handle, QueryType type) override;
  void endQuery(QueryHandle handle, QueryType type) override;
  void signalFence(FenceHandle handle) override;
  void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0,
                   uint32_t instanceCount = 1) override;
  void endRender() override;
  void copyToBuffer(BufferHandle, size_t dstOff,
                    std::span<const std::byte> src) override;
  void end() override;

private:
  GLint getUniformLocation(const char *name);

  std::unordered_map<uint32_t, GLBuffer> *buffers_;
  std::unordered_map<uint32_t, GLTexture> *textures_;
  std::unordered_map<uint32_t, GLPipeline> *pipelines_;
  std::unordered_map<uint32_t, GLFramebuffer> *framebuffers_;
  std::unordered_map<uint32_t, GLQueryObject> *queries_;
  std::unordered_map<uint32_t, GLFence> *fences_;
  GLFWwindow *window_ = nullptr;

  bool recording_ = false;
  PipelineHandle current_pipeline_{0};
  BufferHandle current_vb_{0};
  BufferHandle current_ib_{0};
  size_t current_vb_offset_ = 0;
  size_t current_ib_offset_ = 0;
  GLuint current_fbo_ = 0;
  bool using_offscreen_fbo_ = false;
  bool current_fbo_owned_ = false;
  DepthStencilState current_depth_stencil_state_{};
  DepthBiasState current_depth_bias_state_{};
  bool depth_stencil_state_initialized_ = false;
  bool depth_bias_initialized_ = false;
};

class GLDevice : public Device {
public:
  explicit GLDevice(GLFWwindow *window);
  ~GLDevice() override;

  const char *backend_name() const override;
  const Caps &caps() const override;

  BufferHandle createBuffer(const BufferDesc &desc) override;
  TextureHandle createTexture(const TextureDesc &desc) override;
  SamplerHandle createSampler(const SamplerDesc &desc) override;
  ShaderHandle createShader(std::string_view stage,
                            std::span<const uint8_t> bytes) override;
  PipelineHandle createPipeline(const PipelineDesc &desc) override;
  FramebufferHandle createFramebuffer(const FramebufferDesc &desc) override;
  QueryHandle createQuery(QueryType type) override;
  void destroyQuery(QueryHandle handle) override;
  bool getQueryResult(QueryHandle handle, uint64_t &result, bool wait) override;
  FenceHandle createFence(bool signaled = false) override;
  void destroyFence(FenceHandle handle) override;
  void waitFence(FenceHandle handle, uint64_t timeout_ns = ~0ull) override;
  void resetFence(FenceHandle handle) override;
  void readBuffer(BufferHandle handle, void *dst, size_t size,
                  size_t offset = 0) override;

  CmdList *getImmediate() override;
  void present() override;

private:
  GLFWwindow *window_;
  Caps caps_;

  std::unique_ptr<GLCmdList> cmd_list_;

  std::unordered_map<uint32_t, GLBuffer> buffers_;
  std::unordered_map<uint32_t, GLTexture> textures_;
  std::unordered_map<uint32_t, GLSampler> samplers_;
  std::unordered_map<uint32_t, GLShader> shaders_;
  std::unordered_map<uint32_t, GLPipeline> pipelines_;
  std::unordered_map<uint32_t, GLFramebuffer> framebuffers_;
  std::unordered_map<uint32_t, GLQueryObject> queries_;
  std::unordered_map<uint32_t, GLFence> fences_;

  std::string backend_name_;
  uint32_t next_buffer_id_ = 1;
  uint32_t next_texture_id_ = 1;
  uint32_t next_sampler_id_ = 1;
  uint32_t next_shader_id_ = 1;
  uint32_t next_pipeline_id_ = 1;
  uint32_t next_framebuffer_id_ = 1;
  uint32_t next_query_id_ = 1;
  uint32_t next_fence_id_ = 1;
};

} // namespace pixel::rhi::gl

namespace pixel::rhi {
Device *create_gl_device(GLFWwindow *window);
}
