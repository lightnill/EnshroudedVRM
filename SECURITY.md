The 0.3.2 preview contains native DLLs, including an unsigned project
bootstrap named vulkan-1.dll. Its version metadata identifies EnshroudedVRM and
lightnill. It forwards Vulkan graphics API entry points to the actual system library;
it is not a redistributed Microsoft DLL. The bootstrap is built with Microsoft
C++ tools and uses native PE forwarding to C:\Windows\system32\vulkan-1 on this
preview. Nondefault Windows-directory layouts are unsupported. An
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

0.3.2 retains the original startup DLL and rebuilds only the avatar runtime.
Local fallback/CPU profiling logs contain bounded runtime diagnostic metadata;
they are not sent to a service automatically. Sparse CPU samples and overlapping
scopes are not GPU timings or proof of a performance improvement. Release scan
results are published separately for the exact DLLs and player ZIP. A missing,
pending or unsupported scan is not a clean result, and zero detections alone
does not establish safety. Any reported detection stops this release workflow.

0.3.2 debug reports are local, user-requested allowlisted exports. They do not
attach raw logs/settings, model files, audio, saves, memory dumps, personal paths,
account/server IDs or device endpoint IDs. The location field describes only
fresh qualified game-world coordinates; no real-world location is queried.
Screenshots start disabled for every report and need exact-image preview and
explicit approval. Included visible names/chat are not automatically redacted.
No automatic report upload or telemetry is added. Private preview-subject tags
are transient in-memory validation state and are not exported as identities.
