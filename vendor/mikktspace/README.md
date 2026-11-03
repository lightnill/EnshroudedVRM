# MikkTSpace 1.0

Canonical source from https://github.com/mmikk/MikkTSpace at commit
`3e895b49d05ea07e4c2133156cfa94369e19e409`.

`mikktspace.c` and `mikktspace.h` are unmodified upstream files. Their original
Morten S. Mikkelsen copyright and full license notice are retained in both files.

The importer includes the C implementation once in `src/vrm_asset.cpp` so existing
single-translation-unit importer consumers keep their link contract. The local
adapter lives in `src/import_surface_geometry.inl`; it supplies triangles, retains
per-corner bases, and splits incompatible corners without modifying upstream code.
