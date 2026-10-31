# Security and behavior

The launcher is a readable CMD wrapper, Windows PowerShell entry point and module.
The game loads the required native Vulkan layer through its child-process
environment. The launcher does not attach to another running game process.
Close an existing game before launching.

After a successful launch the launcher writes Play-EnshroudedVRM.cmd beside
itself. This readable local script starts the same PowerShell entry point in
saved-settings mode; it contains no avatar/game paths and makes no additional
downloads or installations. Existing unrelated files with that name are retained.
Play keeps the same game and package checks as setup.

The same launcher process observes up to 30 seconds of startup. It records
process-start failures, early exits and actual exit codes, and copies only a
bounded text tail from the chosen game's enshrouded.log into private State/Logs.
There is no resident helper, upload, game restart or process termination.
Five recognized session groups are retained. Game-log copies are capped at
262144 bytes; completed native logs are trimmed to 1048576 bytes on the next
launch. Attempt JSON is capped at 16384 bytes. Cleanup is flat and constrained
to regular owned files in the real State/Logs folder; links and unknown files
are left alone. Locked files can prevent immediate cleanup. The game log itself
is never edited. -Check never creates or prunes this history.

PowerShell compiles a small read-only Windows filesystem helper locally using
installed .NET tooling. The scripts respect existing execution policies and do
not change security settings. A restrictive policy may prevent them from running.

The runtime operates inside the game process. It observes graphics resources,
uses validated build-specific equipment callbacks and bounded visual-slot writes,
and renders the avatar. These modding behaviors are documented rather than hidden.

The launcher runs with the current user's privileges. It makes no global
environment changes, registry writes, startup tasks, services, antivirus exclusions,
game-file replacements, network requests, telemetry or auto-updates. Steam discovery
reads local information. Steam/game networking is outside the launcher's claims.

For Unicode paths, an existing Windows short alias may be queried and validated
for Vulkan layer discovery. The launcher does not create links or alter volumes.
Settings and bounded text logs stay in State. Automatic graphics-buffer captures,
model exports, anchor capture and command traces are disabled. Logs may identify
private avatars; review them before sharing. Nothing is uploaded automatically.

Release checksums identify exact bytes; they are not a signature or proof of safety.
The native runtime is unsigned. Individual-file reports and coverage are recorded
outside the package so publishing scan evidence does not change the scanned bytes.
Unsupported engines, failures and timeouts are coverage gaps, not clean verdicts.
Version 0.2.0 rebuilds the runtime and changes launcher/document/archive bytes.
Previous scan records identify only their original exact bytes. Use the new
release's verification record for current coverage; do not infer a clean verdict
for changed files from an earlier release's reports.

Report exact versions, file hashes and detection names at
https://github.com/lightnill/EnshroudedVRM/issues . Do not attach private avatars,
derived geometry or unredacted logs. Do not disable antivirus or signature checks.
Nexus performs its own checks; individual scan results do not establish its approval.

## Voice and settings in 0.2.0

Audio lip sync is opt-in. Game voice observes local outgoing voice blocks; the
offline option opens the Windows default or explicitly chosen capture device.
Analysis stays in process. The mod does not save audio recordings or send extra
viseme data over the network. The game's own voice/network behavior is separate.
Model loading and shader preferences use local files and background workers.
F8 stores bounded model/material preferences in State/avatar-settings.json.
The bundled official Oculus LipSync DLL is separately licensed; retain its notices.
Scan reports are provided beside the release and state exact hashes/coverage.
