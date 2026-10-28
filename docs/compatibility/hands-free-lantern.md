# Hands-Free Lantern coexistence

Initial single-player checks passed on 2026-10-27 with the unchanged one-click
r3 VRM runtime, ShroudForge 1.7.5 (build 39957852083), and Hands-Free Lantern
0.11.1 (Nexus file 1339). The game remained Steam build 26082781, resource
version 1273043. No compatibility build or native VRM changes were required.

| Check | ShroudForge + Lantern | ShroudForge + Lantern + VRM |
| --- | --- | --- |
| Startup | Running through the 35-second observation | Generated Play script; running through the 35-second observation |
| Mod content and native plugin | All lantern items present; plugin 0.11.1 loaded | Same; both native DLLs observed in the game process |
| World runtime | Ready; no reported runtime errors | Ready; no reported runtime errors |
| Crafting and equipment | User crafted the basic Lantern and equipped its added slot | Equipped Lantern restored from the baseline save |
| Belt model and K light toggle | User confirmed working | User confirmed working on the VRM avatar |
| Walking, jumping and weapon swings | No separate baseline movement comparison recorded | User confirmed normal behavior and no new clipping or floating |

The first installation displayed ShroudForge's restart-required asset status.
A normal restart was performed before baseline functionality was verified.
The plugin then reported all lantern items present. Actual world behavior,
the native plugin log and the user's checks establish the result above.

Before the first lantern launch, the character/world save folders were copied
and all 51 copied files were hashed. The original KFC index, KFC resource file
and local game configuration were also copied and verified. The backups remain
in the private local test workspace. After the checks passed, the user requested
a return to the pre-lantern backup. The original character/world save trees and
game asset files were restored and verified, and the temporary ShroudForge and
Lantern installation was moved into a disabled recovery folder. All 51 restored
save files match the pretest snapshot, with no extra active save files. The VRM
launcher and settings were preserved. Both the pretest backup and the later test
state remain recoverable.

The tested path is the existing generated `Play-EnshroudedVRM.cmd`. ShroudForge
and Hands-Free Lantern are installed in the game folder; VRM uses its existing
portable Vulkan loading route. The original build check remains enabled.
Only Hands-Free Lantern was enabled as a gameplay mod. Its original source and
DLL were not modified; an ordinary ShroudForge enable setting was added.

Evidence is summarized in
[results.json](../../validation/hands-free-lantern-20261027/results.json).
Raw logs, save data and avatar screenshots are excluded from that report.
Each saved test log is bounded to 256 KiB; the VRM launcher retains five session
groups. The HFL archive SHA-256 is
`2d1da0be8189f1637e8dad146182d7a7e4db38c7075fdc44837a93aeed00ac26`.

This pass covers the basic Workbench Lantern with the selected test avatar and
outfit. Traveler/Wisp lanterns, optional dim mode, controller input, other
avatars/outfits, multiplayer, long play sessions and uninstalling with mod items
in the save remain untested.

Official mod page: [Hands-Free Lantern](https://www.nexusmods.com/enshrouded/mods/235).
