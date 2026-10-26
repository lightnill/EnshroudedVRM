# Security and behavior

This preview uses a normal native Windows launcher and a Vulkan layer loaded by
the game through its child-process environment. The launcher does not attach to
or inject into another running process. Close an existing game before launching.

The runtime operates inside the game process. It observes graphics resources and
uses validated, build-specific in-process equipment callbacks and bounded visual
slot writes for held items. These are modding techniques that security products
may inspect. They are documented rather than hidden or packed.

The launcher requests the current user's privileges (asInvoker). It makes no
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
unredacted logs. Local antivirus scanning was unavailable on the build machine;
there is no claimed clean antivirus verdict.
