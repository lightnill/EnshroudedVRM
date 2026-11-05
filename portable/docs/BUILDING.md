# Building the private MSVC 14.44 candidate

This branch ports the published 0.4.0 implementation to MSVC. It is a private development candidate pending desktop and laptop validation. These instructions are for developers. SOURCE-BUILD-INFO.json and validation/0.4.0 retain historical evidence for the published baseline; they do not validate newly compiled bytes.

## Inputs and toolchain

The loader, runtime and native tests use Visual Studio 2022 x64 MSVC v143 toolset 14.44.35207 (cl 19.44.35229.0), Windows SDK 10.0.28000.0 and the static CRT (/MT). portable/native_compiler.py verifies the actual compiler and SDK, rejects other compilers/unknown options, and records commands and tool identities. There is no GCC fallback. Use Python, PowerShell 7, the existing Vulkan-Headers and DXC inputs. Neither the toolchain nor prebuilt licensed dependencies are downloaded automatically.

The bootstrap retains its independent 0.2.0-standalone-dev resource version. Rebuild it with portable/bootstrap/build_bootstrap.py rather than substituting a system DLL. The new loader hash is recorded in each candidate's build receipt.

Acquire the official Oculus LipSync runtime separately under vendor/oculus-lipsync/PROVENANCE.json and its terms. Its DLL is excluded from public source and distributed unchanged in the player ZIP. See tests/fixtures/README.md to download the hash-pinned public speech fixture separately; audio is deliberately omitted from GitHub source.

The standalone baseline remains EnshroudedVRM-0.2.0-docs1.zip from release v0.2.0, SHA256 6905577b1e5afeb48659de6b7875ec6b449a625175184cd887aebc3b61483a26. The package builder preserves captured rig/catalog provenance, updates only the supported-image fields, and uses explicit source/native/shader pins. No dependencies are downloaded automatically.

## Build

Set EVRM_PYTHON to an explicit Python executable. Run build.ps1 with Target Bridge, CompilerDirectory, VulkanIncludeDirectory, ShaderCompiler, OutputDirectory and PortableRelease. CompilerDirectory is the pinned toolset's bin/Hostx64/x64 directory. Use absolute tool/output paths. The resource compiler is the pinned SDK x64 rc.exe. portable/runtime.rc retains runtime version 0.4.0; candidate version/publication status is recorded separately.

For a complete private freeze use portable/build_standalone.py with explicit --compiler-directory, --bootstrap-compiler-directory, --vulkan-include-directory, --shader-compiler, --base-package, --output-directory and --powershell. Both compiler directories must select the pinned toolset. Supply a distinct private --version and --publication-status private-development. The output directory must be fresh. The builder checks repeated runtime/loader/archive bytes, validates source pins again after compilation, and records exact source/tool/native/shader hashes in build-receipt.json. Packaging does not establish gameplay compatibility.

There are 21 custom SPIR-V modules. All must match the accepted shader hashes; shader algorithms and ABI contracts are preserved. Native PE code changes when changing compilers, so a .rsrc-only comparison is not an acceptance criterion for this port. PDBs, maps, logs and native invocation evidence remain private and outside the player ZIP. Never inherit a prior binary's validation or scan result.

## CPU and shader verification

Run Python tests/run_msvc_cpu_suite.py for the complete checked-in CPU/source/packaging suite. build.ps1 -Target Test selects that suite; -Target All first builds Bridge and then runs it. Several historical build.ps1 named targets refer to fixtures omitted from the published source export; those paths are not evidence of passing coverage.

Real Vulkan/GPU fixtures are separate: use tests/run_msvc_cpu_suite.py --gpu-only or build.ps1 -Target GpuTests and supply their reviewed native input directories. --include-gpu explicitly combines both groups. The runner records every test, failure and skip; additional input gates remain meaningful. Set EVRM_TEST_COMPILER, EVRM_FIXTURE_COMPILER, EVRM_VULKAN_INCLUDE_DIRECTORY, EVRM_SHADER_COMPILER and EVRM_SPIRV_VALIDATOR as needed. Existing tests use generated VRMs, owned memory and fake Vulkan callbacks; ordinary discovery is not a GPU benchmark. Owned intentional-crash fixtures suppress Windows fault dialogs locally.

Relevant groups include enhanced material import/UV/tangent generation, 16-bit textures and image allocation cleanup, player assignments/identity/availability/backend/commit, local-preview request/task lifetime, debug-report privacy, background shader preparation, source/pose/tool publication, game-update guards and native scene tuples. Existing passing feature/game-compatibility checks are retained only when their source inputs match. Release packaging and documentation checks run against the final source and exact archive. CPU/shader checks do not establish game FPS or full raster/OIT fidelity.

## Contracts and limitations

Only Steam build 26430625, executable SHA256 5d9ef2f140b8ed49e30dbee655841b89d08baec20f799a1817e171a18a850c77, is supported. The ownership, request/copy/task, equipment, voice and scene fingerprints were re-derived; previous/mixed images decline. The 40 ms ownership work and 250 ms pose limits are unchanged.

Auto/Enhanced support normal maps and selected VRM0/VRM1 matcap/rim textures and masks, with generated tangents. The optional vertex stream is 32 bytes; optional material block 112 bytes, raw skin 64 bytes, native geometry 44 bytes and prepare frame 224 bytes. MikkTSpace source and notice are retained. This is an approximation in native lighting, not full Unity/MToon/custom-shader support.

Player choices use installation-keyed HMAC tokens and private local paths; they are never release/report inputs. Other players default to native, assignments remain client-local, remote visemes are not implemented, and same-model shader settings are shared. Initial preparation, short fallback events, reflection/low-light issues and performance spikes remain. See KNOWN-ISSUES.txt and validation/0.4.0 for bounded manual coverage.

Never publish personal models, raw logs/reports/screenshots, State, installation keys, account IDs, game executables/assets, developer fixture EXEs or speech audio. The sole authorized sample is explicitly hash-pinned. Release assets contain the player ZIP and public-only checksums/provenance/scan summaries, not private acceptance reports.


## Historical 0.4.0 archive metadata

The 0.4.0 packager writes a fixed publication time (2026-11-03 14:47:00 America/Denver), creator system 0 and Windows archive attribute 0x20. This preserves deterministic output without using a placeholder 1980 date. The corrected public ZIP is a header-only repack of the original release; its embedded provenance still describes the unchanged payload. A source rebuild regenerates source pins/BUILD-INFO and therefore is not promised to reproduce that header-only repack byte for byte. Native implementation sources and binaries are unchanged.
