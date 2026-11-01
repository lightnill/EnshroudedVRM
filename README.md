# EnshroudedVRM 0.3.0

Play Enshrouded as your VRM avatar. By **lightnill**.

0.3.0 changes startup to a standalone game-local Vulkan bootstrap. Install the mod, launch normally through Steam, and configure it in **F8**. Audio lip sync, model switching and whole-model/per-material shader presets remain available. An optional VRoid sample and a persistent **No model replacement** switch are included. Replacement is local to your client; multiplayer avatar synchronization is future work.

The release and avatar runtime are version 0.3.0. The bootstrap keeps its independent
0.2.0-standalone-dev file version and its previously tested DLL bytes. BUILD-INFO.json
records the runtime and bootstrap versions separately.

## Install and play

1. Download **EnshroudedVRM-0.3.0.zip** from [the 0.3.0 release](https://github.com/lightnill/EnshroudedVRM/releases/tag/v0.3.0).
2. Close Enshrouded. In Steam, choose **Manage → Browse local files**.
3. Check for an existing `vulkan-1.dll` beside `enshrouded.exe`. If it belongs to another mod, stop: do not overwrite, rename or chain it. Close the game before updating a known EnshroudedVRM installation.
4. Copy the ZIP's `vulkan-1.dll` and `EnshroudedVRM` folder beside `enshrouded.exe`. Preserve your own models when updating the folder.
5. Start Enshrouded normally through Steam. Press **F8** and choose a VRM using **Browse...** or the model list.

The player download needs no launcher EXE, CMD/batch file, PowerShell startup command, Python, administrator access or separate mod loader. The original game character stays visible until a valid model is selected. The old 0.2.0 script launcher is no longer needed for this package.

## Avatar and appearance

The included `vroid-sample.vrm` was assembled by Lightnill with VRoid Studio default assets. It is optional and is never selected automatically. The exact export is retained with its own embedded VRM permissions; see `EnshroudedVRM/Avatars/README.txt`. The mod's MIT code license does not replace the avatar's permissions.

In **F8 → Avatar**, enable **No model replacement** to use the game's original character. Disable it to restore your selected VRM. The switch, selected model and shader preferences survive restarts. Model and appearance controls are greyed out while replacement is off. The already-loaded avatar is cached for quick return; a disabled startup does not load the saved VRM.

Appearance offers **Game lighting** and **Matte** whole-model presets, plus individual material overrides, inheritance and reset. Profiles use model content identity, so an identical renamed VRM retains its appearance settings. Full MToon shading and native armor fitting remain future work.

Embedded 16-bit-per-channel PNG textures are converted to RGBA8 during import. Rendering keeps the existing texture format and memory cost. Higher-depth decoding requires temporary workspace, so large images can still be declined by the existing image-memory limits.

## Voice to viseme

Voice is opt-in. **Game voice** uses the game's local outgoing voice path and requires a session with active voice chat. **Local microphone** works offline, follows Windows default input unless you choose a device, and is independent of game mute/push-to-talk. Strength, sensitivity and smoothing are configurable. Processing stays local; audio is not recorded or uploaded and no extra viseme network data is sent.

## Settings and upgrading from 0.2.0

Settings and bounded logs live in `%LOCALAPPDATA%/EnshroudedVRM/State`, independently of the game installation. For a first import from the old package, copy only `avatar-settings.json` and `mod-settings.json` from its State folder into `EnshroudedVRM/State` beside Runtime, then launch once. Existing user-folder preferences win and are never overwritten by import. Old launcher `settings.json` is not imported.

Older avatar settings are read compatibly. New saves use schema 2 to remember replacement enablement. Keep a settings backup before downgrading to an older runtime. Leave a moment after the last edit before closing the game so the background save can finish.

## Coverage and limits

The standalone route has desktop evidence on **Windows 10 build 19045 x64**, Windows installed in **C:\Windows**, Vulkan loader **1.4.350**, and Steam build **26082781/resource 1273043**. Laptop validation is pending for this GitHub preview. Windows 11, other Vulkan loader versions, other Windows-directory layouts and combined-loader operation are not claimed as validated.

An existing Vulkan loader loaded first, or a System32-only load, can bypass the local bootstrap. Other mods' `vulkan-1.dll` files conflict with this installation. Earlier EML/ShroudForge coexistence observations for the script route do not validate this route. Game-version and package checks intentionally decline unsupported or changed files. See [known issues](KNOWN-ISSUES.txt).

Large avatars can reduce FPS. Isolated timings and test results do not establish a gameplay FPS improvement. The release provides [exact-hash validation and scan evidence](validation/0.3.0/README.md); unsupported engines and scan limits are coverage gaps.

## Remove

Close the game and remove only this package's `vulkan-1.dll` and mod files. Preserve any personal avatars/import files before removing the folder. User-folder settings remain. Game executable/data files and other mods are not replaced.

See [building from source](BUILDING.md), [release notes](RELEASE-NOTES.md), [security behavior](SECURITY.md) and [third-party notices](THIRD_PARTY_NOTICES.md). Original code is MIT. The official Oculus LipSync runtime and the sample have separate terms. This unofficial mod is not endorsed by Keen Games.
