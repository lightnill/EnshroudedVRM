# Release upload notes

Author: lightnill. Version: 0.1.0-preview-script. Original code: MIT.

Ship only the current explicitly assembled CMD package and its matching source
and checksums. No custom launcher EXE belongs in the player download. Runtime DLL,
shaders and compatibility data remain required. Developer fixtures are not shipped.

Never upload an extraction used for playing: it can contain private avatars,
State or logs. No public sample avatars or model-derived screenshots are bundled.
Use the title-only cover. Keep all applicable runtime/dependency license notices.

Verify every final file hash. Scan individual extracted files and record unsupported
engines, failures and timeouts. Store evidence outside the package. Do not infer
member coverage from an archive verdict or carry an old verdict onto new bytes.

Nexus uses manual installation, not mod-manager installation. Keep the player
introduction clear about the preview stage and local-only appearance. Do not
promise universal FPS gains or implemented multiplayer avatar synchronization.
The owner reviews and publishes the prepared Nexus page.
