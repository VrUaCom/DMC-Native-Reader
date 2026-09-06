---
name: Format / corpus report
title: "[FORMAT] "
about: Report a DMC resource family sample, parser mismatch or new evidence
labels: format,evidence
---

## Resource family

- Family / extension:
- Native Reader currently reports:
- Expected family/status:

## Evidence source

Check all that apply:

- [ ] filename / extraction metadata
- [ ] content signature / magic
- [ ] repeated corpus structure
- [ ] canonical `dmc-rengine-cpp` implementation
- [ ] canonical executable reverse evidence
- [ ] in-game/runtime observation
- [ ] other

## File facts

- Filename:
- Size:
- SHA-256:
- Container / extraction context (if relevant):

Do not upload copyrighted game archives, proprietary executable files or assets you do not have redistribution rights for.

## Observed structure

Describe offsets, counts, records, streams or invariants you actually observed. Keep unknown fields unknown.

## Native Reader behavior

What does the current module accept/reject/show?

## Proposed maturity change

- [ ] recognition only
- [ ] partial / evidence-gated
- [ ] structural
- [ ] mesh / renderable
- [ ] no maturity change — bug/documentation only

## Reproduction / supporting material

Include parser traces, bounded byte excerpts where legally shareable, tests, code references or links to corresponding `dmc-rengine-cpp` evidence.
