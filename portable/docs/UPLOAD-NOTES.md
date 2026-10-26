# Release upload notes

Author: **lightnill**. Version: **0.1.0-preview**. Original code: **MIT**.

Use only the final allowlisted portable/source ZIPs and SHA256SUMS.txt. Never upload
an extraction used for playing: it can contain private avatars, State and logs.
No avatars are bundled, including public samples or diagnostic hand variants.

For Nexus, apply **AI-Generated Content** for substantial AI-generated code and
**AI Media** if using the supplied AI-written page description. Select manual/
portable installation. State Windows x64, exact supported Steam build, local-only
appearance and the known limitations. Do not promise universal FPS gains or avatar
synchronization. This is an unofficial mod, not endorsed by Keen Games.

Game terms and platform acceptance are separate from this project's MIT license;
neither publisher approval nor platform preapproval is claimed. Preserve third-party
notices. Binaries are unsigned and no clean scanner verdict is claimed. Do not pack,
obfuscate, password-protect or advise antivirus exclusions to evade platform review.

GitHub: the original curated preview was published under v0.1.0-preview.
Keep that tag/downloads intact; publish the MSVC update described below as a
separate prerelease, attaching its matching portable/source ZIPs and SHA256SUMS.txt.
The source tree excludes the user's working directory and avatar-derived fixtures.

Policy references:
- https://help.nexusmods.com/article/28-file-submission-guidelines
- https://help.nexusmods.com/article/128-anti-virus-false-positives
- https://docs.github.com/en/site-policy/acceptable-use-policies/github-active-malware-or-exploits

The MSVC launcher update uses tag v0.1.0-preview-msvc, with
EnshroudedVRM-0.1.0-preview-msvc.zip, the matching -source.zip and SHA256SUMS.txt.
It preserves the original release and uses the exact maintainer-tested launcher.
