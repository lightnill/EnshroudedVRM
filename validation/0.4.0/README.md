# EnshroudedVRM 0.4.0 verification

User-run desktop world/equipment/F8, inventory-survivor distinction, reload and local Game voice checks passed. Simultaneous two-player VRMs confirmed on desktop and laptop.

Retained evidence for the unchanged implementation; not 146 newly rerun release tests. Version-resource-only PE comparison and exact source/shader/profile/catalog checks bind this release to the tested candidate.

20 release package checks, two targeted encoding/annotation reruns and three actual constructor cases passed. The release retains the accepted bootstrap, SDK, sample, 21 shaders and exact profile/catalog, including all 21 owner-prefix annotations. Only the runtime version-resource section changed. Original Portuguese instructions are retained unchanged in the bundled guide with validated UTF-8; the repository README.pt-BR adds the correction notice.

Still buggy/incomplete; shaders need refinement; remote lip sync/visemes unimplemented. Brief fallbacks and initial/load/frame-time spikes remain. Laptop graphics settings unavailable in one report, with only a short resolved two-player window. Not a controlled performance comparison or a stability/FPS guarantee.

Bitdefender: zero remaining issues/unscanned objects across 54 exact-file scans. Separate VirusTotal reports and dates are recorded in SCAN-RESULTS-0.4.0.json. VirusTotal cannot fully inspect this multi-file archive: its upload page requires an archive within 3 MB containing exactly one file for complete inner-file scanning. Runtime/bootstrap/SDK have separate exact-hash reports; Bitdefender scanned every packaged member and the complete ZIP. Unchanged bootstrap and SDK reports retain their original analysis dates. Unsupported engines, failures and timeouts are coverage gaps. Zero detections do not guarantee safety.

No private reports/screenshots/raw logs, account identities/tokens, State, paths, audio, game images or personal avatars published. Sole authorized bundled VRoid sample retained unchanged.

## ZIP metadata correction

**ZIP packaging correction (November 3, 2026):** the current 0.4.0 download uses the fixed release timestamp and Windows archive attributes. All 52 file contents, including every DLL, shader and the sample, are unchanged. A clean laptop install using 7-Zip worked; the original startup cause remains unconfirmed because old files were also removed. Close the game, extract to an empty temporary folder, then copy the complete package beside enshrouded.exe. Preserve personal models/settings and other mods. See validation/0.4.0/archive-metadata-correction.json for provenance and updated checksums.

The original release had 20 packaging checks; the corrected packaging source passes 21, including the new all-member timestamp/attribute regression. Fresh Bitdefender scans cover the corrected archive, runtime and 52 members. The exact corrected ZIP has zero VirusTotal detections: 64 undetected, three failures and eight unsupported engines. Original per-DLL reports remain applicable because bytes did not change.
