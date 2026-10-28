# Preview update: saved launcher and eyebrow rendering

The user requested that the tested launcher improvements and eyebrow correction
be pushed to GitHub and included in a new release. Version: `0.1.1-preview`.

The one-click launcher, Steam discovery, startup observation, and retention of
five log sessions already exist on this branch. Preserve their build checks.
Keep loader integration deferred; document only the completed compatibility checks.

The eyebrow experiment changed only the standard VRoid `FaceBrow` material from
BLEND to MASK with cutoff 0.5. The user confirmed that the original world shading
problem disappeared. Equipment previews already use this cutout path.

## Approaches

Editing a player's avatar duplicates a private asset and does not fix the mod.
Changing every BLEND material affects eyes, glass, and intentionally translucent
surfaces. Implement the tested behavior as a narrowly scoped runtime import
compatibility rule for MToon materials with the standard `_FaceBrow_` name token,
BLEND alpha mode, and an unfaded base alpha of 1. This selection is an implementation
decision; the tested cutout behavior and publication are user-authorized.

Keep other materials and faded eyebrow materials unchanged. Use cutoff 0.5,
record an import warning when the compatibility rule is applied, and leave the
source VRM unchanged. The correction intentionally uses hard cutout edges and
does not implement full MToon or general shaded transparency.

## Release

Build and test the native runtime, retain the accepted shader and compatibility
bytes when unchanged, and assemble an explicit allowlist of public files.
No avatar, save, player settings, raw gameplay log, or private capture may ship.
Regenerate source and package checksums and current build metadata. Previous
antivirus records must not be represented as verdicts for changed bytes.
Publish a new GitHub prerelease after verification; retain the existing release.
