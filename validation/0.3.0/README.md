# 0.3.0 verification

The published ZIP is pinned by SHA-256 `519c0141ad00faa6c51a800aee5cf28c5d8fc5aaa415c542c889fb37f4748d0e`. The runtime is version 0.3.0 and includes 16-bit-per-channel PNG input compatibility, normalized to RGBA8 at import. The exact previously tested bootstrap is retained unchanged with its independent 0.2.0-standalone-dev version.

The package has 42 members, 41 manifest entries and 257 checked source inputs. The sample and shaders are unchanged. Two assembled ZIPs are byte-identical. See package-verification.json and SCAN-RESULTS-0.3.0.json for exact hashes and scan coverage. Zero detections do not establish safety; ZIP inspection and unsupported or failed engines have coverage limits.

Targeted importer, importer-integration and packaging verification are recorded separately from historical feature-suite results. The retained bootstrap has desktop Steam/F8 evidence. Laptop validation and a manual check of the selected runtime/bootstrap pair remain pending. Nexus has not been updated. No gameplay FPS improvement is claimed for these packaging changes.
