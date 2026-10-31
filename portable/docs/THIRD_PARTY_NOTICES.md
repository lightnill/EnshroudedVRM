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

## MinHook and Hacker Disassembler Engine

The optional local game-voice observation hook statically links MinHook v1.3.4,
revision c3fcafdc10146beb5919319d0683e44e3c30d537. Copyright (C) 2009-2017
Tsuda Kageyu, BSD 2-clause. Its x64 decoder includes Hacker Disassembler Engine,
Copyright (c) 2008-2009 Vyacheslav Patkov, under the included BSD-style terms.
Original source notices and exact source hashes are retained under vendor/minhook.
Full binary distribution notices: notices/MinHook-BSD.txt.
Source: https://github.com/TsudaKageyu/minhook/tree/c3fcafdc10146beb5919319d0683e44e3c30d537

## Dear ImGui and embedded fonts

The local voice/menu rebuild statically includes selected unmodified Dear ImGui
1.91.9b core and Vulkan backend files, revision
f5befd2d29e66809cd1110a152e375a7f1981f06, Copyright (c) 2014-2025 Omar Cornut,
MIT. Its embedded ProggyClean font and stb components retain their original
notices. The menu embeds Roboto Medium, Copyright 2011 Google Inc., under
Apache License 2.0. Full applicable texts and attributions:
notices/Dear-ImGui-fonts.txt. Exact selected source/font hashes and provenance
are retained in vendor/imgui/SOURCE-PINS.json in the source checkout.
Source: https://github.com/ocornut/imgui/tree/f5befd2d29e66809cd1110a152e375a7f1981f06

## Game and avatar rights

The 0.2.0 release additionally bundles the unmodified official
Oculus LipSync Native SDK 29.0.0 Windows x64 library, DLL version 1.61.0,
Copyright Facebook Technologies, LLC and its affiliates. All rights reserved.
It runs its Enhanced detector locally in software on the voice worker. It is
separately licensed under the applicable Oculus/Meta SDK terms, not this
project's MIT license. Full supplied notices, the SDK agreement and the older
Audio SDK 3.3 text referenced by its header are in notices/Oculus-LipSync.txt.
Exact official downloads and file hashes: vendor/oculus-lipsync/PROVENANCE.json.
Source: https://developers.meta.com/vr/downloads/package/oculus-lipsync-native/

Enshrouded and its trademarks/content belong to Keen Games and their licensors.
This is an unofficial local preview; no endorsement or publisher approval is
claimed. The binary includes minimal locally researched interface/rig compatibility
data with original research provenance flags retained. The project MIT license
does not grant rights to game content. No avatar, game mesh/texture or game
executable is redistributed by this project. Users supply their own permitted VRMs.

Valheim VRM repositories were reference research only. No source/tests/shaders/
assets from them were copied into this product or these archives.
