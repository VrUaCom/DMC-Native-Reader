---
name: Format / corpus evidence report
title: "[FORMAT] "
about: Report canonical evidence, a promoted-format mismatch, or a candidate family for future promotion
labels: format,evidence
---

## Resource family

- Family / extension:
- Is this currently a production Native Reader module? (MOD / SCM / DDS / PTX / no):
- Native Reader version/commit tested:
- Current Native Reader result:
- Expected result/status:

## Report type

- [ ] bug/mismatch in promoted MOD/SCM/DDS/PTX behavior
- [ ] new canonical evidence for an existing promoted family
- [ ] candidate family for future Architecture v2 promotion
- [ ] documentation/evidence correction only

Historical recognition-only code or a filename extension alone is not sufficient for production-module promotion.

## Evidence source

Check all that apply:

- [ ] filename / extraction metadata
- [ ] content signature / magic
- [ ] repeated corpus structure
- [ ] canonical `dmc-rengine-cpp` implementation
- [ ] canonical executable reverse evidence
- [ ] in-game/runtime observation
- [ ] reproducible writer/rebuild evidence
- [ ] other

## File facts

- Filename:
- Size:
- SHA-256:
- Container / extraction context (if relevant):

Do not upload copyrighted game archives, proprietary executable files or assets you do not have redistribution rights for.

## Observed structure / semantics

Describe only offsets, counts, records, streams, relationships or invariants actually supported by evidence. Keep unresolved fields explicitly unknown.

## Canonical authority

- Relevant `dmc-rengine-cpp` file/commit/PR:
- Evidence status / confidence boundary:
- Does Native Reader need a new parser, or only projection/presentation of already-canonical data?

Prefer the latter whenever canonical authority already exists.

## Proposed product change

- [ ] no product change — evidence/documentation only
- [ ] fix existing production module
- [ ] expand typed inspection/presentation capability
- [ ] promote a new bounded Architecture v2 module

If proposing a new module, describe its fail-closed behavior, malformed-input tests, typed projection contract and unresolved semantics. Do not propose restoring the pre-cleanup wide registry wholesale.

## Reproduction / supporting material

Include parser traces, bounded byte excerpts where legally shareable, tests, code references or links to corresponding DMC Rengine evidence.
