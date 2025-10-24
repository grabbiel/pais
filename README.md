# pais
Life 2D Simulation MMO

## Graphics backend development

- Metal (macOS) remains the default backend on Apple hardware.
- A DirectX 12 backend is under development for Windows. To opt in, configure
  CMake with `-DPIXEL_USE_DX12=ON` on a Windows machine. Detailed setup
  instructions are available in [docs/dx12_backend_setup.md](docs/dx12_backend_setup.md).
