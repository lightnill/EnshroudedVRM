# Certified owner-only partial palette observations

The existing catalog's `must_reject_pose_binding` remains true for conflicting complete native palettes. A few audited head resources have a14-slot palette and the exact same14 slots followed by five extra, unmapped joints. Choosing a complete variant from geometry would be ambiguous. An explicitly certified partial observation reads only the common complete option; it does not identify a preferred resource or claim the full palette length.

The optional `owner_partial_palette` object in a v2 descriptor has exactly these fields:

```json
{
  "policy": "owned-prefix-v1",
  "palette_joint_ids": [16, 17, 21, 22, 23, 25, 26, 27, 28, 29, 30, 31, 32, 33]
}
```

The metadata author must audit the complete descriptor candidates and original rejection provenance, including near-signature conflicts. The current amendment adds this policy to21 signatures derived from the exact accepted catalog and original generator projections. All6533 consumed descriptor projections matched the original catalog. Every amended signature has a strict14/19 prefix chain, one empty auxiliary classification, no omitted mapped semantics and no overlapping same-count tolerance boxes at2*tolerance. No existing field or reject flag changes. The accepted profile is unchanged.

The runtime rejects malformed/unknown certificates, unambiguous or nonprefix options, nonempty/ambiguous auxiliary classifications, omitted mapped semantics and overlapping tolerance boxes. Without a certificate the existing explicit reject still applies. A certificate does not authorize pose observation by itself: the snapshot must match its valid owner/source/epoch, the instance must be owned and drawn, and the account/connection/actor association must be structurally valid. The existing attested producer and final source/account/actor/model/assignment checks remain essential.

Only certified slots are decoded and checked. An uncertain tail may be absent or malformed without being read by the pose decoder. Every observed matrix still undergoes bounds/affine/duplicate-joint checks; actor grouping, core semantics and resource-GUID intersection remain. Previously accepted complete palettes still decode their entire length and reject malformed required matrices. Ownerless/menu/legacy calls do not gain partial observation. The selected slot vector records observations rather than a resource's complete palette.

This addresses the audited palette refusal seen during reconnect testing. It does not resolve missing native draw records, off-screen pose availability, camera-wall visibility transitions or all possible player-list transitions. Tests use owned metadata/buffers and the actual pose reader; real reconnect behavior remains a manual check. All work is local and adds no networking or collection of player identifiers.
