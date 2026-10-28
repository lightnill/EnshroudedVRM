# EnshroudedVRM 0.1.2 preview

Long hair has more natural inertia and damping during movement. Running produces
a smaller backward trail, and neighbouring sections share a little rotation to
keep gaps less visible. Hair permits arm overlap to reduce catching; retained torso/head colliders
still take priority. Shoulder contacts follow the model's collider references. Avatar files are
never edited. Enable spring motion to use this behavior.

The runtime also avoids rebuilding static skeleton calibration every frame and
reuses idle GPU buffers and descriptor resources. GPU skinning remains enabled
by default. Isolated tests measured skeleton updates at about 0.055 ms versus
1.15 ms, and greatly reduced GPU resource allocation/retirement costs. These are
processing measurements, not a measured in-game FPS increase. See the source
repository's docs/performance/0.1.2-optimization.md for scope and reproduction.

The approved hair tuning is preserved exactly in this optimization pass. Short
head strands can still jitter during combined running/blocking, and collision
coverage depends on the avatar's authored colliders. Broader rig/scene testing
is still needed.

Extract the complete ZIP outside the game folder, start Steam, and run
Launch-EnshroudedVRM.cmd once. Choose your avatar, confirm the game path and type
Y. Use the generated Play-EnshroudedVRM.cmd afterward. Existing one-click setup,
startup history, held-item alignment and VRoid eyebrow compatibility remain.

Windows 10/11 x64 and Windows PowerShell 5.1 are required. Supported game:
Steam build 26082781, resource version 1273043. Only you see the replacement;
multiplayer avatar synchronization is not implemented. No avatars, saves, custom
launcher EXE or third-party mods are bundled. Keep LICENSE and all notices.

Automated checks cover the launcher/package, 96 spring scenarios, cached pose
validation and output equality, rejected-view history preservation, and GPU
skinning/resource lifetimes. The optimized runtime was not newly tested in-game.
All 13 shaders and compatibility data retain their accepted bytes. This release's
verification and scan records identify its exact files; earlier antivirus reports
apply only to their original hashes.
