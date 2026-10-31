## 0.2.0 source and runtime

Before running the full developer suite, manually acquire the external public speech test input described in [tests/fixtures/README.md](tests/fixtures/README.md). The audio is not redistributed in GitHub source or the player ZIP. Its provenance, MIT license and required SHA-256 are supplied; the SDK test checks the hash before use.

Build the runtime with `build.ps1 -Target Bridge -PortableRelease`. Set the compiler, Vulkan-header and DXC paths for your local toolchain. Runtime and release source pins in BUILD-INFO.json identify exact bytes; no private development paths or state are required. The official Oculus LipSync Windows DLL is obtained separately using vendor/oculus-lipsync/PROVENANCE.json and its supplied license terms; it is distributed in the player ZIP with its notices. Do not replace it with an unverified download.

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
The synthetic spring collision regression uses the same compiler setting:

```powershell
python ./tests/test_spring_collision.py
python ./tests/test_spring_shape.py
python ./tests/test_spring_arm_release.py
python ./tests/test_spring_airflow.py
python ./tests/test_spring_inertial.py
python ./tests/test_spring_cohesion.py
```

It checks sphere/capsule and overlapping contacts along a complete bone segment,
fixed bone length, attachment boundaries, moving colliders between physics ticks,
and duplicate-frame/origin-rebase stability without private avatars or the game.
Hair release cases also check authored arm-tip contacts, helper bones, stable
collision orientation through actor turns, and correction-free contact velocity.
The shape suite checks the actual skin palette of a weighted strip, posed-curve
retention, ordinary lag, collision priority, authored-mode isolation and repeated
render samples across pose changes and floating-origin shifts.
Arm-release cases cover both arms and helper colliders at 25/60/120 FPS. Relaxed
hair permits arm overlap to avoid catching; torso/head contact, Authored hair and
body-spring arm contacts remain enabled without modifying avatar metadata.
The Airflow test profile uses loose r2-style restoring/damping and full-strand
arm contacts. Smoothed horizontal actor travel creates a bounded opposing force,
stronger toward hair tips. Actor history follows camera-origin rebasing; resets,
duplicate timestamps and animated hips/head motion do not create artificial wind.
Its synthetic suite checks steady travel, speed/direction, settling, FPS, origin
handling, virtual tips and arm-on/arm-off/torso controls. The comparison runtime
selected Airflow in both renderer construction paths for the r6/r7 test builds.
The r7 refinement keeps walking airflow unchanged through 2 m/s and progressively
boosts it up to 25 percent at 5 m/s. It retains the same direction, speed cap,
filter and tip weights. The amplitude regression checks both walking and running.
The Inertial comparison profile uses world-space hair tails and existing spring
inertia/damping, with no speed-driven airflow force. Body centers and original
avatar metadata are preserved. Actor translation is sampled per fixed tick;
render-only projections do not advance history. Tests check an independent
world-space spring control, FPS, camera rebasing, duplicates, damping, resets,
turns, catch-up and torso clearance. The inertia comparison renderer selects
Inertial in both spring construction paths.
The r9 refinement carries both hair history points equally with part of each
horizontal actor translation: zero through 2 m/s, 45 percent at 5 m/s and capped
at 50 percent from 6 m/s. Effective travel remains increasing with speed. This
reduces running trail without changing spring damping/stiffness or adding force.
Vertical travel, walking and local head motion retain their full response.
Each travel sample contributes its own weighted carry until the next physics
tick; later slow/idle samples cannot reweight earlier running. Render-only
residual carry does not alter physics history, and participates in cache validity.
The 22 inertial scenarios include these controls and independent following-center
references for steady travel and speed changes between ticks.
The r10 retune increases horizontal carry to 57 percent at 5 m/s and caps it at
60 percent from approximately 5.43 m/s, still zero through 2 m/s. Effective travel
is `0.05 * speed + 1.9` across the ramp and `0.4 * speed` after the cap, so faster
travel continues to increase lag. The spring coefficients, pending carry and
collision policy are unchanged. The amplitude regression tightens the allowed
running trail while preserving walking and the independent reference controls.
The r11 refinement adds a small shared-rotation preference for neighbouring
Inertial hair sections during running. A bounded, symmetric bind-pose graph
matches separate parallel chains of similar length/progress, with two neighbours
per joint. It blends deformation rotations rather than positions, retaining the
authored spacing and common sweep. Final collisions retain priority and both
Verlet points receive the same positional correction. Render-only projection
transports the last physical roll onto its current tail without changing history
or repeating the neighbour solve. The 16 cohesion cases use skinned strips and
cover gap reduction, common motion, walking/head/profile/body isolation, contacts,
FPS, origins, duplicates, resets, scale, virtual hierarchy and large turns.

The optimization build keeps the r11 spring solver and skinning shaders unchanged.
Each native view owns a `vrm::Retargeter` that snapshots rig metadata without
copying vertices or textures. It caches static validation and calibration, keyed
by exact source-rest matrices, options and current semantic availability. Every
current source matrix is still validated on each update. Failed updates do not
replace a valid cache; the one-shot `retarget()` API remains available.

```powershell
python ./tests/test_retarget_cache.py
./tests/native_skinning_gpu.ps1 `
  -CompilerDirectory C:/Tools/w64devkit/bin `
  -VulkanIncludeDirectory C:/Tools/Vulkan-Headers/include `
  -ShaderCompiler C:/Tools/dxc/bin/x64/dxc.exe `
  -OutputDirectory ./build/native-skinning-tests
```

The retarget fixture compares changing node/palette matrices and failures against
the one-shot API, including cache invalidation, constraints, immutable metadata,
affine frames and allocations. A source-boundary fixture compiles the renderer's
actual view-admission methods and checks that rejected poses preserve all four
accepted motion histories, while successful new views retain bounded eviction.
The GPU driver compiles and runs 20 synthetic
production-shader cases plus seven deterministic resource-lifetime/fault groups.
It requires a Vulkan-capable device but does not start the game. `-Fixture GPU`
or `-Fixture Reuse` selects one group.

GPU frames reuse exact-size buffer leases and descriptor packs only after all
frame/history owners release them. Mapped storage is cleared and every descriptor
is rewritten. Caches are bounded, count against the existing aggregate budget,
and evict idle resources only. Quarantine preserves the original device-lifetime
rules. Isolated measurements showed reduced CPU calibration and Vulkan resource
allocation costs; these are not measured in-game FPS gains.
Other development targets may depend on omitted research fixtures; only the
documented targets and selected synthetic tests are claimed to be self-contained.

## Assemble a 0.2.0 release

Build and verify the current runtime and shaders using the Bridge command above.
The package builder does not compile or test the supplied DLL. Use the published
0.1.2-preview ZIP as the hash-verified compatibility baseline; the builder's strict
base-member policy does not accept the newer voice-enabled package as a base.
Download that baseline separately and preserve its published SHA-256 below.

Create a manifest for the exact two skinning shaders beside the verified DLL:

```powershell
$releaseShaders = @('native_skinning.comp.spv', 'native_skinning.finalize.spv') |
  ForEach-Object {
    $releaseShaderPath = (Resolve-Path (Join-Path './build/Runtime/shaders' $_)).Path
    @{ name = $_; path = $releaseShaderPath;
       sha256 = (Get-FileHash -LiteralPath $releaseShaderPath -Algorithm SHA256).Hash.ToLowerInvariant() }
  }
@{ schema = 'enshrouded-vrm-rebuilt-shaders-v1'; modules = @($releaseShaders) } |
  ConvertTo-Json -Depth 4 | Set-Content -LiteralPath './build/rebuilt-shaders.json' -Encoding UTF8
$releaseRuntimeHash = (Get-FileHash -LiteralPath './build/Runtime/enshrouded_vrm_runtime.dll' -Algorithm SHA256).Hash.ToLowerInvariant()
```

Package those verified bytes with the current source checkout:

```powershell
python ./portable/package_script_release.py `
  --base-package C:/Releases/EnshroudedVRM-0.1.2-preview.zip `
  --base-sha256 3f8e5610f6307fa7f85645263843715d9fb07fb4d1d5bb59cb171b10e8baf5a6 `
  --runtime-dll ./build/Runtime/enshrouded_vrm_runtime.dll `
  --runtime-sha256 $releaseRuntimeHash `
  --shader-manifest ./build/rebuilt-shaders.json `
  --version 0.2.0 `
  --root-name EnshroudedVRM-0.2.0 `
  --output ./dist/EnshroudedVRM-0.2.0.zip
```

The remaining baseline runtime/shader/compatibility members are hash-verified;
the official Oculus LipSync DLL and notices come from the separately verified
vendor input. The explicit member map regenerates metadata, manifest and hashes
without walking the workspace or downloading dependencies. Runtime and release
source pins are refreshed from the supplied checkout. The source files must
match the verified build; source hashes alone do not prove the DLL's origin.
Choose a new output path if an archive or its receipt already exists.

Keep compatibility provenance, game fingerprints, resource ownership and package
hash checks intact. Newly packaged bytes need their own scan evidence; reports
for an earlier archive do not validate a changed ZIP. Private State, saves,
avatars, captures and test executables are excluded; retain all runtime licenses.
