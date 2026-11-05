# EnshroudedVRM 0.5.0

For Enshrouded Steam build **26430625** only.

- Replaced the game-local Vulkan bootstrap with **dinput8.dll** and normal Steam startup.
- Added optional **dinput8.chainload.dll** support for a preserved existing input loader, with Windows input fallback and bounded startup diagnostics.
- One complete player ZIP supports manual installation and Vortex. Existing **EML dbghelp.dll** can remain installed; the VRM runtime starts independently.
- Ported the loader, runtime and native tests to Visual Studio 2022 / MSVC 14.44 with the static runtime.
- Updated native compatibility checks for the current game build.
- Retained per-player VRMs, supported enhanced MToon materials, appearance presets, local voice animation and privacy-conscious F8 debug reports.

**Updating:** close the game, preserve your own VRMs/settings, and disable/remove the older EnshroudedVRM package. Remove the game-local vulkan-1.dll only if it belongs to an older EnshroudedVRM installation. If another mod owns dinput8.dll, preserve it as dinput8.chainload.dll before extraction; do not overwrite an existing chainload file or rename an unidentified DLL. Replace an older EnshroudedVRM input DLL directly. Extract the complete ZIP beside enshrouded.exe, or install/enable/deploy that ZIP through Vortex. No older release files are required.

Launch normally through Steam and press F8 (Fn + F8 on some laptops). Choose You or a connected Player N, turn off No model replacement and select a .vrm. Choices affect your client only.

The implementation was checked on desktop and laptop, including two-player replacements, equipment, restart/reconnect and disconnect. Release-native version stamps changed; fresh build, byte-comparison, packaging and scan evidence is recorded under validation/0.5.0. Historical 0.4.0 evidence is retained separately. This is limited coverage, not a stability or FPS guarantee.

Known limits: shaders, transparency, reflections and low-light appearance still need work; brief native-model fallbacks and stutters can occur. Other players' voice lip sync, arbitrary custom shaders and direct EML-hosted loading are not implemented. BuildingTools initialized when chained but disabled its own features on this game build, including without EnshroudedVRM. Other Windows-directory layouts than C:\Windows are unsupported by this bootstrap.

Use F8 > Create debug report for support and review the report before sharing. Settings/reports/logs live in %LOCALAPPDATA%\EnshroudedVRM\State. Reports do not upload automatically. Do not share personal models, raw logs or account assignment/settings files publicly.


The input loader is retained byte for byte from the tested candidate (SHA-256 0ae1e53c29f25c81aadf017fa9ed74550980036823284020d4cbe38114761386). It keeps its independently versioned 0.4.0-input-test resource metadata. The package and runtime are version 0.5.0; no files from an older installation are required. Exact-hash scan reports and coverage gaps accompany this release.
