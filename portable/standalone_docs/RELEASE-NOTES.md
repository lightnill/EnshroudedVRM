# EnshroudedVRM 0.3.2 — local ownership, correct previews and debug reports

- Resolve world ownership through the game's explicit local-player selector and direct entity lookup.
- Keep local inventory previews on the VRM and survivor previews on their native models, using per-request/task/captured-pose association and fresh actor checks.
- Reduce ownership refresh and driver-lookup overhead; retain the existing verification, work and pose-age limits.
- Correct equipment publication/advance timing and preserve held-item alignment through recovery.
- Add F8 Create debug report and Open reports folder, with allowlisted diagnostics/settings/hardware/rendering context and qualified in-game map coordinates. Reports stay local until the user shares them.
- Screenshots are off per report and require preview plus explicit inclusion. No audio, model files, saves, raw logs/settings, account/server IDs, personal paths or real-world location are collected.
- Retain standalone Steam startup, F8 models/voice/materials, higher-fog compatibility, optional sample and persistent original-character switch.

The selected local implementation passed desktop inventory/survivor/inventory, equipment and title/character reload checks. Ownership CPU tests improved initial 7.598→2.389 ms, repeated 1.023→0.378 ms and changed-generation refresh 6.689→0.922 ms in the same controlled 6,500-entity fixture. These are resolver CPU measurements; a matched gameplay FPS comparison has not been performed. Occasional stutters, wider scene/hardware coverage and optional screenshot/map interpretation still need broader testing. Multiplayer synchronization, full MToon and native armor fitting are not implemented.

The runtime is version 0.3.2. The tested startup DLL is unchanged and keeps its independent 0.2.0-standalone-dev file version. Close Enshrouded before updating package files together; retain personal VRMs and preferences. Exact release scan results are supplied separately.

---

# EnshroudedVRM 0.3.1 â€” fog compatibility and replacement recovery

- Reworks the fog/lighting-volume snapshot so replacement can operate above Fog Performance, within the existing checked 64 MiB allocation budget.
- Selects the local player's owned GPU instance IDs before model-part matching; unrelated world actors no longer participate in that world-pose selection.
- Resolves refreshed poses for their own source view and preserves a freshly verified ownership observation when only the old render pose expires. Full owner revalidation and the original 250 ms pose lifetime remain.
- Includes transient GPU-busy/menu and readback-lifetime fixes, plus bounded local fallback and sampled performance diagnostics.
- Retains normal Steam startup, F8 configuration, audio lip sync, offline/default/selectable microphones, model switching, saved shader/material profiles, the optional sample and the saved original-character switch.

Known limits: some scene/menu fallbacks and survivor-preview identification remain under investigation. Stutters remain at a user-built voxel base and were reduced, not eliminated, with VRM completely disabled. No measured gameplay performance improvement is claimed. Multiplayer replacement synchronization is not implemented.

The release/runtime version is 0.3.1. The startup DLL is retained byte-for-byte with its independent 0.2.0-standalone-dev file version. The unchanged 16-bit PNG-to-RGBA8 importer compatibility from 0.3.0 is included. Close the game before updating; preserve user models/preferences. Exact scan/test records are supplied separately and apply only to their stated hashes.

---

EnshroudedVRM 0.3.0 â€” standalone startup

The release and avatar runtime are version 0.3.0. The bootstrap keeps its independent
0.2.0-standalone-dev file version and its previously tested DLL bytes. BUILD-INFO.json
records the runtime and bootstrap versions separately.

Start through Steam after installing the game-local bootstrap and mod folder.
Configure avatars, voice and appearance in F8. The player package has no
launcher EXE, batch/PowerShell startup file or external mod-loader dependency.

Includes Lightnill's unchanged vroid-sample.vrm as an optional model in F8.
No model replacement switches to the original game character and persists across
restarts while remembering the chosen VRM and shader presets. Disabled startup
does not load the saved model, and disabled replacement skips avatar pose/upload
work. Existing settings migrate without changing their enabled behavior.

Settings now use a stable user folder, with validated, non-overwriting import
of the two existing preference files. Native package/game checks precede
optional services. Unsupported helper programs use ordinary Vulkan forwarding.

The lip-sync/model/material algorithms and shaders are retained from the tested
feature build. Multiplayer synchronization remains future work. Desktop manual
startup/model/toggle/restart checks passed; laptop validation is still pending.
Platform coverage and scan results belong to each exact release's evidence.

Importer compatibility: embedded 16-bit-per-channel PNG textures are normalized
to RGBA8 at load time, with alpha preservation and decode-workspace checks.
Existing 8-bit decoding and unused-map placeholders retain their established paths.
