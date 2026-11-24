# EnshroudedVRM 0.6.1

Press **F8 → Avatar → Player**, choose yourself or a connected player, turn off **No model replacement**, and browse to a .vrm file. Your choices affect what you see on your PC. Other players choose their own models.

## What’s new since 0.5.0

New avatars use MToon at the title screen and in the world. Choose MToon + Outline for black outlines, or use the model’s outline colour on individual materials. Existing profiles keep their saved appearance settings. The older Enhanced, Matte and Fast choices now appear as Extended Fast Gloss, Extended Fast Matte and Minimal, respectively.

If one player’s pose becomes unavailable, their character can temporarily return to the native model while the other avatars keep rendering. **Forget all models** clears the F8 dropdown. Loaded avatars, appearance settings and player assignments are preserved.

## Fixed in 0.6.1

Some VRM 0.x ribbons stayed rigid because their physics were attached to invisible helper bones. The importer now recovers the motion when it finds one matching chain driving the visible ribbon. The optional input chainloader can also start a companion DLL after a game update. Avatar replacement supports Steam build **26430625**.

## Performance

The mod uses CPU time on the game’s main render thread even with avatar replacement turned off, so FPS can be lower than in the unmodded game. Displaying an avatar can lower it further. If your frame rate fluctuates and your PC can sustain 30 FPS but struggles to hold 60, try a **30 FPS cap** for steadier motion. If you already hold a higher frame rate consistently, keep that target.

## Install or upgrade

Close the game and back up your VRMs and %LOCALAPPDATA%\EnshroudedVRM\State. Extract EnshroudedVRM-0.6.1.zip beside enshrouded.exe, or deploy the complete ZIP with Vortex. If another mod already supplies dinput8.dll, follow the included README’s chainloader instructions before extracting. Keep your saves and shader caches. If you later roll back to 0.5.0, restore its settings backup before launching.

## Known issues

Running Enshrouded as administrator and playing with HDR enabled are both unsupported in 0.6.1. Launch the game normally and use SDR (HDR off).

Frame generation is unsupported in 0.6.1. With RTX Encore enabled, F8 can open while the native character stays visible. We also observed crashes on exit with Encore enabled. Bright highlights and low-light shadows can obscure detail on some avatars. Lip sync applies only to your own character.

[Download the player ZIP](https://github.com/lightnill/EnshroudedVRM/releases/tag/v0.6.1) · [Nexus Mods](https://www.nexusmods.com/enshrouded/mods/242) · [Build the project](BUILDING.md)
