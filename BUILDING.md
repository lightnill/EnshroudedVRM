# Build EnshroudedVRM 0.6.1 on Windows x64

This branch contains the production C++ and HLSL for the input loader, the Vulkan avatar renderer, its F8 menu and its shaders. The third-party licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Inputs

- Visual Studio 2022 with x64 MSVC v143 toolset 14.44.35207 (compiler 19.44.35229.0) and Windows SDK 10.0.28000.0.
- Python 3.11 or newer and PowerShell 7.
- The Vulkan-Headers include directory and a DXC build with SPIR-V support. The release used a `vulkan_core.h` with SHA-256 `4b685532492d1cec371141e1da0018fcddf84916dc2bee8c5254bfb98509f1d5` and a `dxc.exe` with SHA-256 `aa7965b3763d9c710baaa5a28cedcc633449962a75d1391d161a85304a41b3e2`.
- The original [0.6.1 player ZIP](https://github.com/lightnill/EnshroudedVRM/releases/tag/v0.6.1), SHA-256 `06545849134347cb3cef05e15d1723a96177a8000f6641970f892179d375b70e`. The build reads the package inventory and the licensed Oculus LipSync library from this archive. The project's loader, renderer and shaders compile locally from source. See [the SDK provenance](vendor/oculus-lipsync/PROVENANCE.json) for the upstream library and its terms.

## Build

Run from a checkout on Windows. Substitute the local paths to the ZIP, headers and compiler. Set `EVRM_PYTHON` if your preferred Python executable is outside your PATH.

```powershell
.\build.ps1 -ReleaseZip 'D:\Downloads\EnshroudedVRM-0.6.1.zip' -VulkanIncludeDirectory 'D:\Vulkan-Headers\include' -ShaderCompiler 'D:\DXC\bin\x64\dxc.exe' -OutputDirectory '.\build'
```

The resulting files include `build\bootstrap\dinput8.dll`, `build\enshrouded_vrm_runtime.dll` and the SPIR-V modules in `build\shaders`. The release notes cover installation and supported game builds. Use the published ZIP for a complete player installation.
