# Third-party notices

Original Enshrouded VRM code and documentation: Copyright (c) 2026 lightnill,
MIT. Keep the project LICENSE and the notices below in copies/substantial portions.
The project license does not replace third-party licenses or license game content.

## TinyGLTF and bundled headers

Pinned C++ headers from syoyo/tinygltf revision
`0d9ce1a9561688c6c6e7471430d15a50a0b15f1a`, `attic/`:

- TinyGLTF: MIT, Copyright (c) 2017 Syoyo Fujita, Aurélien Chatelain and contributors.
- JSON for Modern C++ 3.10.4: MIT, Copyright (c) 2013–2019 Niels Lohmann.
- stb_image: selected MIT alternative, Copyright (c) 2017 Sean Barrett.

Headers and embedded notices are preserved in the source archive. Full
binary-distribution notices are in notices/TinyGLTF-MIT.txt,
notices/nlohmann-json-MIT.txt and notices/stb-image-MIT.txt.
Source: https://github.com/syoyo/tinygltf/tree/0d9ce1a9561688c6c6e7471430d15a50a0b15f1a/attic

## ShroudForge

Read-only owner/registry/service layout contracts and the scoped executable-write/
near-allocation approach were adapted from ShroudForge revision
`46b59da1b3642f91fee3808c513efb996ec33f23`, Copyright (c) 2026 bonsaibauer, MIT.
Original notices/provenance remain in native_owner_profile.hpp, native_owner.cpp
and native_tool_call_patch.cpp. Full license: notices/ShroudForge-MIT.txt.
Source: https://github.com/bonsaibauer/shroudforge

## Vulkan headers

The build uses Khronos Vulkan API headers, Copyright 2015–2026 The Khronos Group
Inc., under their MIT alternative (SPDX Apache-2.0 OR MIT). Full selected notice:
notices/Vulkan-Headers-MIT.txt. The SDK/headers/compiler tools are not bundled in
the portable binary ZIP.
Source: https://github.com/KhronosGroup/Vulkan-Headers

## Statically linked compiler/runtime components

The accepted VRM runtime DLL is built with GCC 16.2.0 from w64devkit. GCC's runtime components are under GPLv3
with the GCC Runtime Library Exception 3.1; this eligible GCC compilation permits
the independent project modules to retain MIT terms. The full GPL and exception
texts are in notices/GPL-3.0.txt and notices/GCC-RUNTIME-EXCEPTION.txt.

MinGW-w64/Winpthreads and other runtime notices are copied unmodified from the
toolchain in notices/MinGW-w64-runtime.txt; retain all included attributions and
conditions. notices/LGPL-2.1.txt is included for the Wine header/IDL notice in
that runtime notice bundle. No compiler binaries, game extraction tool or external
modding framework binary is included.

## Game and avatar rights

Enshrouded and its trademarks/content belong to Keen Games and their licensors.
This is an unofficial local preview; no endorsement or publisher approval is
claimed. The binary includes minimal locally researched interface/rig compatibility
data with original research provenance flags retained. The project MIT license
does not grant rights to game content. No avatar, game mesh/texture or game
executable is redistributed by this project. Users supply their own permitted VRMs.

Valheim VRM repositories were reference research only. No source/tests/shaders/
assets from them were copied into this product or these archives.
