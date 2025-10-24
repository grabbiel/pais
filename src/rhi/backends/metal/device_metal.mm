// src/rhi/backends/metal/device_metal.mm
// Metal Device Implementation
#ifdef __APPLE__

#include "pixel/rhi/backends/metal/metal_internal.hpp"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#include <cstring>

namespace pixel::rhi {

MetalDevice::MetalDevice(void *device, void *layer, void *depth_texture,
                         void *window)
    : impl_(std::make_unique<Impl>((__bridge id<MTLDevice>)device,
                                   (__bridge CAMetalLayer *)layer,
                                   (__bridge id<MTLTexture>)depth_texture,
                                   static_cast<GLFWwindow *>(window))) {
  id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
  if (metalDevice) {
    NSString *name = [metalDevice name];
    if (name) {
      const char *utf8Name = [name UTF8String];
      backend_name_ = "Metal";
      if (utf8Name && std::strlen(utf8Name) > 0) {
        backend_name_ += " (";
        backend_name_ += utf8Name;
        backend_name_ += ")";
      }
    }
  }

  if (backend_name_.empty()) {
    backend_name_ = "Metal";
  }
}

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

const char *MetalDevice::backend_name() const { return backend_name_.c_str(); }

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

  // Query (or create) the CAMetalLayer associated with this GLFW window
  CAMetalLayer *metalLayer = nil;
  if ([nsWindow.contentView.layer isKindOfClass:[CAMetalLayer class]]) {
    metalLayer = (CAMetalLayer *)nsWindow.contentView.layer;
  }

  if (!metalLayer) {
    metalLayer = [CAMetalLayer layer];
    nsWindow.contentView.wantsLayer = YES;
    nsWindow.contentView.layer = metalLayer;
  }

  metalLayer.device = device;
  metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metalLayer.framebufferOnly = NO;

  CGFloat backingScale = 1.0;
  if (nsWindow.screen) {
    backingScale = nsWindow.screen.backingScaleFactor;
  } else if ([NSScreen mainScreen]) {
    backingScale = [NSScreen mainScreen].backingScaleFactor;
  }
  if (backingScale <= 0.0) {
    backingScale = 1.0;
  }

  metalLayer.contentsScale = backingScale;
  metalLayer.maximumDrawableCount = 3;
  metalLayer.allowsNextDrawableTimeout = NO;

  // Get window size
  int width, height;
  glfwGetFramebufferSize(glfwWindow, &width, &height);
  if (width <= 0 || height <= 0) {
    NSRect bounds = nsWindow.contentView.bounds;
    width = static_cast<int>(bounds.size.width * backingScale);
    height = static_cast<int>(bounds.size.height * backingScale);
  }

  if (width <= 0 || height <= 0) {
    std::cerr << "Metal layer drawable size is zero (" << width << "x" << height
              << ")" << std::endl;
    width = 1;
    height = 1;
  }

  metalLayer.drawableSize = CGSizeMake(width, height);
  std::cout << "Metal layer drawable size set to " << width << "x" << height
            << " (scale " << backingScale << ")" << std::endl;

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
                         (__bridge void *)depthTexture,
                         static_cast<void *>(glfwWindow));
}

} // namespace pixel::rhi

#endif // __APPLE__
