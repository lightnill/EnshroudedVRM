# Preview Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans
> to implement this plan inline. Use one fresh whole-change review before publication.

**Goal:** Publish 0.1.1-preview with saved one-click launching and the tested
VRoid eyebrow cutout correction.

**Architecture:** Apply a narrow, testable material compatibility rule during
import. Build the existing native runtime; assemble the script package using
the existing explicit archive allowlist and fresh build/source receipts.

**Tech Stack:** C++17/GCC, Vulkan/DXC, Windows PowerShell 5.1, Python, GitHub.

**Spec:** `docs/superpowers/specs/2026-10-27-preview-update-design.md`

## Global constraints

- Supported Steam build 26082781, resource version 1273043; retain fingerprint checks.
- Retain five launcher sessions and existing startup observation behavior.
- Preserve unrelated transparency, shaders, compatibility data, and private files.
- Publish only freshly verified artifact hashes; do not reuse old scan verdicts.

## Review focus

- A similarly named non-MToon or faded material must retain BLEND.
- Eyes, lashes in other materials, glass, and custom names must remain unchanged.
- The original avatar must work without an edited avatar copy.
- Archive and source receipts must describe the rebuilt runtime and new version.
- GitHub release assets must match local hashes and contain no player state.

## Tasks

### 1. Runtime compatibility rule

Files: `src/material_compatibility.hpp`, `src/vrm_asset.cpp`,
`tests/material_compatibility.cpp`, `tests/test_material_compatibility.py`.

- [ ] Add a failing behavioral test for the standard unfaded MToon FaceBrow
  overlay becoming MASK/cutoff 0.5, with unrelated/faded/non-MToon controls.
- [ ] Implement the smallest import rule, preserve all other material values,
  and emit a warning only when applied.
- [ ] Run the regression test and original-avatar import verification.
- [ ] Build the native runtime and verify shader equivalence.
- [ ] Verify the rebuilt runtime in-world using the original avatar.

### 2. Reproducible package and source receipts

Files: release builder, builder tests, runtime resource version, release docs,
`SOURCE-BUILD-INFO.json`, and `SHA256SUMS.txt`.

- [ ] Add failing builder tests for a hash-pinned rebuilt DLL and new version.
- [ ] Implement bounded explicit rebuilt-runtime inputs with fresh metadata.
- [ ] Update installation instructions, release notes, and compatibility scope.
- [ ] Run the launcher, builder, fixture, and material tests.
- [ ] Assemble and verify a clean package; audit public sources and receipts.

### 3. Review and publish

- [ ] Have a fresh reviewer inspect the whole change and resolve material findings.
- [ ] Commit the authorized changes and integrate with current GitHub main safely.
- [ ] Push without force; publish a new prerelease and upload verified assets.
- [ ] Verify remote source, release tag, asset hashes, and links.

## Execution ledger

- Existing feature checkout contains all authorized launcher changes. Work in
  place to preserve that state; no additional worktree or dependency installation.
- User publication request authorizes the push and new release; no second
  publication confirmation is needed.
- Task 1 RED: real synthetic VRM 0/1 imports reached 84 checks and failed only
  the six expected eyebrow routing/cutoff/warning assertions; negative controls passed.
- Ruling: keep the small rule inside the existing material import function instead
  of adding a separate header. Real importer tests cover its public behavior.
- Task 2 RED/GREEN: rebuilt-runtime CLI test failed on missing arguments, then all
  17 release-builder tests passed after adding hash-pinned DLL/version support.
- Task 1 GREEN: 84 synthetic material checks and 3 original-avatar checks passed.
  Runtime DLL built successfully; all 13 shaders match the prior package. User
  confirmed normal eyebrows, movement and held items with the original avatar
  using the rebuilt DLL in-world.
- Review P2 reproduced: an existing unsupported executable suppressed the setup
  picker. Added a failing negative control and hash validation before skipping
  the picker. Full final launcher verification runs with no live game process.
- Review P3: refresh packaged compatibility and Nexus installation/evidence copy.
