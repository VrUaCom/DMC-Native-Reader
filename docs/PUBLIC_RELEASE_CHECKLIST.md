# DMC Native Reader — Public Repository Opening Checklist

> **PUBLIC-OPENING / ADMIN CHECKLIST, NOT v33 RELEASE AUTHORITY.** This document was originally anchored to the accepted/candidate state on 2026-09-10 and intentionally preserves that decision history. Current v33 product/release authority lives in `docs/PROJECT_AI_CONTEXT.md`, `docs/STATUS.md`, `docs/MODULAR_SPIDER_V33.md`, Program #34, release Phase #40 and Final Review Gate #45. Re-evaluate every version/module/C++ metadata statement below immediately before changing repository visibility.

Last updated: 2026-09-10 baseline; classification clarified 2026-09-15.

This checklist separates the **current private development repository**, a future **public source repository**, and any **official signed application distribution**. These are different milestones.

## Current product truth at the 2026-09-10 public-opening review

- [x] Accepted Android baseline is Native Reader `1.0` / versionCode `24` on `main` at that review point.
- [x] Production `NativeModuleRegistry` contained exactly **MOD / SCM / DDS / PTX** at that review point.
- [x] Unknown/unpromoted families fail closed.
- [x] MOD/SCM use canonical DMC Rengine read-side authority through the pinned ReaderCore boundary.
- [x] DDS/PTX use reusable native codec/framing/preview/TextureSet paths.
- [x] Android is a thin shell; DMC format parsing and application decisions remain native/typed.
- [x] Physical Android device acceptance confirmed the then-supported families opened and PTX model texture application worked.
- [x] v24 installed-size report was 2.32 MB, down from 6.27 MB before the size/module cleanup.
- [x] Pre-cleanup wide-format work is preserved on `main.2` as backlog/reference rather than advertised as production support.
- [x] The then-current v26 UV/focused-inspection work was marked candidate/draft until device acceptance.

Do **not** use this historical module/version list as the current v33 release matrix. Current production/candidate truth must be read from `docs/STATUS.md` and the active Project phases/review gates.

## Public-facing repository material

- [x] README described the accepted baseline and active candidate separately at the time of review.
- [x] Current architecture and authority boundaries were documented.
- [x] Status and roadmap distinguished completed work from pending candidate work.
- [x] Changelog no longer presented obsolete development lines as current releases.
- [x] Historical build documents were classified as historical evidence rather than current product instructions.
- [x] Contribution/security guidance preserves evidence and read-only boundaries.
- [x] Capcom / Devil May Cry affiliation disclaimer is present.
- [x] Repository does not intentionally contain Capcom game archives, proprietary game assets, proprietary source code or DMC executable binaries.

## Open admin/product gates before Public

### 1. Licensing decision

**OPEN — owner decision required.**

There is currently no root `LICENSE` file on the reviewed baseline. Until explicit licensing terms are committed, do not describe the repository as open source merely because source may later become publicly visible.

If custom/source-available terms are selected, public copy must use that wording consistently. If an OSI-approved license is selected, update README/NOTICE/contribution material accordingly.

### 2. Repository About and topics

Historical 2026-09-10 recommendation (must be regenerated from the accepted product state before public opening):

> Native Android reader for Devil May Cry 3 HD resources — C++20 MOD/SCM 3D inspection, DDS/PTX previews and model texture attachment.

Historical recommended topics:

`devil-may-cry` `dmc3` `reverse-engineering` `modding` `android` `cpp` `file-format` `binary-analysis` `dds` `3d-viewer`

The quoted C++20/module wording is intentionally retained as history and is **not** current v33 metadata. Recreate About/topics immediately before public opening from the then-accepted `main`.

### 3. Historical branches and stale PRs

**OPEN — admin/history review required.**

Repository visibility exposes historical refs that remain reachable. Review development/release/experiment branches before changing visibility. In particular:

- preserve `main.2` only if its pre-cleanup backlog/history is intentionally public;
- treat old public-opening preparation branches/PRs as stale unless rebuilt/rebased on current accepted `main`;
- keep iOS/Windows work explicitly preview/experimental until its own build/device acceptance;
- do not infer candidate acceptance from an old draft state.

Do not merge an old public-prep branch simply because its documentation was once correct.

### 4. Commit metadata and privacy

**OPEN — privacy review recommended.**

Review historical commit author/committer metadata and branch history before public visibility. If history sanitization is required, perform it before opening the repository; changing future Git settings does not rewrite old commits.

### 5. Development and production signing

The tracked `keys/dmc-native-reader-test.jks` is a **development/test-only** signer used for install-over continuity of internal/device-test APKs. It must not be represented as production trust material.

Before an official public APK:

- provision a separate production signing authority outside Git history;
- keep private keys/passwords in protected release infrastructure;
- preserve update-signing continuity for subsequent official releases;
- record the production certificate digest and APK SHA-256 in release evidence;
- decide whether the development test JKS should remain in a public repository or be removed as a hygiene/product decision even though it is not a production secret.

### 6. GitHub repository controls

**OPEN — admin action.**

Before Public, review/configure:

- About text and topics;
- default branch and branch/ruleset protection;
- issue/contribution policy;
- private security reporting / security features where available;
- release permissions and protected production environment;
- stale branches/PRs;
- repository visibility itself.

### 7. Distribution surface

Do not publish a canonical “latest stable” asset until the chosen release line has:

- source state tied to an accepted commit/tag;
- required host/native/APK evidence;
- real-device/corpus acceptance;
- correct production signing authority;
- release notes and artifact hashes matching the exact delivered binary.

For the active v33 program, detailed release evidence is governed by #40 and #45 rather than this public-opening checklist.

## Correct first-public messaging

Before public opening, regenerate this section from the then-accepted `main`. The public repository should accurately state:

- DMC Native Reader is a read-only native resource viewer/inspector for user-owned Devil May Cry 3 HD files;
- which platform shells are accepted versus preview;
- the exact promoted production modules at that time;
- DMC Rengine is the canonical reverse/read-side authority for promoted format logic;
- semantic completeness is evidence-gated;
- archived/experimental formats are not current support claims.

This wording can expand only when the corresponding capability has actually been promoted to accepted `main`.
