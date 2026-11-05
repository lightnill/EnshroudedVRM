# EnshroudedVRM 0.4.0

**Still buggy and incomplete.** Shader quality and stability are below the intended standard. Other players' voice-driven visemes/lip sync are not implemented. This release is named 0.4.0 without a Preview suffix.

For Enshrouded Steam build **26430625**. Version 0.3.3 remains available for build 26320373.

## Features

- F8 Player selector reuses Avatar controls for per-account, remembered local model assignments. Unassigned players default to native appearance; multiple different VRMs can render simultaneously. No model/assignment networking is added.
- Broader importer support for standard glTF normal maps, supported VRM0/VRM1 MToon matcap/rim textures and masks, supported UV transforms and tangent generation. Existing 16-bit PNG-to-RGBA8 import remains supported. Custom Unity effects and arbitrary gloss/roughness-mask workflows are not claimed.
- Auto/Enhanced/Fast/Matte whole-model presets and per-material overrides, persisted by model content. Same-model assignments share shader profiles.
- Relaxed importer-only memory caps, F8 memory estimates and background optional shader preparation. Decoder, structural, device and allocation safeguards remain; large-model loading can still stall or fail.
- One-click privacy-conscious local reports with recent diagnostics, settings, hardware/rendering context and in-game map position. Screenshots require review and explicit inclusion; reports never upload automatically.
- Updated exact-game ownership, correct local inventory versus survivor previews, multiplayer join/leave handling, equipment and fog/building compatibility. Repeated ownership reads and driver lookup work were reduced.

## Known limitations

Reflective surfaces, matcaps, transparency and dim/night lighting still need work. Fast can be a useful fallback for incorrect enhanced shading. Brief native-model fallbacks and frame spikes remain, including a known wall/visibility edge case. Initial preparation can delay a body or preview; a remote weapon can appear first. Local microphone/game-voice lip sync animates You only; **remote visemes do not work yet**. Native armor fitting, complete Unity/MToon/custom shader effects and remote model synchronization are not implemented.

Desktop/laptop two-player checks passed on the supported build, with remaining timing events documented. CPU fixtures and isolated shader checks do not prove game FPS or full visual fidelity. Exact-file verification/scans accompany the release. Preserve your VRMs/settings, update the entire package with the game closed, launch normally through Steam and configure through F8.
