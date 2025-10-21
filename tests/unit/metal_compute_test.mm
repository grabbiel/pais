#ifdef __APPLE__
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "pixel/rhi/rhi.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

using namespace pixel::rhi;

int main() {
  if (!glfwInit()) {
    return 1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window =
      glfwCreateWindow(128, 128, "MetalComputeTest", nullptr, nullptr);
  assert(window != nullptr);

  Device *device = create_metal_device(window);
  assert(device != nullptr);

  constexpr uint32_t kElementCount = 128;
  std::vector<uint32_t> input(kElementCount);
  for (uint32_t i = 0; i < kElementCount; ++i) {
    input[i] = i + 1u;
  }

  std::vector<uint32_t> accumInit(kElementCount, 1u);

  BufferDesc storageDesc{input.size() * sizeof(uint32_t), BufferUsage::Storage,
                         true};
  BufferHandle inputBuffer = device->createBuffer(storageDesc);
  BufferHandle outputBuffer = device->createBuffer(storageDesc);
  BufferHandle accumBuffer = device->createBuffer(storageDesc);

  BufferDesc countDesc{sizeof(uint32_t), BufferUsage::Uniform, true};
  BufferHandle countBuffer = device->createBuffer(countDesc);

  ShaderHandle computeShader = device->createShader("cs_test", {});
  PipelineDesc pipelineDesc{};
  pipelineDesc.cs = computeShader;
  PipelineHandle pipeline = device->createPipeline(pipelineDesc);

  CmdList *cmd = device->getImmediate();
  cmd->begin();

  std::span<const uint32_t> inputSpan(input);
  cmd->copyToBuffer(inputBuffer, 0, std::as_bytes(inputSpan));

  std::span<const uint32_t> accumSpan(accumInit);
  cmd->copyToBuffer(accumBuffer, 0, std::as_bytes(accumSpan));

  uint32_t totalElements = kElementCount;
  std::span<const uint32_t> countSpan(&totalElements, 1);
  cmd->copyToBuffer(countBuffer, 0, std::as_bytes(countSpan));

  float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  cmd->beginRender(TextureHandle{}, TextureHandle{}, clearColor, 1.0f, 0);
  cmd->setComputePipeline(pipeline);
  cmd->setStorageBuffer(0, inputBuffer);
  cmd->setStorageBuffer(1, outputBuffer);
  cmd->setStorageBuffer(2, accumBuffer);
  cmd->setStorageBuffer(3, countBuffer);

  cmd->dispatch(kElementCount, 1, 1);
  cmd->memoryBarrier();
  cmd->dispatch(kElementCount, 1, 1);
  cmd->memoryBarrier();

  cmd->endRender();
  cmd->end();

  std::vector<uint32_t> output(kElementCount);
  device->readBuffer(outputBuffer, output.data(),
                     output.size() * sizeof(uint32_t));

  std::vector<uint32_t> accum(kElementCount);
  device->readBuffer(accumBuffer, accum.data(),
                     accum.size() * sizeof(uint32_t));

  for (size_t i = 0; i < output.size(); ++i) {
    assert(output[i] == input[i] * 2u);
    assert(accum[i] == accumInit[i] + input[i] * 2u);
  }

  // Verify we can return to a render pass after compute work
  cmd->begin();
  cmd->beginRender(TextureHandle{}, TextureHandle{}, clearColor, 1.0f, 0);
  cmd->endRender();
  cmd->end();

  delete device;
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

#else
int main() { return 0; }
#endif
