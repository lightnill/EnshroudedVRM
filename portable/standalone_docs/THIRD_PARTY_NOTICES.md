Original EnshroudedVRM code is MIT licensed; see LICENSE.

The runtime and bootstrap include components under their respective terms:
GCC/libstdc++ runtime exception, MinGW-w64 runtime, Dear ImGui and the bundled
Roboto font, MinHook, TinyGLTF, nlohmann JSON, stb image, Vulkan headers and
attributed ShroudForge-derived source. Full notices are retained in notices/.
The official Oculus LipSync binary is included unmodified under its supplied
license; see notices/Oculus-LipSync.txt. It is not covered by the project's MIT
license. Windows system DLLs and Vulkan drivers are not redistributed.

The included `Avatars/vroid-sample.vrm` was supplied by Lightnill, assembled with
VRoid Studio default assets and distributed unchanged with the creator's
permission. Its embedded VRM 1.0 usage permissions apply separately from the
mod's MIT code license. See `Avatars/README.txt` and the original VRM metadata.

No third-party speech sample or other avatar is bundled. Source tests document
their separate public test-fixture download. Standalone startup requires no framework
or separate loader; retaining source attribution does not introduce a dependency.
