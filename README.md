# EnshroudedVRM

Play Enshrouded as your own VRM avatar. By **lightnill**.

The mod uses GPU skinning to reduce CPU work when animating the avatar. This is
an early Windows x64 preview. The replacement is visible only to you; multiplayer
avatar support is planned but is not implemented. No avatars or game executables
are bundled.

## Install and play

1. Download the CMD package from the [current release](https://github.com/lightnill/EnshroudedVRM/releases/tag/v0.1.0-preview-script).
2. Extract the whole ZIP to a writable local folder outside the game directory.
3. Start Steam, then run **Launch-EnshroudedVRM.cmd**.
4. Choose enshrouded.exe and your own .vrm, review the paths, then type **Y**.

Offline play is the tested path. Close the game before changing avatars. Use
the CMD each time you want the replacement; starting through Steam alone runs
the normal game. To uninstall, close the game and remove the extracted folder.

Windows 10/11 x64 with the built-in Windows PowerShell 5.1 is required. The scripts
respect existing script policies and do not change security settings. Restrictive
policies may prevent them from running. No administrator rights, Python or global
installation is required for players.

The three readable launcher files are in [portable/script](portable/script).
The required native Vulkan runtime DLL handles the in-game rendering. The package
includes GPU skinning, lit rendering, spring motion, menu previews and held-item
alignment.

## Compatibility and limits

Supports Steam build **26082781**, resource version **1273043**. Other executable
builds are rejected. Supports VRM 0.x/1.x humanoid rigs within importer bounds.
Large avatars can reduce FPS. Menu previews use alpha cutout for blended materials.
Native armor fitting and multiplayer avatar synchronization are not included.
See [known issues](KNOWN-ISSUES.txt) and [release notes](RELEASE-NOTES.md).

## Source, support and license

See [BUILDING.md](BUILDING.md) for runtime, fixture and release assembly steps.
Source packages exclude private avatars, working data and game executables.
Never attach private models, derived geometry or unredacted State/logs to issues.

Original code is **MIT**, Copyright (c) 2026 **lightnill**. Reuse with the copyright
and license notice retained. Preserve [third-party notices](THIRD_PARTY_NOTICES.md).
Game and avatar rights remain with their owners. This mod is unofficial and not
affiliated with or endorsed by Keen Games.

See [SECURITY.md](SECURITY.md) for behavior and the release's individual-file scan
evidence. Scanner coverage varies by file type; platform approval is separate.
Development included substantial AI-generated code under lightnill's direction
and testing. No ValheimVRM code or assets are bundled.
