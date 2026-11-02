EnshroudedVRM 0.3.2 â€” standalone preview

The release and avatar runtime are version 0.3.2. The bootstrap keeps its independent
0.2.0-standalone-dev file version and its previously tested DLL bytes. BUILD-INFO.json
records the runtime and bootstrap versions separately.

0.3.2 adds explicit local-player ownership, correct inventory/survivor previews,
ownership/driver-lookup optimizations and private one-click F8 debug reports.
Launch through Steam and configure in F8. See KNOWN-ISSUES.txt for coverage.

INSTALL
This preview is validated only with Windows installed in C:\Windows.
Do not install it on a different Windows-directory layout. Windows 11 and
other Vulkan loader versions have not been validated.

1. Close Enshrouded. In Steam, use Manage > Browse local files.
2. Check for an existing vulkan-1.dll beside enshrouded.exe. If one is already
   there from another mod, stop: do not overwrite, rename or chain it. Update
   only a known EnshroudedVRM installation with the game closed.
3. Copy these two entries from this ZIP beside enshrouded.exe:
     vulkan-1.dll
     EnshroudedVRM (folder)
4. Start Enshrouded normally through Steam. Press F8 for the mod settings.
   Choose an avatar using Browse, or put VRM files in EnshroudedVRM/Avatars
   and refresh the list. The included vroid-sample.vrm by Lightnill is available
   to try; it is never selected automatically. See Avatars/README.txt for terms.
   Enable No model replacement to use the game's original character. The switch
   and your VRM/shader choices are remembered across restarts.

Use an ordinary, non-administrator launch. No launcher EXE, batch file,
PowerShell startup command, registry installation or extra mod loader is needed.
Do not combine this runtime with files from the old script package.
If you installed this project's older private powrprof.dll test bootstrap,
remove that project-owned copy first. Leave other mods' DLLs alone.

Another component loading the system Vulkan DLL first can bypass this bootstrap.
Starting the game with a System32-only Vulkan load can also bypass it. This
preview does not patch the game or chain another loader to force activation.

SETTINGS AND UPGRADES
Preferences live at %LOCALAPPDATA%/EnshroudedVRM/State, independently of this
installation. Model selection and whole-model/per-material presets save there.
If saving is unavailable, live controls still work and F8 shows a save warning.

To bring preferences from 0.2.0, copy only avatar-settings.json and
mod-settings.json from its State folder into EnshroudedVRM/State beside Runtime.
Start normally once. Valid files import separately only when that preference
does not already exist in your user folder. Source files stay untouched.
Old launcher settings.json is not imported. If a model moved, choose it again
in F8; presets still apply to identical model content. Your saved menu key
continues to apply if it differs from the default F8.

VOICE
Voice to viseme is off by default. Game voice needs a session that enables the
game's voice chat. For offline use, choose Local microphone. It follows Windows
default unless you choose a device. Its Voice toggle controls local capture;
game mute/push-to-talk applies to the Game voice source. A previously saved
enabled setting is restored. The mod does not record or upload microphone audio.

IF F8 DOES NOT APPEAR
Check the two entries are beside the actual game EXE, the game build is
supported, and no conflicting bootstrap or user layer override is in use.
See KNOWN-ISSUES.txt. If the runtime never loads, it cannot show F8 or create
a fresh log. An older log is not evidence that this launch loaded the mod.
When the supported game reaches the runtime but package checks fail, a fresh
log records the decline reason. This does not enable avatar or voice features.
When available, the latest log is %LOCALAPPDATA%/EnshroudedVRM/State/runtime.log.

REMOVE OR UPDATE
Close the game first. Remove only this package's vulkan-1.dll and mod files.
Keep or move any avatars/import preferences you placed in EnshroudedVRM before
deleting that folder. Personal settings in your user folder remain. Never
delete another mod's files. Leave published 0.2.0 packages intact for rollback.

REPORT AN ISSUE
F8 -> Create debug report saves a local ZIP. Open reports folder opens its
location. Nothing uploads automatically. Reports contain allowlisted diagnostics,
settings, hardware and terrain/rendering context, with fresh qualified in-game
XYZ coordinates when available. Real-world location and voxel/save extraction
are never used. Screenshots are off; preview and explicit inclusion are needed.
Review any visible names/chat and the completed ZIP before sharing it yourself.
