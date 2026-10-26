# EnshroudedVRM

Use your own VRM avatar in Enshrouded. By **lightnill**.

Windows x64 portable preview with GPU skinning, lit rendering, spring bones,
menu previews and held-item alignment. The replacement is local to your screen.
No avatars or game executables are bundled.

Current download: [0.1.0 preview with the MSVC launcher](https://github.com/lightnill/EnshroudedVRM/releases/tag/v0.1.0-preview-msvc).
The launcher update preserves the accepted runtime and all menu/equipment fixes.

## Install and play

1. Download the portable ZIP from [Releases](https://github.com/lightnill/EnshroudedVRM/releases).
2. Extract the entire ZIP to a writable local folder outside the game directory.
3. Start Steam, open **EnshroudedVRM.exe**, select enshrouded.exe and your own .vrm,
   then click **Launch game**. One VRM in Avatars can be selected automatically.
4. Offline play is the tested path. Close the game before changing avatars.

Start through the mod launcher each time you want the replacement. Steam's normal
launch starts the unmodified game. To uninstall, close the game and delete the
extracted folder. No global installation or administrator rights are required.

## Compatibility and limits

Version 0.1.0-preview supports Steam build **26082781**, resource version **1273043**.
Other executable builds are rejected; do not bypass the check. Supports VRM 0.x/1.x
humanoid rigs within importer bounds. Large avatars may reduce FPS. Menu previews
use alpha cutout for blended materials. Multiplayer avatar synchronization and
native armor fitting are not included. Unusual hands and equipment need broader
testing. Read [known issues](KNOWN-ISSUES.txt) and [release notes](RELEASE-NOTES.md).

## Build, support and license

See [BUILDING.md](BUILDING.md) for tools and supported build targets. This curated
source excludes private testing assets and research tools. Runtime compatibility
data is available in the matching binary download. Keep your models and State
private; never attach private avatars, derived geometry or unredacted logs to issues.

Original code: **MIT**, Copyright (c) 2026 **lightnill**. Reuse with the license and
copyright notice retained. See [third-party notices](THIRD_PARTY_NOTICES.md).
Game and avatar rights remain with their owners. Unofficial; not affiliated with
or endorsed by Keen Games. Binaries are unsigned; no clean antivirus verdict is
claimed. See [SECURITY.md](SECURITY.md) for behavior and reporting.

Development included substantial AI-generated code under lightnill's direction
and testing. No ValheimVRM code or assets are included.
