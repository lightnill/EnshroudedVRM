# Building EnshroudedVRM 0.4.0

Players download EnshroudedVRM-0.4.0.zip, copy its two installation items beside enshrouded.exe, launch through Steam and configure F8. These instructions are for developers.

## Inputs and toolchain

The runtime is built with Windows x64 GCC 16.2.0 (w64devkit), Vulkan-Headers, DXC and PowerShell 7. The retained bootstrap is Microsoft-built and independently versioned 0.2.0-standalone-dev, SHA256 d0e5c7f7db86f5e7a5b0bb69780da746d54ade3837eb026e2b23d32fba231cef. Do not relabel its resource or substitute an operating-system DLL. Python runs packaging/CPU checks. Use exact tool/source pins from SOURCE-BUILD-INFO.json and validation/0.4.0.

Acquire the official Oculus LipSync runtime separately under vendor/oculus-lipsync/PROVENANCE.json and its terms. Its DLL is excluded from public source and distributed unchanged in the player ZIP. See tests/fixtures/README.md to download the hash-pinned public speech fixture separately; audio is deliberately omitted from GitHub source.

The standalone baseline remains EnshroudedVRM-0.2.0-docs1.zip from release v0.2.0, SHA256 6905577b1e5afeb48659de6b7875ec6b449a625175184cd887aebc3b61483a26. The package builder preserves captured rig/catalog provenance, updates only the supported-image fields, and uses explicit source/native/shader pins. No dependencies are downloaded automatically.

## Build

Run build.ps1 with Target Bridge, CompilerDirectory, VulkanIncludeDirectory, ShaderCompiler, OutputDirectory and PortableRelease. Use explicit absolute tool/output paths. portable/runtime.rc carries runtime version 0.4.0. Then use portable/package_standalone_release.py with explicit baseline, runtime, retained-bootstrap, source-pin and shader-manifest inputs, version 0.4.0 and publication-status release. The output directory/file must be fresh. Check all package members against release-manifest.json and the supplemental Portuguese guide against BUILD-INFO.json.

There are 21 custom SPIR-V modules. The release keeps the exact user-tested shader algorithms and native implementation; compare rebuilt PE sections against the accepted runtime so only .rsrc differs. Runtime and shader build receipts, game-version proofs, tests and scans are separate evidence. Do not inherit an old binary's scan result for new bytes.

## CPU and shader verification

Supply EVRM_TEST_COMPILER, EVRM_FIXTURE_COMPILER, EVRM_VULKAN_INCLUDE_DIRECTORY, EVRM_SHADER_COMPILER and EVRM_SPIRV_VALIDATOR. Existing tests use generated VRMs, owned memory and fake Vulkan callbacks; ordinary discovery is not a GPU benchmark. Real GPU fixtures are opt-in, gated by exact reviewed/scanned inputs. Suppress Windows fault dialogs in owned intentional-crash tests with SetErrorMode(0x8003).

Relevant groups include enhanced material import/UV/tangent generation, 16-bit textures and image allocation cleanup, player assignments/identity/availability/backend/commit, local-preview request/task lifetime, debug-report privacy, background shader preparation, source/pose/tool publication, game-update guards and native scene tuples. Existing passing feature/game-compatibility checks are retained only when their source inputs match. Release packaging and documentation checks run against the final source and exact archive. CPU/shader checks do not establish game FPS or full raster/OIT fidelity.

## Contracts and limitations

Only Steam build 26404966, executable SHA256 ce6fdba63e27744943b695921383a29133285d359e07671f6ca33a47e2147618, is supported. The ownership, request/copy/task, equipment, voice and scene fingerprints were re-derived; previous/mixed images decline. The 40 ms ownership work and 250 ms pose limits are unchanged.

Auto/Enhanced support normal maps and selected VRM0/VRM1 matcap/rim textures and masks, with generated tangents. The optional vertex stream is 32 bytes; optional material block 112 bytes, raw skin 64 bytes, native geometry 44 bytes and prepare frame 224 bytes. MikkTSpace source and notice are retained. This is an approximation in native lighting, not full Unity/MToon/custom-shader support.

Player choices use installation-keyed HMAC tokens and private local paths; they are never release/report inputs. Other players default to native, assignments remain client-local, remote visemes are not implemented, and same-model shader settings are shared. Initial preparation, short fallback events, reflection/low-light issues and performance spikes remain. See KNOWN-ISSUES.txt and validation/0.4.0 for bounded manual coverage.

Never publish personal models, raw logs/reports/screenshots, State, installation keys, account IDs, game executables/assets, developer fixture EXEs or speech audio. The sole authorized sample is explicitly hash-pinned. Release assets contain the player ZIP and public-only checksums/provenance/scan summaries, not private acceptance reports.


## Archive metadata

The 0.4.0 packager writes a fixed publication time (2026-11-03 14:47:00 America/Denver), creator system 0 and Windows archive attribute 0x20. This preserves deterministic output without using a placeholder 1980 date. The corrected public ZIP is a header-only repack of the original release; its embedded provenance still describes the unchanged payload. A source rebuild regenerates source pins/BUILD-INFO and therefore is not promised to reproduce that header-only repack byte for byte. Native implementation sources and binaries are unchanged.
