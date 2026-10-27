EnshroudedVRM 0.1.0 preview — by lightnill

Play as your own VRM avatar in Enshrouded. This is an early preview. The change
is visible only on your screen; multiplayer avatar support is planned but is
not implemented. No avatars or game executables are bundled.

1. Extract the whole ZIP to a writable local folder outside the game directory.
2. Start Steam, then double-click Launch-EnshroudedVRM.cmd.
3. Choose enshrouded.exe and your own .vrm file.
4. Review the selected paths, then type Y to start the modded game.
5. Use Offline mode for the preview's tested play path.

Close the game before changing avatars. Run the CMD each time you want the mod;
starting through Steam alone runs the normal game. To uninstall, close the game
and remove the extracted folder. No global installation is required.

Requires Windows 10/11 x64 and built-in Windows PowerShell 5.1. The scripts obey
existing execution policies; restrictive policies may block them. Security
settings are not changed. No administrator rights, Python or extra mod framework
is required for players.

The three launcher files are readable text. A native runtime DLL is required
for rendering. GPU skinning, lit rendering, spring motion, menu previews and
held-item alignment are included. Large models can reduce FPS. Read KNOWN-ISSUES.txt.

The launcher checks the supported game fingerprint and package file hashes
before launch. Keep the package complete. Settings and bounded text logs stay in
State. Automatic model exports, graphics-buffer captures, anchor capture, command
traces, telemetry and auto-updates are disabled. Nothing is uploaded automatically.

Keep avatars and State private. Never share private models, derived geometry,
screenshots of private models or unredacted logs in public reports.

Original code is MIT, Copyright (c) 2026 lightnill. Keep LICENSE and all third-party
notices. Game and avatar rights remain with their owners. This is unofficial
and not endorsed by Keen Games.

Source, support and build instructions: https://github.com/lightnill/EnshroudedVRM
