# Security and behavior

The current preview uses a readable CMD wrapper and Windows PowerShell 5.1. The
game loads the required native Vulkan layer through its child-process environment.
The launcher does not attach to or inject into another running process. Close an
existing game before launching. Earlier launcher EXE builds are legacy releases.

The three launcher files are included in the package and in `portable/script`.
PowerShell compiles a small read-only Windows filesystem helper locally using
its installed .NET tooling. The scripts respect the existing execution policy
and do not change security settings. A restrictive policy may prevent them from running.

The runtime operates inside the game process. It observes graphics resources and
uses validated, build-specific in-process equipment callbacks and bounded visual
slot writes for held items. These are modding techniques that security products
may inspect. They are documented rather than hidden or packed.

The launcher runs with the current user's privileges. It makes no
registry writes, scheduled tasks, services, global environment changes, security
exclusions, game-file replacements, network requests, telemetry or auto-updates.
Steam discovery reads local library information. The game/Steam retain their own
normal behavior, which is outside this launcher's network claims.
For Unicode extraction folders, the launcher may query an existing Windows short
name for Vulkan layer discovery. It verifies that alias resolves to the same
folder; it does not create links, move files or change filesystem settings.

Settings and bounded diagnostic text stay in the extracted folder's State
directory. Automatic graphics-buffer captures, model exports, anchor capture and
command traces are disabled. Review any log before choosing to share it; avatar
titles or paths may identify private models. Nothing is uploaded automatically.

The package has exact file hashes. Check them against the maintainer's published
release hash. A hash verifies bytes against that reference;
it is not a signature, malware verdict or proof of safety. This build is unsigned.

If a security product flags it, stop and inspect the detection and source. Report
the exact version/hash and detection name to the maintainer or the vendor's false
positive process. Do not disable antivirus, SmartScreen or signature enforcement.
No clean VirusTotal result, code signature or platform approval is claimed unless
the release's validation record explicitly documents one.

Report issues at https://github.com/lightnill/EnshroudedVRM/issues .
Include version, game build and detection name; do not attach private avatars or
unredacted logs.

## Current script package scan record - 26 October 2026

The 39 files in the current CMD package were checked individually on VirusTotal,
with zero reported detections in all 39 exact-hash reports. Counted engine
coverage ranged from 53 to 70 depending on file type. Unsupported types,
timeouts and failures are coverage gaps, not clean engine verdicts. Local
Bitdefender also completed 39 separate file scans with no issues or skipped objects.

See the [per-file hashes, report links and coverage](validation/script-launcher-20261026/PER-FILE-SCAN-RESULTS.csv)
and [final byte verification](validation/script-launcher-20261026/final-verification.json).
The ZIP SHA-256 is
`3b7865eb7b0a67971310eb1db83a4c13f66555be7920e49342f79a4bde4f61bb`.
All 17 runtime, shader and compatibility files are unchanged from the accepted
MSVC package. The author confirmed that the CMD package launches the game correctly.

Nexus's automated checks quarantined this package as file 1331. Nexus has not
identified the specific triggering file or check. Individual scan results do not
establish platform approval; manual review is still required. No antivirus
exclusions, obfuscation or scanner-specific binary changes are used.

## Legacy MSVC launcher scan record - 26 October 2026

For launcher SHA256
86e0b708c90cdb54b503e9d2857b078520e421f9494ac285b88029cf5cec9cb9,
VirusTotal reported 1/69 (Trapmine: Malicious.high.ml.score). Microsoft, Bkav Pro
and BitDefender reported Undetected. This is a standalone launcher scan, not a
complete-package verdict. The specific cause of Trapmine's score is unconfirmed.
The working runtime and shaders are unchanged. Review the exact file and source
if a security product flags it; no antivirus exclusions are required or advised.

https://www.virustotal.com/gui/file/86e0b708c90cdb54b503e9d2857b078520e421f9494ac285b88029cf5cec9cb9/detection
