# Building EnshroudedVRM

Original code is MIT, copyright (c) 2026 lightnill. Keep LICENSE and all third-party
notices. No avatars, private fixtures, research tools or game executables are included.

## Current CMD/PowerShell launcher

The current launcher is readable source in `portable/script`:

- `Launch-EnshroudedVRM.cmd`
- `Launch-EnshroudedVRM.ps1`
- `launcher_core.psm1`

No custom launcher EXE is built or distributed in the current package. Windows
PowerShell 5.1 uses its installed .NET tooling to compile a small read-only path
helper locally. Existing script policies are respected.

The launcher files belong at the portable package root, beside the required
Runtime, Compatibility and release-manifest files. A source checkout alone is
not a complete playable package. Use the published release for the supported
compatibility data and exact file hashes. See
[launcher instructions](portable/docs/SCRIPT-LAUNCHER.txt).

Developer regression tests use an existing synthetic child-process fixture:

```powershell
python ./tests/test_script_launcher.py C:/BuildFixtures/portable_child.exe
python ./tests/test_portable_package.py
```

The child fixture is built from `tests/portable_child.cpp` with a Windows x64 C++
compiler. These tests create synthetic files and child processes, not a game
session. Python and the fixture compiler are developer tools, not player requirements.

## Native VRM runtime and shaders

The game still requires the native Vulkan layer DLL. Install your own Windows
x64 GCC toolchain (tested with w64devkit/GCC 16.2.0),
Vulkan-Headers and Microsoft DXC. The build does not download or run tools automatically.

```powershell
./build.ps1 -Target Bridge -PortableRelease `
  -CompilerDirectory C:/Tools/w64devkit/bin `
  -VulkanIncludeDirectory C:/Tools/Vulkan-Headers/include `
  -ShaderCompiler C:/Tools/dxc/bin/x64/dxc.exe `
  -OutputDirectory ./build/Runtime

python ./tests/test_portable_package.py
```

The current script release carries the accepted runtime and shaders unchanged;
changing the launcher did not require rebuilding them.

## Legacy launcher EXE builders

Earlier native launcher source and build scripts remain for historical
inspection and reuse under their licenses. They are not the current distribution.
Previous binaries remain in the release history.

```powershell
./portable/build.ps1 -CompilerDirectory C:/Tools/w64devkit/bin `
  -OutputDirectory ./build/Launcher -Test
```

The legacy launcher can also be built separately with Microsoft Visual C++ x64 and a
Windows SDK. This does not rebuild the VRM runtime or its shaders. Tested with
MSVC 19.27.29112 (Visual Studio 2019) and Windows SDK 10.0.19041.0:

```powershell
./portable/build-msvc.ps1 `
  -VcToolsDirectory 'C:/Tools/MSVC/14.27.29110' `
  -WindowsSdkIncludeDirectory 'C:/Tools/WindowsSDK/Include/10.0.19041.0' `
  -WindowsSdkLibraryDirectory 'C:/Tools/WindowsSDK/Lib/10.0.19041.0' `
  -ResourceCompiler 'C:/Tools/WindowsSDK/bin/10.0.19041.0/x64/rc.exe' `
  -OutputDirectory ./build/Launcher-MSVC -Python python -Test
```

Use the actual paths from your licensed Visual Studio/Build Tools and SDK
installation. The SDK include directory must contain `ucrt`, `shared` and `um`;
the library directory must contain `ucrt/x64` and `um/x64`. The script changes
only its process environment and downloads no dependencies. `-Test` runs the
core checks and four synthetic child-process tests; it never starts the game.
Python is needed only for those native process tests.

The MSVC launcher uses the release static CRT (`/MT`, `/DNDEBUG`), ASLR, DEP,
Control Flow Guard and reproducible linking (`/Brepro`). Core test assertions
remain enabled. No separate Visual C++ Redistributable is required by this
launcher; its imports are Windows system DLLs. Keep the Microsoft notices when
distributing it. The accepted runtime DLL still uses GCC and retains its notices.

The runtime and launcher are normal unpacked PEs with ASLR/DEP and reproducible
PE timestamps. Release binaries are unsigned. No signing identity is included.
The launcher is statically linked; end users need no compiler, Python or PowerShell.

This curated source snapshot builds the Bridge runtime and launcher and includes
selected synthetic tests. Other development targets may depend on omitted local
research fixtures. Do not claim all development test targets are self-contained.

## Runtime package integrity

To run a rebuilt runtime, use the supported-build Compatibility files from the
matching binary release. These are minimal locally researched interface/rig data,
not model geometry. Their original provenance flags are retained. The MIT license
covers original code, not game content or avatars. Never bypass the executable,
resource ownership or file-hash checks to make an unsupported build run.

If replacing files in a local development package, regenerate the matching manifest
hashes deliberately. Published release hashes refer only to the distributed bytes.
GPU/model buffer captures are disabled by the portable launcher's environment.
Never submit private models, derived geometry, State or captures to source control.
