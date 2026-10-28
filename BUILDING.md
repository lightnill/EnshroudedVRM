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
After a successful configured launch the player-local Play-EnshroudedVRM.cmd is
generated beside them. It runs the same entry point with -Play and reads only
saved settings. Do not include generated Play or private State in release inputs.

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
The launcher suite runs generated CMD files against that child, including saved
settings, relocation, metacharacters, validation failures and output collisions.
Adjacent stand-in loader files check preservation only; live EML/ShroudForge
compatibility requires separate gameplay testing.
Startup-history tests scale only the private observer's time window, use the
unchanged synthetic child, and exercise bounded tails, early/nonzero exits,
retention, stale logs and linked-folder protection without launching Enshrouded.

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

The 0.1.1 runtime rebuild applies the standard VRoid FaceBrow compatibility rule.
The existing shaders are unchanged. Run the material import regression with:

```powershell
$env:EVRM_TEST_COMPILER = 'C:/Tools/w64devkit/bin/g++.exe'
python ./tests/test_material_compatibility.py
```
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

## Package a separately rebuilt runtime

The release builder accepts an explicitly supplied DLL and digest. It does not
compile or test that DLL. Build and verify it first, then use:

```powershell
python ./portable/package_script_release.py `
  --base-package C:/Releases/accepted.zip `
  --base-sha256 PUBLISHED_BASE_SHA256 `
  --runtime-dll ./build/Runtime/enshrouded_vrm_runtime.dll `
  --runtime-sha256 VERIFIED_REBUILT_DLL_SHA256 `
  --version 0.1.1-preview `
  --output ./dist/EnshroudedVRM-0.1.1-preview.zip
```

The 16 other runtime/shader/compatibility members remain hash-verified against
the base package. Runtime source pins are refreshed from the supplied source
checkout. The source files must match the verified build; the builder's source
hashes are not by themselves proof that a DLL was built from those sources.
Private State, saves, avatars, captures and test executables are excluded.
