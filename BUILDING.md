# Building EnshroudedVRM 0.3.0

Players use the release ZIP and normal Steam startup. These instructions are for developers.

## Dependencies and exact inputs

Use Windows x64, a Windows SDK, Microsoft x64 C++ tools for the small bootstrap, the existing Windows x64 MinGW/GCC toolchain for the native runtime, Vulkan-Headers, Microsoft DXC, Python and PowerShell 7 as the developer build host. The validated bootstrap compiler is MSVC 19.27.29112 (v142); the runtime uses GCC 16.2.0. Other toolchain/platform combinations need their own validation.

Run from a Microsoft developer environment with SDK INCLUDE/LIB configured and rc.exe on PATH, or set EVRM_BOOTSTRAP_RESOURCE_COMPILER to its full path. Source bytes must remain unchanged; .gitattributes disables line-ending conversion so upstream source pins remain reproducible.

Acquire the official Oculus LipSync input separately according to vendor/oculus-lipsync/PROVENANCE.json and its supplied license terms. The SDK DLL is excluded from GitHub source, is required at vendor/oculus-lipsync/Win64/OVRLipSync.dll for the build, and is distributed unmodified in the player ZIP. Do not substitute an unverified library.

The compatibility baseline is the published EnshroudedVRM-0.2.0-docs1.zip:
https://github.com/lightnill/EnshroudedVRM/releases/download/v0.2.0/EnshroudedVRM-0.2.0-docs1.zip

Required SHA-256: 6905577b1e5afeb48659de6b7875ec6b449a625175184cd887aebc3b61483a26

The baseline supplies checked compatibility/shader/vendor files. The release and runtime are 0.3.0; the bootstrap retains its independent 0.2.0-standalone-dev version. The selected release candidate reuses the exact previously tested bootstrap DLL (SHA-256 d0e5c7f7db86f5e7a5b0bb69780da746d54ade3837eb026e2b23d32fba231cef) and the rebuilt 0.3.0 runtime with the importer fix (SHA-256 9c63410651703633268648fe346d435dc3a43040a82dc00c0c91a12be1473524). The old base remains intentionally pinned. Dependencies are not downloaded automatically.

## Build and freeze

Run the following as one command, using absolute paths for your toolchain and baseline:

```powershell
python portable/build_standalone.py --compiler-directory C:/Tools/w64devkit/bin --bootstrap-compiler-directory C:/Tools/MSVC/bin/Hostx64/x64 --vulkan-include-directory C:/Tools/Vulkan-Headers/include --shader-compiler C:/Tools/dxc/bin/x64/dxc.exe --powershell C:/Tools/PowerShell/pwsh.exe --base-package C:/Releases/EnshroudedVRM-0.2.0-docs1.zip --output-directory C:/Builds/evrm-030 --version 0.3.0 --publication-status preview
```

The command above performs fresh native builds for development. A new compiler/linker can produce different bootstrap bytes even when its source and version are unchanged. To package retained binaries, use portable/package_standalone_release.py with explicit --bootstrap-dll/--bootstrap-sha256 and --runtime-dll/--runtime-sha256 inputs, a complete current source-pin file, and the accepted shader manifest. Do not change the retained DLL version resource. BUILD-INFO.json records nativeMetadataVersion for the runtime and bootstrapMetadataVersion separately.

The output directory must be fresh. The helper builds the runtime twice, builds the bootstrap twice, checks byte identity, verifies all source/shader/asset pins and produces two identical ZIPs. It records exact tool hashes and build times. It never installs the mod or launches Enshrouded. This preview targets the validated C:\Windows system-loader location; other Windows-directory layouts are refused.

A build receipt establishes reproducible bytes and provenance, not successful tests or antivirus clearance. Keep exact-hash test/scan evidence separate from the ZIP. Do not load changed distributable DLLs before the required scans.

The single included sample is explicitly hash/size-pinned. Personal models, game files, captures, private State/logs and audio are not source/release inputs. See portable/standalone_assets/README.txt for sample attribution and permissions.

## Tests

Acquire the external public speech fixture described in [tests/fixtures/README.md](tests/fixtures/README.md) before audio tests. Its bytes are not redistributed by request; the tests verify its pinned hash.

Run every tests/test_*.py module with the Windows compiler, Vulkan headers and DXC supplied through EVRM_TEST_COMPILER, EVRM_FIXTURE_COMPILER, EVRM_VULKAN_INCLUDE_DIRECTORY and EVRM_SHADER_COMPILER. Bootstrap tests additionally use EVRM_TEST_BOOTSTRAP_COMPILER_DIRECTORY, EVRM_APPROVED_BOOTSTRAP_DIRECTORY and the exact scanned EVRM_APPROVED_BOOTSTRAP_SHA256. Exact-runtime checks take EVRM_RUNTIME_TEST_DIRECTORY and an explicitly provided supported game executable for read-only attestation.

The retained script-launcher tests require their owned portable_child.exe fixture, built with tests/build_script_fixture.ps1 or test_script_fixture_build.py. They test the legacy source only; no script launcher is included in the 0.3.0 player ZIP. Suppress Windows error dialogs in the test parent with SetErrorMode(0x8003) for intentional fault cases. Fixtures must not launch the real game or capture microphone input.

The verified feature build covered 31 modules/201 methods: 200 passed and one Windows symlink-permission skip. Its startup test adapter was corrected and that complete eight-method module rerun; the original failure is retained in the evidence. Release metadata/package/forwarding checks are recorded separately for the exact 0.3.0 bytes. Manual Steam/F8/model/toggle/restart checks passed on the desktop; laptop validation remains pending.

## Legacy tooling

portable/script, portable/docs and package_script_release.py remain for historical source/tests. They describe older releases and are not the 0.3.0 installation or startup path. Research-only build.ps1 targets may reference omitted research fixtures; the supported release build is the Bridge route used by build_standalone.py.
