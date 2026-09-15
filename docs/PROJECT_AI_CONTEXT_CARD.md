# Project Context Card — READ FIRST

This card exists as a compact entry point for the private GitHub Project / AI workflow.

## Canonical context
Read before planning or implementation:

- `docs/PROJECT_AI_CONTEXT.md` — architecture, standards, evidence rules, C++23, Spider C++, WorkspaceGraph, repository boundaries, Project task format and review-gate workflow.
- `docs/CXX23_SPIDER_MIGRATION_2026-09-15.md` — current migration decision/research/plan.
- `docs/MODULAR_SPIDER_V33.md` — v33 architecture contract.
- `docs/RESOURCE_DEPENDENCY_GRAPH_V33.md` — dependency-graph direction.
- `docs/STATUS.md` — current accepted/candidate/evidence state.

## Project tracking
- `#34` — C++23 Migration Program master tracker.
- `#36` — current active Phase 2.
- `#41` — mandatory next review gate.

## Mandatory execution rule
Do not start a later implementation phase directly after finishing the previous phase. Complete the review gate first; the gate must update/split the next phase based on exact-head evidence and issue GO/NO-GO.

## Repository boundary
Current program writes only to `VrUaCom/DMC-Native-Reader`. No other repository may be modified without Viktor's explicit authorization.
