# EnshroudedVRM 0.6.1

For Enshrouded on Windows x64, Steam build **26430625**. Download **EnshroudedVRM-0.6.1.zip** under Assets for manual or Vortex installation.

### Fixed in 0.6.1

- The optional `dinput8.chainload.dll` now starts independently of the game version, Steam build ID or executable hash. The avatar renderer still checks for the supported game build.
- VRM 0.x ribbons can recover their authored motion when an invisible helper chain has one matching chain driving the visible mesh. Ambiguous chains keep their authored setup.

### Also included for players updating from 0.5.0

- MToon is the default for new profiles at the title screen and in the world. Black outlines are optional, and individual materials can use the model’s outline colour. Saved profiles carry over.
- Avatar fallback and recovery run per player. The renderer reuses GPU resources, and pose decoding starts earlier in the frame.
- **Forget all models** clears the F8 dropdown and preserves loaded avatars, settings and assignments. F8 includes a title-screen flicker fix, and its debug reports identify the running package and runtime.

### Performance

The mod uses CPU time on the game’s main render thread even with avatar replacement turned off, so FPS can be lower than in the unmodded game. Displaying an avatar can lower it further. If your frame rate fluctuates and your PC can sustain 30 FPS but struggles to hold 60, try a **30 FPS cap** for steadier motion. If you already hold a higher frame rate consistently, keep that target.

**Upgrade.** Back up your VRMs and `%LOCALAPPDATA%\EnshroudedVRM\State`, close Enshrouded, and update the complete package. Preserve any existing `dinput8.chainload.dll`. You can keep your game saves and shader caches. Restore your 0.5.0 settings backup before rolling back to that version.

**Known issues.** Running Enshrouded as administrator and playing with HDR enabled are both unsupported in 0.6.1. Launch the game normally and use SDR (HDR off).

Frame generation is unsupported in 0.6.1. With RTX Encore enabled, avatar replacement can fail and the game can crash on exit. MToon highlights and low-light shadows still vary by model. Lip sync is local-player only.
