# EnshroudedVRM 0.3.3

Play Enshrouded as your VRM avatar. By **lightnill**.

[Português (Brasil): instalação e solução de problemas](README.pt-BR.md). The ZIP also includes `LEIA-ME-PT-BR.txt`.

0.3.3 restores compatibility with Enshrouded Steam build **26320373** and adds a Portuguese installation guide. Start normally through Steam and configure the mod with **F8**. Local-player ownership, correct inventory/survivor previews, held equipment, both lip-sync sources, model switching and saved material presets are retained. Multiplayer avatar synchronization is future work.

The release and avatar runtime are version 0.3.3. The bootstrap keeps its independent
0.2.0-standalone-dev file version and its previously tested DLL bytes. BUILD-INFO.json
records the runtime and bootstrap versions separately.

## Install and play

1. Download **EnshroudedVRM-0.3.3.zip** from [the 0.3.3 release](https://github.com/lightnill/EnshroudedVRM/releases/tag/v0.3.3).
2. Close Enshrouded. In Steam, choose **Manage â†’ Browse local files**.
3. Check for an existing `vulkan-1.dll` beside `enshrouded.exe`. If it belongs to another mod, stop: do not overwrite, rename or chain it. Close the game before updating a known EnshroudedVRM installation.
4. Copy the ZIP's `vulkan-1.dll` and `EnshroudedVRM` folder beside `enshrouded.exe`. Preserve your own models when updating the folder.
5. Start Enshrouded normally through Steam. Press **F8** and choose a VRM using **Browse...** or the model list.

The player download needs no launcher EXE, CMD/batch file, PowerShell startup command, Python, administrator access or separate mod loader. The original game character stays visible until a valid model is selected. The old 0.2.0 script launcher is no longer needed for this package.

## Avatar and appearance

The included `vroid-sample.vrm` was assembled by Lightnill with VRoid Studio default assets. It is optional and is never selected automatically. The exact export is retained with its own embedded VRM permissions; see `EnshroudedVRM/Avatars/README.txt`. The mod's MIT code license does not replace the avatar's permissions.

In **F8 â†’ Avatar**, enable **No model replacement** to use the game's original character. Disable it to restore your selected VRM. The switch, selected model and shader preferences survive restarts. Model and appearance controls are greyed out while replacement is off. The already-loaded avatar is cached for quick return; a disabled startup does not load the saved VRM.

Appearance offers **Game lighting** and **Matte** whole-model presets, plus individual material overrides, inheritance and reset. Profiles use model content identity, so an identical renamed VRM retains its appearance settings. Full MToon shading and native armor fitting remain future work.

Embedded 16-bit-per-channel PNG textures are converted to RGBA8 during import. Rendering keeps the existing texture format and memory cost. Higher-depth decoding requires temporary workspace, so large images can still be declined by the existing image-memory limits.

## Voice to viseme

Voice is opt-in. **Game voice** uses the game's local outgoing voice path and requires a session with active voice chat. **Local microphone** works offline, follows Windows default input unless you choose a device, and is independent of game mute/push-to-talk. Strength, sensitivity and smoothing are configurable. Processing stays local; audio is not recorded or uploaded and no extra viseme network data is sent.

## Updating and saved settings

To update an existing EnshroudedVRM installation, close the game and replace the included package files together. Keep your own VRMs and saved preferences. The retained bootstrap has its own independent version; do not mix runtime/shader files from other builds. A 0.2.0 script launcher is no longer used after installing this standalone package.

Settings and bounded logs live in `%LOCALAPPDATA%/EnshroudedVRM/State`, independently of the game installation. For a first import from the old package, copy only `avatar-settings.json` and `mod-settings.json` from its State folder into `EnshroudedVRM/State` beside Runtime, then launch once. Existing user-folder preferences win and are never overwritten by import. Old launcher `settings.json` is not imported.

Older avatar settings are read compatibly. New saves use schema 2 to remember replacement enablement. Keep a settings backup before downgrading to an older runtime. Leave a moment after the last edit before closing the game so the background save can finish.

## Coverage and limits

The standalone route has desktop evidence on **Windows 10 build 19045 x64**, Windows installed in **C:\Windows**, Vulkan loader **1.4.350**, and Steam build **26320373**. Desktop checks on the new game build cover Steam startup/F8, world avatar and held equipment, inventory/survivor/inventory previews, title/character reload, and Game voice plus Local microphone lip sync. Laptop reports informed these fixes; the versioned 0.3.3 package has not completed full cross-machine validation. Windows 11, other loader versions, other Windows-directory layouts and combined-loader operation remain unverified.

An existing Vulkan loader loaded first, or a System32-only load, can bypass the local bootstrap. Other mods' `vulkan-1.dll` files conflict with this installation. Earlier EML/ShroudForge coexistence observations for the script route do not validate this route. Game-version and package checks intentionally decline unsupported or changed files. See [known issues](KNOWN-ISSUES.txt).

Higher fog settings no longer hit the old snapshot dimension limit. The fog snapshot uses a packed representation within the existing 64 MiB allocation budget; driver allocation requirements remain checked. Local-player ownership is used before matching model parts, so unrelated world characters are excluded from that selection.

World replacement now follows the game's explicit local-player selector. Inventory previews require their own verified local subject; survivor previews stay native. The selected local build passed inventory/survivor/inventory, equipment and title/character reload checks on the desktop. Wider scene and hardware coverage is still needed. Base-area stutters can still occur: a complete VRM-off comparison reduced, but did not eliminate, them. The included sampled hook timings are diagnostic evidence; no gameplay FPS improvement is claimed. Large avatars can reduce FPS. The release provides [exact-hash validation and scan evidence](validation/0.3.3/README.md); unsupported engines and scan limits are coverage gaps.

## Remove

Close the game and remove only this package's `vulkan-1.dll` and mod files. Preserve any personal avatars/import files before removing the folder. User-folder settings remain. Game executable/data files and other mods are not replaced.

See [building from source](BUILDING.md), [release notes](RELEASE-NOTES.md), [security behavior](SECURITY.md) and [third-party notices](THIRD_PARTY_NOTICES.md). Original code is MIT. The official Oculus LipSync runtime and the sample have separate terms. This unofficial mod is not endorsed by Keen Games.

## Report an issue

In F8, choose **Create debug report**. The ZIP is saved under `%LOCALAPPDATA%/EnshroudedVRM/State/Reports`; **Open reports folder** opens that folder. Nothing uploads automatically.

Reports reconstruct allowlisted recent diagnostics and settings, CPU/RAM/active GPU/driver details, anonymous model statistics, and observed terrain/building/depth/fog rendering context. Fresh qualified **in-game Enshrouded XYZ coordinates** are included when available (Y up, X/Z horizontal). Real-world location, actual voxel blocks and saves are not collected.

Screenshots are off for every new report. If requested, capture is limited to the game image; F8 is hidden for that frame. Inspect the exact preview and choose **Include this screenshot** or **Save without screenshot**. An approved image can contain visible names/chat. Raw logs/settings, personal paths, account/server identifiers, microphone endpoint IDs, audio, models and memory dumps are omitted. Missing fields are marked unavailable. Review the ZIP before sharing it yourself.

## Game update compatibility

0.3.3 supports only Steam build **26320373**, executable SHA-256 `0403981fd2e8ad18bd8c5698dff03c8d505b5a46677832c496db9c645513f2a6`. **0.3.2 is retained for the previous game build 26082781** (resource 1273043); it is not a fallback for the updated game. Update all package files together. Fog Ultra hotspots, model/material persistence, optional report screenshots and the laptop still need fresh coverage on this patch; older tests do not establish that coverage.

The exported report records the selected voice source at capture time. It currently omits voice-source history/counters; a later microphone selection does not mean Game voice was never used. Raw session logs stay local and may contain private details; do not publish them without reviewing them.
