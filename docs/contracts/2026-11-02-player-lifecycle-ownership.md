# Player identity and render readiness during connection changes

Local development correction for exact game build26320373. This changes the previously approved per-player assignment implementation, not its saved choices, UI, networking, voice or shaders. Other accounts still default to no replacement.

## Observed coupling

The identity worker's `players()` path previously resolved every active player's entity, equipment, models and native buffer before returning a UI roster. Per-player render capture did the same and then filtered requested identities. One connected actor without a spawned world entity therefore removed the local ownership result as well. A null roster also cleared the worker's explicit local identity, routing rendering through the older accountless local observation. Audited partial-palette sampling intentionally refused that ownerless path.

The installed diagnostic connected a census validation failure to an ownerless certified-palette refusal. A subsequent bounded observer during a normal connection caught the original empty entity-hash-chain predicate while processing another player, with a successful separate local structural capture. The external observer used a private1000ms IPC-read allowance; it neither changed the installed40ms work cap nor established native admission, performance or an atomic reconstruction of an earlier frame. No raw player identities, names or pointers were exported by that observer.

## Scope of absence

Only a nonlocal active association whose original ID hash probe reaches an empty occupied-bit slot is treated as a missing entity. Dimensions, ranges and original probing semantics are checked before this branch. The relevant occupancy byte and map/identity metadata remain consumed guards. Local entity absence, occupied bad pointers or headers, exhausted probes, missing components, zero models, invalid strides, conflicts, unreadable data and budget failures remain hard render-capture failures.

A missing component is deliberately different: an existing entity might still hold equipment, so silently skipping it could discard overlap evidence. This correction does not classify those cases as normal absence.

## Three consumers

- Identity-only roster discovery retains exact image, owner, reflected registry, named service and original code/binding checks, all16active/inactive scalar identity rules, uniqueness and the explicit local association. It reverifies the consumed guards and budget, then stops before render readiness. These identities propose UI targets and grant no drawing authority.
- The original strict owner API still requires the complete all-active render census, even when an unrequested active actor is missing.
- The available-owner API returns an explicit result in each requested position: Ready, EntityAbsent or AssociationUnavailable. Requests must first pass schema/uniqueness checks and a freshly valid complete roster. Invalid metadata clears the batch. Only Ready contains a fully validated candidate; the other results cannot produce a pose, mask or permit.

Render capture still traverses all active actors and rejects overlapping ready entities/model indices, including ready actors with no assigned VRM. Ready candidates share one immutable complete proof bank, including identity and negative empty-slot evidence. No independently sampled banks are merged.

## Proposals, refresh and submission

The backend keeps the full ordered subject/identity request key and availability results. A partial or empty ready set is retried within the existing250ms observation cadence, with explicit current source/epoch/identity invalidation. This cache proposes a selection; it cannot renew a pose timestamp or skip final verification.

Only ready subjects enter the renderer's cohort. An unavailable subject's body remains native, and its mask is never added to the clipping union. Existing CPU source/owner/assignment/pose checks and the shared GPU commit apply to every included subject.

An inserted or removed entity changes the negative/identity guard bank. Final census refresh uses available capture over the same all-active proof, even for a cohort containing only the local identity. Every included requested row must remain Ready with exact same owner selection, parts, source and assignment, followed by fresh shared-bank verification within the one40ms native allowance. Refresh must not discard an unassigned actor or absence guard merely because it is not rendered. Unsafe or changed proof declines the cohort normally. The250ms pose cap and4-view/16-player bounds stay unchanged.

## Evidence boundaries

Tests use the actual core against owned synthetic engine memory and the actual backend against fake Vulkan. They cover request mapping, negative-guard changes, partial cache/retry, strict versus available semantics, native-body exclusion, corruption/refusal and included-cohort refresh. They do not prove the live visual connection behavior. A normal two-account manual recheck is required before claiming that correction.

The separate wall/zero-owned-drawn-record gap is not addressed by this change. Bootstrap, profile/catalog, shaders, model content and user state remain unchanged. No model transfer, remote voice, real-world location or new diagnostic identity collection is added.
