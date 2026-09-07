---
name: Feature request
title: "[FEATURE] "
about: Propose a Native Reader viewing, platform or UX improvement
labels: enhancement
---

## Problem

What user problem or workflow limitation does this address?

Native Reader's core mission is: **Make DMC resources feel like ordinary files.** Prefer requests that make promoted resources easier to open, recognize, inspect, visualize, navigate or understand.

## Proposed behavior

Describe the expected Native Reader behavior.

## Scope

- [ ] Android v1 shell / file opening
- [ ] future iOS shell
- [ ] future Windows shell
- [ ] future Web/WebAssembly shell
- [ ] MOD
- [ ] SCM
- [ ] DDS
- [ ] PTX
- [ ] Inspector / rendering / preview UI
- [ ] child-resource navigation
- [ ] build / release / CI
- [ ] new format promotion
- [ ] other

## DMC Rengine / architecture impact

Does this request introduce reusable format semantics, parser behavior, writer/runtime knowledge or another capability that belongs in the central DMC Rengine C++20 core?

If yes, explain the intended upstream authority rather than proposing a private Native Reader-only parser.

For Web work, DMC resource semantics should remain in C++20/WebAssembly; JavaScript/TypeScript is a presentation/binding layer, not an alternate parser authority.

If the request changes format semantics, explain the canonical/evidence source that would support it.

If it requires a new format, explain why it is ready to be promoted into the production registry instead of remaining research-only, and what natural user-facing representation it should expose.

## Product-role check

Does this belong in Native Reader's viewing/accessibility role, or is it primarily an edit/repack/resource-management workflow better suited to Pocket GDS or another DMC Rengine-backed authoring tool?

## Alternatives

What workarounds or alternative designs have you considered?

## Legal / data boundary

Do not attach proprietary game archives, executable binaries, leaked source or material you do not have redistribution rights for.

Commercial-use requests are subject to the project `LICENSE` and are not granted merely by opening an issue.
