EnshroudedVRM 0.5.0 contains unsigned native DLLs. The original project
bootstrap is named dinput8.dll and its metadata identifies EnshroudedVRM and
lightnill. It forwards the six input exports to the genuine Windows input DLL,
or to an explicitly preserved dinput8.chainload.dll after admission. It is not
a redistributed Microsoft DLL. The supported system provider location is
C:\Windows\System32\dinput8.dll on Windows x64; other Windows-directory layouts
are unsupported. Existing EML dbghelp.dll can coexist independently.

The bounded loader arranges this process's Vulkan layer during process attach.
Optional chaining runs once at the supported game's verified startup call,
outside the loader lock. The temporary command-line import hook is restored
before chaining. No bootstrap worker thread or helper process is installed.
Loader, runtime and native tests use Visual Studio 2022 / MSVC 14.44.

Only current-process discovery is configured. No registry/global environment
changes, helper process, automatic update, game EXE/asset replacement, recording
or telemetry is installed. Normal game voice networking remains the game's own.

Package hashes, exact game attestation and existing owner/byte/lifetime guards
are checked before optional hooks. Hash manifests detect changes; they are not
a digital signature or a promise that antivirus products will accept a build.
Do not bypass game-version checks, disable antivirus, add broad exclusions or
overwrite a conflicting DLL. Use the exact-hash scan report supplied separately;
the package builder does not inherit earlier scan claims or certify its output.

Private settings/logs stay in the user folder. Import reads only the two known,
bounded preference schemas and preserves existing destinations/source files.
Source/test audio, private avatars and developer test executables are excluded.
The single creator-authorized vroid-sample.vrm is included unchanged, explicitly
hash/size-pinned during packaging and covered by the exact runtime manifest.
Third-party terms are listed in THIRD_PARTY_NOTICES.md and notices/.

The per-player model feature reads the game's account/actor association
inside this process to bind each optional replacement to its player. Persistent
choices use HMAC-SHA-256 tokens with a random installation key, not raw account
values or a globally comparable hash. The key, tokens and local model paths stay
in player-model-assignments.json in the private user State directory. This file
is not a debug-report or package input. Anonymous Player N menu labels are session
labels. No account data, assignments or models are transmitted by this feature.
Reports retain their existing field allowlist and omit these private fields.

Debug reports reconstruct allowlisted fields, not raw files. In-game map position is permitted when fresh; no real-world location source is queried. Screenshots are off until explicitly requested, previewed and included. Names/chat in the approved image remain visible. No automatic uploads, audio recordings, model transfers or remote-viseme networking are added. Exact-file scan evidence is in validation/0.5.0; unsupported engines and archive inspection limits remain coverage gaps.

The input loader is retained unchanged from the tested candidate, SHA-256
0ae1e53c29f25c81aadf017fa9ed74550980036823284020d4cbe38114761386.
Its original version resource remains 0.4.0-input-test; the package and runtime
are version 0.5.0. A clean rebuild of the retained loader source must reproduce
that exact hash. Exact-hash VirusTotal reports accompany the release. Timed-out,
failed and unsupported engines remain coverage gaps. Archive reports do not
establish that every contained file was inspected by every engine; the native
DLLs have individual reports and every package member was scanned locally.
