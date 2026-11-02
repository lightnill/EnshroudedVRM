# Building EnshroudedVRM 0.3.3

Players use the release ZIP and normal Steam startup. These instructions are for developers.

## Dependencies and exact inputs

Use Windows x64, a Windows SDK, Microsoft x64 C++ tools for the small bootstrap, the existing Windows x64 MinGW/GCC toolchain for the native runtime, Vulkan-Headers, Microsoft DXC, Python and PowerShell 7 as the developer build host. The validated bootstrap compiler is MSVC 19.27.29112 (v142); the runtime uses GCC 16.2.0. Other toolchain/platform combinations need their own validation.

Run from a Microsoft developer environment with SDK INCLUDE/LIB configured and rc.exe on PATH, or set EVRM_BOOTSTRAP_RESOURCE_COMPILER to its full path. Source bytes must remain unchanged; .gitattributes disables line-ending conversion so upstream source pins remain reproducible.

Acquire the official Oculus LipSync input separately according to vendor/oculus-lipsync/PROVENANCE.json and its supplied license terms. The SDK DLL is excluded from GitHub source, is required at vendor/oculus-lipsync/Win64/OVRLipSync.dll for the build, and is distributed unmodified in the player ZIP. Do not substitute an unverified library.

The compatibility baseline is the published EnshroudedVRM-0.2.0-docs1.zip:
https://github.com/lightnill/EnshroudedVRM/releases/download/v0.2.0/EnshroudedVRM-0.2.0-docs1.zip

Required SHA-256: 6905577b1e5afeb48659de6b7875ec6b449a625175184cd887aebc3b61483a26

The baseline supplies checked compatibility/vendor files and the accepted shader set. The packed fog snapshot and debug-report capture module have explicit checked hashes. Release/runtime version is 0.3.3. The bootstrap is retained byte-identically with independent 0.2.0-standalone-dev metadata and SHA-256 d0e5c7f7db86f5e7a5b0bb69780da746d54ade3837eb026e2b23d32fba231cef. SOURCE-BUILD-INFO.json and validation/0.3.3 record the exact runtime/source/shader/package hashes. Dependencies are not downloaded automatically.

## Build and freeze

Run the following as one command, using absolute paths for your toolchain and baseline:

```powershell
python portable/build_standalone.py --compiler-directory C:/Tools/w64devkit/bin --bootstrap-compiler-directory C:/Tools/MSVC/bin/Hostx64/x64 --vulkan-include-directory C:/Tools/Vulkan-Headers/include --shader-compiler C:/Tools/dxc/bin/x64/dxc.exe --powershell C:/Tools/PowerShell/pwsh.exe --base-package C:/Releases/EnshroudedVRM-0.2.0-docs1.zip --output-directory C:/Builds/evrm-033 --version 0.3.3 --publication-status preview
```

The command above performs fresh native builds for development. A new compiler/linker can produce different bootstrap bytes even when its source and version are unchanged. To package retained binaries, use portable/package_standalone_release.py with explicit --bootstrap-dll/--bootstrap-sha256 and --runtime-dll/--runtime-sha256 inputs, a complete current source-pin file, and the accepted shader manifest. Do not change the retained DLL version resource. BUILD-INFO.json records nativeMetadataVersion for the runtime and bootstrapMetadataVersion separately.

The output directory must be fresh. The helper builds the runtime twice, builds the bootstrap twice, checks byte identity, verifies all source/shader/asset pins and produces two identical ZIPs. It records exact tool hashes and build times. It never installs the mod or launches Enshrouded. This preview targets the validated C:\Windows system-loader location; other Windows-directory layouts are refused.

A build receipt establishes reproducible bytes and provenance, not successful tests or antivirus clearance. Keep exact-hash test/scan evidence separate from the ZIP. Do not load changed distributable DLLs before the required scans.

The single included sample is explicitly hash/size-pinned. Personal models, game files, captures, private State/logs and audio are not source/release inputs. See portable/standalone_assets/README.txt for sample attribution and permissions.

## Tests

Acquire the external public speech fixture described in [tests/fixtures/README.md](tests/fixtures/README.md) before audio tests. Its bytes are not redistributed by request; the tests verify its pinned hash.

Run the selected CPU tests/test_*.py modules with the Windows compiler, Vulkan headers and DXC supplied through EVRM_TEST_COMPILER, EVRM_FIXTURE_COMPILER, EVRM_VULKAN_INCLUDE_DIRECTORY and EVRM_SHADER_COMPILER. Bootstrap tests additionally use EVRM_TEST_BOOTSTRAP_COMPILER_DIRECTORY, EVRM_APPROVED_BOOTSTRAP_DIRECTORY and the exact scanned EVRM_APPROVED_BOOTSTRAP_SHA256. Exact-runtime checks take EVRM_RUNTIME_TEST_DIRECTORY and an explicitly provided supported game executable for read-only attestation.

`python -m unittest discover -s tests -p test_graphics_lifecycle.py -v` runs the pose-snapshot and menu busy-frame regressions with fake Vulkan callbacks. It compiles production code but does not create a graphics device or window, load the runtime DLL, or launch the game. Real GPU fixtures are separate and must be selected explicitly when appropriate.

The retained script-launcher tests require their owned portable_child.exe fixture, built with tests/build_script_fixture.ps1 or test_script_fixture_build.py. They test the legacy source only; no script launcher is included in the 0.3.3 player ZIP. Suppress Windows error dialogs in the test parent with SetErrorMode(0x8003) for intentional fault cases. Fixtures must not launch the real game or capture microphone input.

Historical feature checks are retained in earlier validation records. The 0.3.3 release source passed 87 selected CPU/source/import/package methods without skips. The final package-only fog-pin correction has its own subsequent complete packaging-module result in validation/0.3.3. User-run inventory/survivor/inventory, equipment and title/character reload checks passed on the selected local implementation. The rebuilt runtime changes only its version-resource section. Wider exact-package laptop, scene and optional screenshot coverage remains pending.

## Legacy tooling

The debug-report feature adds `debug_report_policy/archive/sources/service/gpu.cpp` and `debug_report_capture.comp.spv` to the standalone Bridge build. The historical script package keeps its accepted shader set; standalone packaging adds the new, explicitly hashed report module while preserving every accepted 0.2.0 shader pin. The runtime manifest requires the complete new member set.

Run `python -m unittest -v test_debug_report` with `PYTHONPATH=tests`, `EVRM_TEST_COMPILER` and `EVRM_VULKAN_INCLUDE_DIRECTORY` configured. These tests use synthetic metadata, temporary owned files and fake Vulkan callbacks; they do not query actual hardware, load a DLL, record audio, capture the screen or create a graphics device. They cover field allowlists, private canaries, native map admission, PNG/ZIP validation, cancelled jobs, exact presented-image consent, write failures and graphics retirement. Live capture/preview, native map-coordinate interpretation and reports from different hardware still require a player-run test.

portable/script, the legacy portable/docs README/building notes and package_script_release.py remain for historical source/tests. portable/docs/NEXUS-DESCRIPTION.txt is the current public Nexus description. Research-only build.ps1 targets may reference omitted research fixtures; the supported release build is the Bridge route used by build_standalone.py.

The local ownership diagnostic milestone uses the original `LocalPlayerData+4` selector and selected entity hash chain. `tests/test_native_preview_trace.py` tests its separate bounded GPU ancestry history, privacy fields, actual fake-driver observation/copy/retirement wiring and report archive export. That first trace adds no engine hook or GPU submit/wait. Stages are binding/inline-plan/queued-plan observations; successful GPU execution is not implied.

The refined diagnostic milestone originally added four exact-image observations to trace requests through command-task creation and consumption, preserving rendering decisions. Its live trace separated inventory and survivor origins. `tests/test_native_preview_handoff.py` retains correlation/export, forwarding, unsupported-host and report-contention coverage.

The 0.3.3 preview-ownership implementation uses six original entry observations (request, Character render, returning task allocator, worker, main-context constructor and destructor), plus exact CALL observers around accepted publication and consumer copy. Setup checks the supported image and every original entry/call byte before mutation, outside rendering/loader callbacks. Relays remain process-owned. The redundant command-builder constructor observation and borrowed-builder read are removed. A separate native main-context lifetime generation rejects identical-address/ID reuse and blocks preview admission throughout construction/destruction, including nested or throwing calls. Unknown/reused task addresses cannot inherit old labels.

`tests/test_native_preview_subject.py` exercises accepted/rejected/unknown requests, publication/copy overlap, immutable deferred local/other tasks, fixed capacity, exact-key retirement, source/actor generation changes, original-call exceptions and a real owned CALL relay with return-address preservation. Its actual extracted backend methods check Character preparation, full actor verification, VRM emission, native clipping and pipeline restoration using fake external callbacks. The graphics lifecycle readback fixture records tags on every draw, including unchanged descriptors, and tests actual completion and mixed-subject rejection. These are CPU fixtures; they open no game process and create no graphics device. The selected local implementation passed user-run inventory/survivor, equipment and title/character reload checks. The versioned package still needs wider cross-machine testing. Exact release checks and scan coverage are recorded under validation/0.3.3.

0.3.3 targets game SHA2560403981fd2e8ad18bd8c5698dff03c8d505b5a46677832c496db9c645513f2a6 (Steam26320373). The packager retains the explicitly pinned0.2.0/docs1 base for vendor/rig inputs, updates only profile/catalog game metadata with capture provenance preserved, and uses the new game hash in the manifest. It never recaptures or changes rig matrices/palettes. LEIA-ME-PT-BR.txt is a supplemental root document, separate from the unchanged42-member runtime contract; its hash is recorded in BUILD-INFO and the full archive checksum evidence.
