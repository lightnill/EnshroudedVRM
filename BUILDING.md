# Building EnshroudedVRM

Original code is MIT, copyright (c) 2026 lightnill. Keep LICENSE and all third-party
notices. No avatars, private fixtures, research tools or game executables are included.

Install your own Windows x64 GCC toolchain (tested with w64devkit/GCC 16.2.0),
Vulkan-Headers and Microsoft DXC. The build does not download or run tools automatically.

```powershell
./build.ps1 -Target Bridge -PortableRelease `
  -CompilerDirectory C:/Tools/w64devkit/bin `
  -VulkanIncludeDirectory C:/Tools/Vulkan-Headers/include `
  -ShaderCompiler C:/Tools/dxc/bin/x64/dxc.exe `
  -OutputDirectory ./build/Runtime

./portable/build.ps1 -CompilerDirectory C:/Tools/w64devkit/bin `
  -OutputDirectory ./build/Launcher -Test

python ./tests/test_portable_package.py
```

The runtime and launcher are normal unpacked PEs with ASLR/DEP and reproducible
PE timestamps. Release binaries are unsigned. No signing identity is included.
The launcher is statically linked; end users need no compiler, Python or PowerShell.

This curated source snapshot builds the Bridge runtime and launcher and includes
selected synthetic tests. Other development targets may depend on omitted local
research fixtures. Do not claim all development test targets are self-contained.

To run a rebuilt runtime, use the supported-build Compatibility files from the
matching binary release. These are minimal locally researched interface/rig data,
not model geometry. Their original provenance flags are retained. The MIT license
covers original code, not game content or avatars. Never bypass the executable,
resource ownership or file-hash checks to make an unsupported build run.

If replacing files in a local development package, regenerate the matching manifest
hashes deliberately. Published release hashes refer only to the distributed bytes.
GPU/model buffer captures are disabled by the portable launcher's environment.
Never submit private models, derived geometry, State or captures to source control.
