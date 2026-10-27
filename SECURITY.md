# Security and behavior

The launcher is a readable CMD wrapper, Windows PowerShell entry point and module.
The game loads the required native Vulkan layer through its child-process
environment. The launcher does not attach to another running game process.
Close an existing game before launching.

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

Report exact versions, file hashes and detection names at
https://github.com/lightnill/EnshroudedVRM/issues . Do not attach private avatars,
derived geometry or unredacted logs. Do not disable antivirus or signature checks.
Nexus performs its own checks; individual scan results do not establish its approval.
