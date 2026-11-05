# Build 26430625 compatibility audit

Steam installed this hotfix during the first normal launch of the input-loader
candidate. The previous candidate declined it; that launch did not establish
gameplay compatibility. This port supports the exact current image only.

| Fact | Value |
| --- | --- |
| Previous executable SHA-256 | `ce6fdba63e27744943b695921383a29133285d359e07671f6ca33a47e2147618` |
| Current executable SHA-256 | `5d9ef2f140b8ed49e30dbee655841b89d08baec20f799a1817e171a18a850c77` |
| PE timestamp | `1793791052` |
| File bytes / mapped image bytes | `35476040` / `44298240` |
| Reflected types / changed layouts, indices or hashes | `12658` / `0` |
| Runtime function table entries | `72885` |
| GetCommandLineW IAT / trusted return RVA | `0x13cf598` / `0x139c9c7` |

Private evidence is retained under
`msvc-14.44-port-20261103/private/bootstrap-rebuild/game26430625` in the parent
workspace. Neither executable nor extracted game assets are distributed.
`images.json` and `input-identities.json` bind the original references;
`verified-new-profile.json` hashes the individual proof records. The source
profile records that profile hash and the full current reflection capture hash.

Function locations were derived separately from complete unwind function
comparisons, with address operands normalized. Functions containing embedded
jump tables received additional proofs for every translated entry and all
remaining bytes. Named descriptors and their complete service lists resolve
ambiguous byte patterns. The GPU thunk is anchored by the independently derived
vtable and its full constructor target. The local-player predicate is a unique
complete leaf in both executable code sections. No global address delta was
applied.

The owner root, vtables, service offsets and all 1541 consumed ECS type records
remain unchanged. Original addressing instructions in complete equivalent
functions independently verify registry and execution fields. All 13 player
identity pins, preview producer/copy/render/publication sites, lifetime hooks,
voice sites, diagnostic function groups and the bounded input startup call were
re-derived. Image, bytes, owner, count, lifetime, file and caller guards remain
required. The immediately previous image is refused before layout traversal.

Fresh read-only graphics extraction verifies the current archive content map,
all 202 referenced SPIR-V modules and the existing pipeline interfaces of six
graphics assets. The referenced modules are byte-identical. An additional UI
drop-shadow layout does not change existing expanded interfaces. Mod shader
sources remain unchanged. This is static evidence; actual Vulkan admission,
startup timing, F8, model switching and multiplayer require separate live
acceptance receipts for the final scanned candidate.
