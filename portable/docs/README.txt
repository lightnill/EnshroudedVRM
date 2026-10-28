EnshroudedVRM — readable CMD/Windows PowerShell launcher

Run Launch-EnshroudedVRM.cmd to select your avatar. A valid saved or automatically
detected game is selected for you; the game picker appears only when no usable
game path is available. Review the
paths and type Y before the first launch. After a successful launch, the launcher
creates Play-EnshroudedVRM.cmd beside itself. Use Play for everyday starts: it reads
your saved game, avatar and spring setting with no file pickers or text input.
Run Launch again to change your avatar or repair missing paths. Any required
file picker is prefilled from the previous selection.

Extract the whole package into a writable local folder outside the game folder.
Use a package-folder path without semicolons (;) or percent (%) signs. Windows
can expand percent signs before a CMD file even starts. Game/avatar paths may
still contain percent signs because they are read from saved settings.
Start Steam before launching. Close the game before changing avatars. Offline
play is the tested path; starting through Steam alone runs the normal game.
Windows 10/11 x64 and built-in Windows PowerShell 5.1 are required.

Keep Play beside the other launcher files. Moving the package alone works if the
saved game/avatar locations still exist. Play stops with a setup instruction if
settings are missing, invalid or point to missing files; it never silently picks
a different avatar. A visible error may need a keypress to close.

An existing Play file with different contents, or a folder/link with that name,
is left untouched. Rename that item and run Launch again to generate Play.
A generation warning after startup does not mean that the game failed to start.

The game starts immediately. This launcher window checks the first 30 seconds,
then closes automatically, or earlier if the game process exits. No extra input
is needed for a normal start. A process surviving this check is not a guarantee
that every loading or rendering step is finished. The launcher never retries,
restarts or closes the game automatically.

State/Logs keeps the latest five launch attempts. Each has a small .attempt.json
record, the mod's .log when written, and a .game.log startup tail when fresh game
log data is available. Game-log snapshots are capped at 256 KB. On the next launch,
completed retained mod logs are trimmed to 1 MB and older recognized attempts are
removed. The active mod log keeps its existing 10,000-line limit until then.
Unknown files, directories and links are left alone. Locked files can delay
cleanup and produce a warning. The game's original log is never changed.
Keep these local logs private; they may contain player, game and avatar details.

Launch-EnshroudedVRM.ps1 is the entry point. launcher_core.psm1 contains validation
and launch logic, including a small read-only C# filesystem helper compiled by
Windows PowerShell's installed .NET tooling. No custom launcher EXE is distributed.

PowerShell 5.1 is required. Existing script policies are respected; the launcher
does not change Windows security settings. Steam and the game retain their own
normal behavior. The mod changes only the child-process Vulkan environment.

Explicit command options include -Check, -Launch, -Play, -Wait, -NoSprings,
-EnableSprings and -Report. Use -EnableSprings to turn saved spring motion back on.
-Play requires complete saved settings and cannot be combined with -Check, -Launch,
game/avatar paths or spring overrides. -Wait and -Report work with -Play.
For validation without starting the game:
  .\Launch-EnshroudedVRM.ps1 -Check -Game "D:\Steam\enshrouded.exe" -Model "D:\Avatars\Avatar.vrm"

Settings and bounded text logs stay in State. Captures, model exports and command
traces remain disabled. The launcher refuses unsupported game builds, changed
package files and an already-running game. It makes no global installation,
startup or Steam launch-option changes. Nothing is uploaded automatically.

Initial single-player checks passed with EML 0.2.0 and ShroudForge 1.7.5, each
tested separately alongside VRM. Combined-loader and multiplayer support remain
untested; direct loader integration is deferred.
The launcher does not replace their DLLs or edit their mod folders. Unrelated DLLs
beside enshrouded.exe do not themselves fail the exact executable check. Mods
that patch the executable or the same runtime hooks may require compatibility
work. Graphics-layer discovery changes are outside this launcher update.

Original code is MIT, Copyright (c) 2026 lightnill. Preserve LICENSE and the
applicable dependency notices. The runtime DLL, shaders and compatibility data
retain their own release hashes and provenance.
