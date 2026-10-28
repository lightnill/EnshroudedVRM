# Loader coexistence

The one-click update retains the game fingerprint check and the existing Vulkan
loading route. The launcher does not install or reconfigure another loader.
Initial live Windows coexistence tests were performed on 2026-10-27 with the
unchanged one-click r3 candidate. These results cover the combinations below,
not general compatibility with every mod hosted by those loaders.

| Combination | Current evidence | Remaining acceptance |
| --- | --- | --- |
| VRM launcher with adjacent DLL/mod files | Synthetic child starts; stand-in files remain byte-for-byte unchanged | Covered by the real loader tests below |
| VRM + EML 0.2.0 | EML executes a print-only Lua probe; generated Play starts without configuration input; VRM replacement visible at menu and in the local test world | Gameplay-changing EML mods and overlapping resource changes |
| VRM + ShroudForge 1.7.5 | Enabled runtime probe loads and executes its update callback; native runtime and resource service become ready in-world; VRM replacement remains visible; F9 UI opens | Gameplay-changing mods and overlapping native hooks |
| EML + ShroudForge together | Not tested; both packages ship overlapping proxy DLL filenames | A separate stacked-loader test if required |
| Hands-Free Lantern 0.11.1, Nexus 235 | Basic Lantern crafting/equipment, light toggle, save reload and VRM movement checks passed with ShroudForge 1.7.5; see [mod-specific results](hands-free-lantern.md) | Higher lantern tiers, other outfits/avatars, controller input and multiplayer |

The test game was Steam build **26082781**, resource version **1273043**, with
EXE SHA-256 `d64966e23b1a458ee6f147bcbceac0b577fa7e39a212f371287d8cc119817882`.
EML was pinned to `v0.2.0` (`16708594d686b030d909f2d835b199d879ea9ad8`);
ShroudForge was pinned to `v1.7.5-build.39957852083`
(`46b59da1b3642f91fee3808c513efb996ec33f23`). Official download hashes and the
seven valid live-case receipts are summarized in
[results.json](../../validation/loader-coexistence-20261027/results.json).
The runtime DLL remained
`17695426ecfbe188db42709b6dd70b7e629b9dfe34b976facaf210cd5a96d045`.

Each loader was first launched without VRM, then through the generated Play
script. All seven valid cases remained running for the 35-second observation.
The ShroudForge probe reported an executed update callback, an empty runtime
error map, and ready native/resource services in-world both without and with
VRM. Before a world is loaded, an unavailable runtime/world context is expected
in these tests; that status cleared when the active-probe world loaded.
The user confirmed normal walking, jumping, weapon swings and held-item behavior
in both the EML + VRM and ShroudForge + VRM sessions. World-to-menu return in the
ShroudForge + VRM session retained the VRM avatar.

ShroudForge's bundled gameplay mods were excluded. Automatic updates, exports,
the debug console and continuous diagnostics were disabled for this test setup.
The probes do not modify resources, player attributes, inventory or world data.
The first EML fixture attempt had its script in the wrong directory; the first
ShroudForge probe was discovered but disabled. Both fixture issues were corrected
and those attempts are excluded from the valid-case count.

EML rewrote the KFC index despite reporting zero patches. Its finalizer also
created nine zero-byte archives and appended one empty 4096-byte resource chunk.
The rewritten index did not prevent the retained build check or VRM startup.
The original index was backed up before testing and restored before switching
to ShroudForge. Final cleanup restored that exact index backup, removed only the
verified empty resource append, and set aside the empty archives. The original
58 resource chunk records and their end offset establish the append boundary;
the resource prefix was preserved. An independent pretest resource-file hash
was not captured, and the cleanup receipt records that limitation. Loader
DLL hashes remained unchanged. ShroudForge updates its own configuration/state
as part of normal operation, so a changed configuration hash alone does not
establish that our launcher modified it. Private saves and raw logs stay in the
local test workspace, outside the sanitized report.

Both test loader setups were disabled at the end. The game EXE and local
configuration hashes match their pretest values. Test save progress was retained,
with the pretest save backup kept separately. No VRM production code changes
were needed for this initial pass. Future EML experiments should back up both
the KFC index and resource file before first load, including when the probe has
no resource changes. EML's finalization behavior is visible in its pinned
[writer implementation](https://github.com/Brabb3l/kfc-parser/blob/v0.2.0/crates/kfc-base/src/container/writer.rs).

For further combinations, record the exact supported game and loader versions,
the participating mods, launch path and fresh log results. First verify that the
loader and a representative mod work on their own. Then use the generated Play
script and verify both the same mod effect and the VRM avatar, menu/world
transitions, spring settings and held-item alignment. Close the game normally and
repeat Play without selecting paths or entering text. Confirm the loader's files
and configuration remain untouched by our launcher.

Do not equate a loaded DLL with a working mod, or a successfully started process
with correct rendering. Report overlapping engine hooks separately from startup
problems. Graphics-layer discovery/order work remains a separate milestone.
Testing each loader individually does not establish that both loaders work
together in the same installation.

Deferred, low-priority follow-up: investigate direct EML and ShroudForge
integration to simplify installation and potentially support normal Steam/game
launches without the separate VRM Play script. Feasibility, supported loader
hooks and Vulkan initialization timing still need investigation. Keep the
validated Play-script route for the current release; no integration work is
scheduled as part of the completed compatibility tests.

Primary loader references:

- [EML 0.2.0 release](https://github.com/Brabb3l/kfc-parser/releases/tag/v0.2.0)
- [EML usage guide](https://brabb3l.github.io/kfc-parser/eml/usage.html)
- [ShroudForge 1.7.5 release](https://github.com/bonsaibauer/shroudforge/releases/tag/v1.7.5-build.39957852083)
- [Hands-Free Lantern](https://www.nexusmods.com/enshrouded/mods/235)
