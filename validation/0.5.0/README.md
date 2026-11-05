# EnshroudedVRM 0.5.0 verification

The original input-loader implementation passed desktop/laptop Steam startup,
F8/model switching, actual Vortex deployment and EML 0.2.0 coexistence, plus
two-player model/equipment/reconnect/disconnect checks on Steam build 26430625.
BuildingTools initialized but disabled its features on this game build even
without this mod. Direct EML-hosted startup and remote voice lip sync are not
implemented.

The release was built twice with VS2022/MSVC 14.44. Runtime, loader and ZIP were
byte-identical between builds. The accepted loader is retained byte for byte. The runtime differs only in
version resources and standard linker identity/resource-size fields; executable
code and other data match. All 21 shader modules match and pass SPIR-V validation.
Fresh release checks: 28 input, 23 package and 19 native/runtime checks passed.
These include two actual rebuilt-runtime tests through the system Vulkan loader;
they do not launch the game or replace the earlier bounded live acceptance.

VirusTotal individually analyzed all three shipped DLLs and the exact ZIP.
The unchanged accepted loader, runtime, SDK and ZIP have zero malicious or
suspicious detections in their exact-hash reports. The retained loader keeps its
independent 0.4.0-input-test resource version and reproduces its accepted hash
in two clean builds. Engine gaps are retained in the reports.
Bitdefender found zero issues/unscanned objects across the final ZIP and all
52 extracted members. Engine gaps and the archive's inner-file inspection limit
are recorded in SCAN-RESULTS-0.5.0.json. Zero detections do not prove safety.

No private diagnostics, account data, settings, developer binaries, game assets
or personal models are included. The creator-authorized VRoid sample and all
applicable third-party notices are retained.
