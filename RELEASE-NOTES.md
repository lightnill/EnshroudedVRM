# EnshroudedVRM 0.1.0 preview

## Current CMD/PowerShell distribution - 26 October 2026

The readable CMD/Windows PowerShell launcher is now the recommended download.
No custom launcher EXE is included. The required native VRM runtime, all 13
shaders and compatibility files are unchanged from the accepted release.
Earlier EXE builds remain in the release history as legacy versions.

Extract the entire CMD ZIP, start Steam, run `Launch-EnshroudedVRM.cmd`, choose
the game and your VRM, then review the paths and type Y. Windows PowerShell 5.1
is built into supported Windows versions; existing script policies are respected.

Twenty synthetic script-launcher regressions passed. The author also verified
a live game launch. All 39 packaged files have individual VirusTotal reports
with zero reported detections; unsupported engines and timeouts are recorded.
Local Bitdefender scanned all 39 separately with no issues or skipped objects.
Nexus nevertheless quarantined file 1331 and has not identified the trigger.

The script ZIP is byte-identical to the tested and scanned candidate:
SHA-256 `3b7865eb7b0a67971310eb1db83a4c13f66555be7920e49342f79a4bde4f61bb`.
See [the per-file record](validation/script-launcher-20261026/PER-FILE-SCAN-RESULTS.csv).

## Original portable preview

By lightnill. First portable preview for Windows x64 and the supported Steam build.

- Load your own VRM 0.x/1.x humanoid avatar locally.
- GPU skinning, lit rendering and spring-bone support.
- World, main menu, character selection and Character-panel previews.
- Held-item alignment for sword, shield, two-handed tools and bow draw; optional
  finger mappings with approximate wrist fallback when mappings are missing.
- Guarded compatibility checks; portable launcher with no global installation.

Menu previews approximate blended materials with alpha cutout. World transparency
is unchanged. Large models can reduce FPS; unusual rigs/equipment need broader
coverage. Other players do not see the VRM. See KNOWN-ISSUES.txt for details.

No avatars or game executables are included. Bring your own permitted VRM.
Current install: extract the CMD ZIP, start Steam, open Launch-EnshroudedVRM.cmd,
choose the game and avatar, then type Y. Offline play is the tested path. Launch
through the mod each time; launching through Steam alone runs the normal game.

Original code is MIT with attribution to lightnill; dependency notices are included.
This is unofficial and not endorsed by Keen Games. Binaries are unsigned; no clean
antivirus verdict is claimed. Development included substantial AI-generated code
under lightnill's direction and testing.

## Legacy MSVC launcher update - 26 October 2026

The launcher is now built with Microsoft Visual C++ and a static release runtime.
The executable is the same MSVC build verified in-game by the maintainer.
The accepted VRM runtime, all 13 shader modules and compatibility files are
byte-identical to the original preview, including the menu and equipment fixes.

The launcher is 520,704 bytes and remains unsigned. Core checks, four synthetic
child-process tests and the packaged compatibility/tamper checks passed. No
additional Visual C++ Redistributable is required by this launcher.

The standalone launcher's VirusTotal scan on 26 October 2026 reported 1/69:
Trapmine, Malicious.high.ml.score. Microsoft, Bkav Pro and BitDefender reported
Undetected. This scan covers the launcher alone and is not a clean verdict for
the complete package. The runtime loads inside the game and uses graphics hooks
and validated in-process patches; security products may inspect these techniques.

Launcher SHA256:
86e0b708c90cdb54b503e9d2857b078520e421f9494ac285b88029cf5cec9cb9

Scan: https://www.virustotal.com/gui/file/86e0b708c90cdb54b503e9d2857b078520e421f9494ac285b88029cf5cec9cb9/detection

The original v0.1.0-preview tag/downloads are preserved. This update uses the
v0.1.0-preview-msvc tag; the embedded launcher version remains 0.1.0-preview.
