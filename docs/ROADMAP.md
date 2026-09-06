# DMC Native Reader — Public Roadmap

## Phase 1 — Native Reader v1 architecture

**Status: achieved**

- explicit modular reader architecture;
- no central family decoder;
- no wildcard structural fallback;
- fail-closed unknown-family behavior;
- 71 known-family contracts;
- production Android `Open with` path;
- core popular DMC3 modding formats promoted to real readers;
- reproducible host + Android CI gate.

## Phase 2 — Device / corpus debug

**Status: active**

Primary goal: test the fixed v1 architecture against real DMC3 HD files and real Android/OEM routing behavior.

Priority debug targets:

1. MOD real-model corpus;
2. SCM real-scene corpus;
3. PTX -> DDS child consistency;
4. DDS mip/compression edge cases;
5. stage TXT and `.index` parser/routing edge cases;
6. PAC/PNST/NBZ inspection behavior;
7. Samsung My Files / Android Files `Open with` routing;
8. malformed/truncated input hardening.

## Phase 3 — Promote the next evidence-ready readers

A family is promoted only when reverse evidence is strong enough to support a bounded product parser.

Current candidates:

- SHW — strong real-payload + executable evidence; guarded product parser still needs closure;
- EFM — mesh-bearing family evidence exists; exact format-specific bindings remain open;
- SO — significant research exists; product parser contract still needs promotion;
- MRP — family identity is confirmed, but exact binary schema remains open.

## Phase 4 — Deeper semantic interpretation

After structural reliability is established:

- richer SCM scene/material semantics;
- MOD skeletal/skin behavior closure;
- texture/material linkage;
- animation/control families;
- stage/event semantics;
- deeper archive-to-resource provenance.

## Phase 5 — Authoring / write path

Native Reader v1 is intentionally read-only.

Editing/repacking belongs to a later milestone and must reuse canonical writer contracts from DMC Rengine rather than adding ad-hoc Android-only writers.

The order is deliberate:

```text
recognize correctly
  -> parse safely
      -> validate on real corpus
          -> understand semantics
              -> only then author/write
```
