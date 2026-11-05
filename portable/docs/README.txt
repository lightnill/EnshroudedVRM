# EnshroudedVRM 0.4.0

Play Enshrouded as your VRM avatar. By **lightnill**.

[Português (Brasil): instalação e ajuda](README.pt-BR.md). The player ZIP also includes **LEIA-ME-PT-BR.txt**.

**0.4.0 is still buggy and incomplete.** This is a feature release, not a claim of stability. The shaders are still being refined: reflections, matcaps, transparency and low-light shading can look wrong. **Lip sync/visemes for other players are not implemented.** Brief native-character fallbacks and loading or frame-time spikes can still occur.

Supports Enshrouded Steam build **26430625** only. Keep **0.3.3** for the previous build **26320373**; packages are not interchangeable across game updates.

## New in 0.4.0

- **Per-player VRMs in F8:** select You or a connected Player N and assign a local model. Other players default to no replacement. Choices are remembered per account on this PC, including across characters and reconnects. Different players can use different VRMs simultaneously.
- **Broader importer/material support:** standard glTF normal maps and supported VRM0/VRM1 MToon matcap/rim maps and masks, with supported UV transforms and generated tangents. Embedded 16-bit PNGs are decoded into the existing RGBA8 render format. This does not reproduce custom Unity shaders or every possible mask workflow.
- **Auto, Enhanced, Fast and Matte appearance presets**, with saved whole-model settings and per-material overrides. Auto selects supported enhanced effects when authored maps need them; map-free materials retain the fast path.
- **Larger-model handling:** remove the earlier small importer memory caps, show decoded texture/retained CPU-buffer estimates in F8, and prepare optional shader variants in the background. Structural, decoder, device and allocation limits still apply; loading an oversized model can exhaust memory or stall the game.
- **One-click local debug reports** with recent approved diagnostics, current settings, CPU/RAM/GPU information, rendering context and fresh in-game map coordinates when available. Optional screenshots require a preview and explicit inclusion. No automatic upload.
- Updated player/preview ownership, fog and building-rendering compatibility, equipment handling and reduced repeated ownership/driver work. These improvements do not guarantee a particular FPS.

## Install and play

1. Download **EnshroudedVRM-0.4.0.zip** from the [0.4.0 release](https://github.com/lightnill/EnshroudedVRM/releases/tag/v0.4.0). GitHub's Source code archives are for developers, not the player package.
2. Close Enshrouded. In Steam, choose **Manage > Browse local files** and find **enshrouded.exe**.
3. If another mod owns a **vulkan-1.dll** beside the game, stop: do not overwrite, rename or chain it. For an existing EnshroudedVRM installation, replace the complete package with the game closed and preserve your own VRMs.
4. Extract the ZIP and copy **vulkan-1.dll** and the entire **EnshroudedVRM** folder beside **enshrouded.exe**. Avoid a duplicated folder level. The Portuguese guide at the ZIP root is optional documentation.
5. Launch normally through Steam and press **F8** (or **Fn + F8** on some laptops). Choose a player, disable **No model replacement**, and use **Browse...** or the model list. The original character stays visible until the model is ready.

No launcher EXE, batch file, PowerShell startup script, Python, administrator access or separate mod loader is required. Do not use the older script launcher with this package.

    Enshrouded/
      enshrouded.exe
      vulkan-1.dll
      EnshroudedVRM/
        Runtime/
        Compatibility/
        Avatars/
        release-manifest.json

## Player models and appearance

**F8 > Avatar > Player** selects whose model the controls edit. **No model replacement** keeps that player native while remembering the selected VRM. Anonymous Player N labels may change between sessions; the remembered choice follows the account. Remote assignments affect world characters only. Your inventory/title previews remain local; survivor previews stay native when their ownership is resolved.

The mod only changes what **your client** renders. It does not send model files or assignments to other clients. Other players must configure their own installation to see their preferred replacements. Native armor is not fitted to VRMs automatically.

Appearance settings are remembered by model content, so an identical renamed file retains its settings. Settings are shared by every assigned copy of the same model. A material can inherit the whole-model preset, override it, or reset. Try **Fast** if Enhanced/Auto produces incorrect reflections or shading. **Matte is not guaranteed to remove every authored reflective effect.** Optional background preparation may temporarily keep a preview native or use the basic rendering path.

The optional **vroid-sample.vrm**, assembled by Lightnill using VRoid Studio defaults, is included unchanged and is not selected automatically. Its embedded permissions apply separately from the mod's MIT code license; see Avatars/README.txt.

## Voice to viseme

Enable Voice to viseme in F8. **Game voice** uses your local outgoing game-voice audio and requires a voice-enabled session; Single Player may not activate it. **Local microphone** works offline, follows Windows default input or a selected device, and is independent of the game's mute/push-to-talk. Strength, sensitivity and smoothing are adjustable; no manual vowel calibration is required.

Voice always animates **You**, regardless of the selected Player control. **Other players' voice-driven visemes/lip sync do not work yet.** Processing is local; the mod does not record/upload audio or transmit additional viseme data. The game's own voice networking is unchanged.

## Settings and updating

Settings and bounded local logs live in **%LOCALAPPDATA%\EnshroudedVRM\State**. Replace all package files together; do not mix Runtime, shaders, Compatibility or manifests from different releases. Leave a moment after edits for background saving. Preserve models and back up settings before downgrading.

Private per-account choices live in **player-model-assignments.json**, using installation-specific keyed tokens and local paths rather than raw account IDs. Do not share that file or raw runtime logs. Use the F8 debug report instead. If saving fails, the menu warns and session choices can still work without persistence.

For a first import from an old script installation, only avatar-settings.json and mod-settings.json from the old State folder are imported via the game-local EnshroudedVRM/State directory. Existing user-folder settings win. The old launcher settings.json is not imported.

## Report a problem

Use **F8 > Create debug report**, then **Open reports folder**. The ZIP is saved under **%LOCALAPPDATA%\EnshroudedVRM\State\Reports**. It reconstructs allowlisted diagnostics; it does not attach raw logs/settings, account/server identifiers, personal paths, microphone IDs, audio, model files or memory dumps.

Reports include recent timing/fallback counters, selected settings, hardware/model statistics, observed terrain/building/depth/fog context and current **Enshrouded map coordinates** when a fresh qualified local-world sample exists. No real-world location, save files or actual voxel blocks are collected. Missing fields are marked unavailable; a partial report can still help. Reports record the selected voice source at capture time, not complete voice-source history.

Screenshots are off for each new report. If enabled, inspect the exact game-image preview and choose **Include this screenshot** or **Save without screenshot**. Visible names/chat remain visible in an included image. Review the ZIP before sharing it manually; nothing is sent automatically.

## Current limits and validation

Desktop and laptop checks on the supported game build confirmed world avatars, equipment and simultaneous two-player replacements. Desktop checks also covered F8, inventory/survivor previews, reload and local game-voice lip sync. This is limited manual coverage, not a stability or performance guarantee. Short fallbacks, initial preparation delays and frame spikes remain. The laptop report omitted graphics settings, so those runs are not a matched performance comparison.

Enhanced rendering approximates supported materials in Enshrouded's lighting. Full Unity/MToon rendering, custom shaders, outlines, UV animation, extra gloss/roughness-mask models and remote lip sync remain incomplete or unsupported. Reflective surfaces can remain too bright in dim scenes; some nighttime underlighting also exists on the original character. See [known issues](KNOWN-ISSUES.txt).

The current bootstrap has validated the default **C:\Windows** system-loader location on Windows x64. Other Windows-directory layouts are unsupported; another loader loaded first can bypass it. Unsupported game versions deliberately keep the native path. Do not disable antivirus or compatibility checks to force loading.

## Remove / developer information

Close the game and remove only this package's vulkan-1.dll and mod files, preserving personal VRMs. User-folder settings remain. The mod does not replace the game's executable/data files.

See [release notes](RELEASE-NOTES.md), [building](BUILDING.md), [security](SECURITY.md), [notices](THIRD_PARTY_NOTICES.md), and exact-file verification under **validation/0.4.0**. Runtime/release version is **0.4.0**; the retained Microsoft-built bootstrap keeps its independently versioned, previously tested bytes. This unofficial mod is not endorsed by Keen Games.
