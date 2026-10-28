# EnshroudedVRM 0.1.1 preview

By lightnill. Windows x64; readable CMD/Windows PowerShell launcher and the
required native VRM runtime DLL. No avatars or game executables are bundled.

- After first setup, use the generated **Play-EnshroudedVRM.cmd** to start with
  saved game, avatar and spring settings without selecting files or typing.
- Steam game discovery checks local library installations. Setup asks for the
  executable only when it cannot find a usable game path.
- Startup observation records launch failures and early exits without retrying
  or closing the game. Only the latest five recognized sessions are retained;
  game-log tails are limited to 256 KB and completed mod logs to 1 MB.
- Standard unfaded VRoid MToon `_FaceBrow_` overlays use the tested 0.5 alpha
  cutout path in the world, correcting the eyebrow shading mismatch. Other
  materials, including blended eyes and glasses, retain their existing behavior.
- Initial single-player checks passed with EML 0.2.0 and ShroudForge 1.7.5,
  each tested separately alongside VRM. ShroudForge's Hands-Free Lantern 0.11.1
  also passed the tested basic-lantern gameplay checks. No loader or lantern mod
  is bundled, and direct loader integration remains deferred.

Extract the whole ZIP outside the game folder, start Steam, and run
**Launch-EnshroudedVRM.cmd** once. Choose your avatar, confirm the detected game,
and type Y. Use **Play-EnshroudedVRM.cmd** afterward. Run Launch again to change
avatars or repair missing paths. Existing package and executable checks remain.

The runtime DLL is rebuilt for this release. All 13 shaders and compatibility
data retain their accepted bytes. This is a targeted VRoid material compatibility
rule, with hard cutout edges; full MToon and general shaded transparency remain
future work. The original avatar file is never edited.

Supported game: Steam build **26082781**, resource version **1273043**. Other
executables are rejected. Windows 10/11 x64 and Windows PowerShell 5.1 are required.
Only you see the replacement; multiplayer avatar synchronization is not included.
Large avatars and unusual equipment/rigs need broader testing. See KNOWN-ISSUES.txt.

Verification records identify this release's exact files. Previous antivirus
reports apply only to their original hashes; they do not validate the rebuilt
DLL or other changed bytes. Retain LICENSE and third-party notices.
