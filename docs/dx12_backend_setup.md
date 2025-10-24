# DirectX 12 Backend Development Setup

The DirectX 12 backend is currently under active development. The build system
now exposes an opt-in CMake switch so Windows developers can iterate on the
backend without impacting other platforms or existing presets.

## Prerequisites

- Windows 10 or newer with the Windows SDK installed.
- Visual Studio 2022 (with the "Desktop development with C++" workload) or the
  Visual Studio Build Tools and Ninja generator.
- CMake 3.24 or newer (matches the project minimum).

## Configure a dedicated build directory

Use a separate build directory so the DX12 configuration does not interfere
with other presets:

```powershell
cmake -S . -B build/dx12-win \
  -G "Visual Studio 17 2022" \
  -DPIXEL_USE_DX12=ON
```

You can replace the generator with `-G Ninja` if you prefer Ninja builds. The
important flag is `-DPIXEL_USE_DX12=ON`, which enables the new backend and
ensures the correct Windows libraries are linked.

## Build the project

```powershell
cmake --build build/dx12-win --config Debug
```

The runtime implementation currently throws a `std::runtime_error` at device
creation time. This is intentional scaffolding: the backend code will be
implemented on Windows as part of upcoming work.

## Keeping other platforms unaffected

- The DX12 backend is disabled by default. macOS builds continue to use the
  Metal backend, and Linux builds will error out unless another backend is
  selected (as before).
- CMake prevents enabling both Metal and DirectX 12 simultaneously.
- Existing presets (`cmake --preset ...`) remain unchanged because they do not
  set `PIXEL_USE_DX12`.

## Next steps

Implement the DirectX 12 device and command list inside
`src/rhi/backends/dx12/device_dx12.cpp`, removing the placeholder exception and
adding the necessary resource management code.
