# DMC Native Reader documentation

This directory contains current product documentation, release notes, architecture specifications, reverse-engineering evidence and historical development records.

Do not assume that every version-numbered document describes the current public application.

## Current source of truth

For current product state, read in this order:

1. [`../README.md`](../README.md) — public product overview, platform status and naming policy.
2. [`STATUS.md`](STATUS.md) — accepted `main` baseline and current platform state.
3. [`ROADMAP.md`](ROADMAP.md) — active work and next milestones.
4. [`ARCHITECTURE_V2.md`](ARCHITECTURE_V2.md) — portable native architecture.
5. [`RELEASE_GATES_V1.md`](RELEASE_GATES_V1.md) — release/promotion rules.
6. [`SPIDER_FAMILY.md`](SPIDER_FAMILY.md) — Spider/Black Widow/Crusader responsibility boundaries.

When an older evidence document conflicts with current code or `STATUS.md`, the current accepted code and current status document take precedence for present-day product claims.

Historical evidence remains authoritative for the build/revision it originally documented.

## Current releases

### Android

- **v68 / 1.0.41**
- promoted to `main` on 2026-09-26
- release notes: [`releases/android-v68-1.0.41.md`](releases/android-v68-1.0.41.md)

### Windows

- **v1.0.0 Preview**
- public technical preview
- older capability baseline than Android v68
- Windows parity work must not be represented as released until a newer Windows artifact is published

## Naming policy

Use:

- **Devil May Cry HD Collection** for the collection;
- **Devil May Cry 3: Special Edition** for the game;
- **DMC3** as shorthand after the full name is established.

Do not use “Devil May Cry 3 HD Collection” as a product title.

## Historical evidence

Files with old version numbers, branch names, hashes, package sizes or acceptance states are preserved as historical evidence. Do not rewrite those values merely to make them match a newer release.

Instead:

- classify the old document as historical;
- add a new release/evidence document for the new artifact;
- update `README.md`, `STATUS.md`, `ROADMAP.md` and `CHANGELOG.md` when the current product state changes.

## Public/release administration

- [`PUBLIC_RELEASE_CHECKLIST.md`](PUBLIC_RELEASE_CHECKLIST.md) — repository/public-release administration.
- [`../SECURITY.md`](../SECURITY.md) — security reporting.
- [`../CONTRIBUTING.md`](../CONTRIBUTING.md) — contribution rules.

Repository administration, licensing, platform distribution and parser acceptance are separate concerns.
