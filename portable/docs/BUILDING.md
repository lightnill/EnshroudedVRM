# Building EnshroudedVRM

Original code is MIT, Copyright (c) 2026 lightnill. Retain LICENSE and the
third-party notices. Run the following commands from the repository root.

## CMD/PowerShell launcher

The current launcher is readable source in `portable/script`: the CMD wrapper,
PowerShell entry point and `launcher_core.psm1`. No custom launcher EXE is built
or distributed. Windows PowerShell uses its installed .NET tooling to compile
a small read-only filesystem helper locally. Existing script policies are respected.

These files belong at the portable package root beside Runtime, Compatibility
and release-manifest files. A source checkout alone is not a playable package.

## Developer checks

Build the developer-only synthetic child fixture with a Windows x64 GCC compiler:

```powershell
./tests/build_script_fixture.ps1 -Compiler C:/Tools/w64devkit/bin/g++.exe `
  -OutputDirectory ./build/script-tests
python ./tests/test_script_launcher.py ./build/script-tests/portable_child.exe
python ./tests/test_portable_package.py
python ./tests/test_script_release.py
python ./tests/test_script_fixture_build.py
```

The child fixture is test-only and is never included in player downloads. Tests
use synthetic data and do not start Enshrouded. Python and a compiler are developer
tools, not player requirements.

## Native VRM runtime and shaders

The game requires a native Vulkan layer DLL. Use your own Windows x64 GCC toolchain
(tested with w64devkit/GCC 16.2.0), Vulkan-Headers and Microsoft DXC. No tools are
downloaded automatically.

```powershell
./build.ps1 -Target Bridge -PortableRelease `
  -CompilerDirectory C:/Tools/w64devkit/bin `
  -VulkanIncludeDirectory C:/Tools/Vulkan-Headers/include `
  -ShaderCompiler C:/Tools/dxc/bin/x64/dxc.exe `
  -OutputDirectory ./build/Runtime
```

The script release preserves the accepted runtime DLL and shaders unchanged.
Other development targets may depend on omitted research fixtures; only the
documented targets and selected synthetic tests are claimed to be self-contained.

## Assemble a release

Use a locally supplied, verified current package as the source for the supported
Runtime and Compatibility data. Supply its published SHA-256 explicitly:

```powershell
python ./portable/package_script_release.py `
  --base-package C:/Releases/accepted.zip `
  --base-sha256 PUBLISHED_64_CHARACTER_SHA256 `
  --output ./dist/EnshroudedVRM-0.1.0-preview-script.zip
```

The builder uses an explicit member map, carries the 17 runtime/compatibility
files unchanged, and regenerates package metadata, manifest and hashes. It does
not walk the workspace, include avatars/State, or download anything. Newly built
bytes need their own scan record; old reports do not validate a rebuilt archive.

Keep the compatibility data's provenance flags. Never bypass game fingerprints,
resource ownership or package hash checks. Preserve runtime license notices.
Published source hashes state their text normalization separately from binary
package hashes. Captures and model exports remain disabled.
