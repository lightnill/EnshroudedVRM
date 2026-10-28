# Importer dependencies

Unmodified legacy TinyGLTF 2.9 C++ headers from the official syoyo/tinygltf repository, commit `0d9ce1a9561688c6c6e7471430d15a50a0b15f1a`, `attic/` directory. The repository's current mainline is a different C API. Only the pinned C++ interface is used here.

- `tiny_gltf.h`: TinyGLTF, MIT; license copied alongside it.
- `json.hpp`: nlohmann JSON; MIT license is embedded in the header.
- `stb_image.h`: stb_image; dual public domain/MIT license embedded in the header.

Importer compilation disables filesystem access, external images, image writing, and image formats other than PNG/JPEG. The wrapper validates GLB and accessor bounds before TinyGLTF decoding and bounds image memory before image decompression. This does not constitute a general security audit of these dependencies.
