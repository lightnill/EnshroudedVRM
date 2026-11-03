EnshroudedVRM 0.4.0 contains native DLLs, including an unsigned project
bootstrap named vulkan-1.dll. Its version metadata identifies EnshroudedVRM and
lightnill. It forwards Vulkan graphics API entry points to the actual system library;
it is not a redistributed Microsoft DLL. The bootstrap is built with Microsoft
C++ tools and uses native PE forwarding to C:\Windows\system32\vulkan-1 on this
release. Nondefault Windows-directory layouts are unsupported. An
already-loaded system loader can bypass it. No loader-lock resolver, worker,
entry-point patch or helper process is added.

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

Debug reports reconstruct allowlisted fields, not raw files. In-game map position is permitted when fresh; no real-world location source is queried. Screenshots are off until explicitly requested, previewed and included. Names/chat in the approved image remain visible. No automatic uploads, audio recordings, model transfers or remote-viseme networking are added. Exact-file scan evidence is in validation/0.4.0; unsupported engines and archive inspection limits remain coverage gaps.
