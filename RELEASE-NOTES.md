# EnshroudedVRM 0.2.0

- Audio-driven lip sync with automatic setup, mouth strength/sensitivity/smoothing controls and supported silence/neutral handling.
- Game voice chat input, plus an offline local-microphone option with Windows default input and device selection.
- Movable dark F8 mod settings with immediate startup: no avatar picker or typed confirmation before the game. Select/configure the avatar in F8; the original character remains until selection.
- In-game VRM switching with validated preparation and safe GPU resource retirement.
- Game lighting and Matte whole-model presets, individual material overrides, inheritance/reset and persistent per-model profiles.
- Faster owner-snapshot sharing, fingerprinting and bounded diagnostics. Model switches retain the tool hook instead of restoring/reinstalling it for every avatar change. No model cache was added.

0.2.0 is a local-avatar feature release. Multiplayer synchronization, a signed EXE launcher, full MToon and native armor fitting remain future work. The current CMD/PowerShell bootstrap still loads the mod; all ordinary avatar/voice/appearance setup is in F8.

Game voice requires an online-capable session with active voice chat; Local microphone supports offline use. Voice is opt-in. Windows 10/11 x64, PowerShell 5.1 and Steam build 26082781/resource 1273043 remain required. No game executable, avatar or private state is included.

R9's model-switch/shader feature set was manually accepted by the maintainer. The 0.2.0 startup change is verified with isolated renderer, service and real child-process launcher tests; scan and final build validation receipts accompany the release. Component timings and reference-lifetime checks do not establish an in-game FPS gain or a whole-process memory bound.
