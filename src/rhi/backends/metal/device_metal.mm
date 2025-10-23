// src/rhi/backends/metal/device_metal.mm
// Metal Device Implementation
#ifdef __APPLE__

#include "metal_internal.hpp"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

namespace pixel::rhi {

MetalDevice::MetalDevice(void *device, void *layer, void *depth_texture)
    : impl_(std::make_unique<Impl>((__bridge id<MTLDevice>)device,
                                   (__bridge CAMetalLayer *)layer,
                                   (__bridge id<MTLTexture>)depth_texture)) {}

MetalDevice::~MetalDevice() = default;

const Caps &MetalDevice::caps() const {
  static Caps caps;
  caps.instancing = true;
  caps.samplerAniso = true;
  caps.maxSamplerAnisotropy = 16.0f;
  caps.samplerCompare = true;
  caps.uniformBuffers = true;
  caps.clipSpaceYDown = true; // Metal uses Y-down clip space
  return caps;
}

CmdList *MetalDevice::getImmediate() { return impl_->immediate_.get(); }

void MetalDevice::present() {
  // Present is handled by the command list
}

Device *create_metal_device(void *window) {
  GLFWwindow *glfwWindow = static_cast<GLFWwindow *>(window);
  NSWindow *nsWindow = glfwGetCocoaWindow(glfwWindow);

  if (!nsWindow) {
    std::cerr << "Failed to get Cocoa window" << std::endl;
    return nullptr;
  }

  // Create Metal device
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (!device) {
    std::cerr << "Metal is not supported on this device" << std::endl;
    return nullptr;
  }

  std::cout << "Metal device: " << [[device name] UTF8String] << std::endl;

  // Create Metal layer
  CAMetalLayer *metalLayer = [CAMetalLayer layer];
  metalLayer.device = device;
  metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metalLayer.framebufferOnly = NO;

  // Set layer on the window
  nsWindow.contentView.layer = metalLayer;
  nsWindow.contentView.wantsLayer = YES;

  // Get window size
  int width, height;
  glfwGetFramebufferSize(glfwWindow, &width, &height);
  metalLayer.drawableSize = CGSizeMake(width, height);

  // Create depth texture
  MTLTextureDescriptor *depthDesc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                   width:width
                                  height:height
                               mipmapped:NO];
  depthDesc.usage = MTLTextureUsageRenderTarget;
  depthDesc.storageMode = MTLStorageModePrivate;

  id<MTLTexture> depthTexture = [device newTextureWithDescriptor:depthDesc];

  if (!depthTexture) {
    std::cerr << "Failed to create depth texture" << std::endl;
    return nullptr;
  }

  // Create and return device (bridging for C++ ownership)
  return new MetalDevice((__bridge void *)device, (__bridge void *)metalLayer,
                         (__bridge void *)depthTexture);
}

} // namespace pixel::rhi

#endif // __APPLE__
