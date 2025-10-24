// src/rhi/backends/dx12/device_dx12.cpp
// Placeholder implementation for the DirectX 12 backend. The actual backend
// will be implemented on Windows. This file simply ensures that the build
// system is wired for DX12 development without affecting other platforms.
#include "pixel/rhi/rhi.hpp"

#include <stdexcept>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <d3d12.h>
#  include <dxgi1_6.h>
#  include <wrl/client.h>

// The DirectX 12 headers require linking against system libraries. The
// corresponding link libraries are provided through CMake when
// PIXEL_USE_DX12=ON.

namespace pixel::rhi {

Device *create_dx12_device(void *window_handle) {
  (void)window_handle;

  throw std::runtime_error(
      "DirectX 12 backend scaffolding is in place, but the runtime "
      "implementation has not been provided yet.");
}

} // namespace pixel::rhi

#else

namespace pixel::rhi {

Device *create_dx12_device(void *window_handle) {
  (void)window_handle;
  throw std::runtime_error(
      "DirectX 12 backend is only supported on Windows builds.");
}

} // namespace pixel::rhi

#endif
