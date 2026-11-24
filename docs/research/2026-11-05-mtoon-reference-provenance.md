# MToon material reference provenance

The optional MToon implementation is developed independently from the official interoperability contracts and the MIT-licensed UniVRM/MToon reference. The downloaded third-party VRM2Enshrouded executable, its embedded shaders and its GPL implementation are not implementation/build inputs.

## Pinned primary references

- VRM specification: `vrm-c/vrm-specification` revision `94e82dd346fa6cf0337c4421728640e5252dd38e`, `specification/VRMC_materials_mtoon-1.0/README.md` and `schema/VRMC_materials_mtoon.schema.json`.
- UniVRM: `vrm-c/UniVRM` revision `efd20f62f1bf0ac3fcc34de1e497324c5197d146`, `Packages/VRM/MToon/Shaders/MToon.shader`, `MToonCore.cginc`, and `Packages/VRM10/MToon10/Shaders/vrmc_materials_mtoon.shader`.
- UniVRM's pinned `LICENSE.txt` is MIT, copyright 2020 VRM Consortium and 2018 Masataka SUMI for MToon. Its notice is retained in THIRD_PARTY_NOTICES.md.

The specification repository's pinned root tree has no LICENSE file. It is consulted for factual field/default/interoperability requirements; implementation text and sample assets from that repository are not copied into this project.

Reference fingerprints:

| Reference | SHA-256 |
| --- | --- |
| VRM1 prose | ac2b6060587b2cd5ddabb26bd7a07e04992a73d0bcbae5e6acb4cdc94da3f70b |
| VRM1 schema | 39e69f102c7ae3bdaa35ff8f2bcbc022a5a4c052fe97009198566105c667e941 |
| MToon0 shader properties | 4b61ffcd3e0b42e504457e5084dd13b378e683639f93c74f1de425d43bfe2c5f |
| MToon0 core reference | b623d50bdfd64d808ba789d528b9d8ce7b8f55633af073962b50fd0a05f43fd9 |
| MToon1 shader properties | 0406c5e6bbb8bdd24b53eac23a59a9fe582617ad00f73e95eb2617b09a0f1a1f |
| UniVRM MIT notice | 5cb01ebb60a7fa1885278a23e7f147f611b44686f772dd78835375365531cfb9 |

## Default and coordinate policies

The pinned VRM1 prose states a black omitted `shadeColorFactor`, whereas the schema and UniVRM shader properties specify white. The implementation follows the schema/reference shader's white default. Explicitly authored black remains black, with a separate authored-presence flag. VRM0 retains its own .97/.81/.86 shade default and legacy lighting defaults; it is not assigned VRM1 defaults.

VRM1 matcap colour follows the schema's white factor. The pinned Unity shader's black property default is documented upstream as a historical compatibility workaround. An absent texture remains absent and contributes no matcap; the factor alone does not invent one.

Legacy MToon textures use the source MainTex transform except the view-dependent sphere/matcap texture. Their stored Unity UV transform is conjugated to glTF UV coordinates consistently with the existing importer. Animation controls remain source-version values for later shader evaluation. VRM1 texture coordinates and KHR_texture_transform metadata are retained independently per binding.

The separate immutable MToon record preserves render mode, culling, cutoff, depth policy, queue controls, outline settings, maps and lighting parameters. It is source metadata, not a GPU ABI or a claim that all features render. Existing surface material ABI/preset selection is unchanged by data preservation alone.

Referenced MToon-only images are decoded under the same caller-supplied image limits, instead of becoming white placeholders. This can increase retained CPU pixels for authored texture sets; optional GPU upload/readiness remains the renderer's responsibility.
