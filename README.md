# EnshroudedVRM

Play Enshrouded as your own VRM avatar. By **lightnill**.

The mod uses GPU skinning to reduce CPU work when animating the avatar. This is
an early Windows x64 preview. The replacement is visible only to you; multiplayer
avatar support is planned but is not implemented. No avatars or game executables
are bundled.

## Install and play

1. Download the CMD package from the [current release](https://github.com/lightnill/EnshroudedVRM/releases/tag/v0.1.1-preview).
2. Extract the whole ZIP to a writable local folder outside the game directory.
   The package-folder path must contain neither semicolons (;) nor percent (%) signs.
3. Start Steam, then run **Launch-EnshroudedVRM.cmd**.
4. Choose your own .vrm. Select enshrouded.exe only if automatic game detection
   cannot find it. Review the paths, then type **Y**.
5. After the first successful launch, use the generated **Play-EnshroudedVRM.cmd**
   for subsequent launches. It uses the saved avatar/settings with no picking or typing.

Offline play is the tested path. Close the game before changing avatars. Use
**Launch-EnshroudedVRM.cmd** again to change the avatar or repair missing paths.
Keep **Play-EnshroudedVRM.cmd** beside the other launcher files; it follows the
package if moved, provided the saved game and avatar paths still exist.
Starting through Steam alone runs
the normal game. To uninstall, close the game and remove the extracted folder.

The game starts immediately. The launcher checks its first 30 seconds, then closes
automatically; an early exit and its code are recorded without retrying the game.
State/Logs retains the latest five attempts, including a game-log tail of at most
256 KB for each. Older recognized attempts are removed on the next launch, and
completed retained mod logs are trimmed to 1 MB. Keep State private.

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
Initial single-player checks passed with EML 0.2.0 and ShroudForge 1.7.5, each
tested separately alongside VRM, including the tested Hands-Free Lantern 0.11.1
scenario. See the [loader checks](docs/compatibility/loader-coexistence.md) and
[lantern checks](docs/compatibility/hands-free-lantern.md) for scope. Installing
both loaders together remains untested. The launcher leaves adjacent loader files
alone. Executable changes still fail the exact-build check. Direct loader
integration and graphics-layer discovery changes remain deferred.

Standard unfaded VRoid MToon `_FaceBrow_` materials use alpha cutout in the world
to correct eyebrow shading. Other transparent materials retain world blending.
This targeted compatibility rule does not implement full MToon shading.
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
