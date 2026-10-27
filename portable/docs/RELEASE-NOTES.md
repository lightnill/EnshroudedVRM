# EnshroudedVRM 0.1.0 preview

By lightnill. The player download uses a readable CMD/Windows PowerShell launcher
and the required native VRM runtime DLL. No custom launcher EXE or avatars are bundled.

- VRM 0.x/1.x humanoid loading within importer bounds.
- GPU skinning, lit rendering and relaxed hair/clothing spring motion.
- World, main-menu, character-selection and Character-panel previews.
- Sword/shield, two-handed tool and drawn-bow alignment, with approximate wrist
  fallback when optional finger mappings are missing.
- Build-specific compatibility checks and portable local settings/logs.

Extract the complete ZIP, start Steam, run Launch-EnshroudedVRM.cmd, choose the
game and avatar, then review the paths and type Y. Windows PowerShell 5.1 is built
into supported Windows versions; existing script policies are respected.

This is an early preview. Only you see the replacement. Multiplayer avatar support
is planned but is not implemented. Large models may reduce FPS; unusual rigs and
equipment combinations need broader testing. Menu previews approximate blended
materials with alpha cutout; world blending is unchanged. See KNOWN-ISSUES.txt.

Runtime DLL, shaders, compatibility data and launcher logic are preserved from
the tested script build. Release source excludes retired launcher-only code and
build targets. License notices required by the runtime remain included.

Verification is recorded for the exact published package. Individual scan reports
are supplied separately; a ZIP-level verdict does not establish member coverage.
Original code is MIT, Copyright (c) 2026 lightnill. Retain dependency notices.
