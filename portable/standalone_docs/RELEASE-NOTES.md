EnshroudedVRM 0.3.0 — standalone startup

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
