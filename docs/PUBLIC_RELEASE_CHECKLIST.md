# DMC Native Reader — Public Repository Opening Checklist

Last updated: 2026-09-10.

This checklist separates the **current private development repository**, a future **public source repository**, and any **official signed application distribution**. These are different milestones.

## Current product truth

- [x] Accepted Android baseline is Native Reader `1.0` / versionCode `24` on `main`.
- [x] Production `NativeModuleRegistry` contains exactly **MOD / SCM / DDS / PTX**.
- [x] Unknown/unpromoted families fail closed.
- [x] MOD/SCM use canonical DMC Rengine read-side authority through the pinned ReaderCore boundary.
- [x] DDS/PTX use reusable native codec/framing/preview/TextureSet paths.
- [x] Android is a thin shell; DMC format parsing and application decisions remain native/typed.
- [x] Physical Samsung acceptance confirms all four supported families open and PTX model texture application works.
- [x] v24 installed-size report is 2.32 MB, down from 6.27 MB before the size/module cleanup.
- [x] Pre-cleanup wide-format work is preserved on `main.2` as backlog/reference rather than advertised as production support.
- [x] Current v26 UV/focused-inspection work is clearly marked candidate/draft until device acceptance.

Do **not** advertise HITS, TXT/index, DCA, LIG/LIG2, PAC/PNST, NBZ, EFM/MRP/SHW or a 71-family registry as current Native Reader production support. Those statements belong to historical pre-cleanup development only.

## Public-facing repository material

- [x] README describes the accepted v24 baseline and active v26 candidate separately.
- [x] Current architecture and authority boundaries are documented.
- [x] Status and roadmap distinguish completed work from pending candidate work.
- [x] Changelog no longer presents obsolete development lines as current releases.
- [x] Historical build documents are classified as historical evidence rather than current product instructions.
- [x] Contribution/security guidance preserves evidence and read-only boundaries.
- [x] Capcom / Devil May Cry affiliation disclaimer is present.
- [x] Repository does not intentionally contain Capcom game archives, proprietary game assets, proprietary source code or DMC executable binaries.

## Open admin/product gates before Public

### 1. Licensing decision

**OPEN — owner decision required.**

There is currently no root `LICENSE` file on accepted `main`. Until explicit licensing terms are committed, do not describe the repository as open source merely because source may later become publicly visible.

If custom/source-available terms are selected, public copy must use that wording consistently. If an OSI-approved license is selected, update README/NOTICE/contribution material accordingly.

### 2. Repository About and topics

Recommended GitHub About description:

> Native Android reader for Devil May Cry 3 HD resources — C++20 MOD/SCM 3D inspection, DDS/PTX previews and model texture attachment.

Recommended topics:

`devil-may-cry` `dmc3` `reverse-engineering` `modding` `android` `cpp` `file-format` `binary-analysis` `dds` `3d-viewer`

Recheck these immediately before public opening so metadata matches the then-accepted `main`, not an old milestone.

### 3. Historical branches and stale PRs

**OPEN — admin/history review required.**

Repository visibility exposes historical refs that remain reachable. Review development/release/experiment branches before changing visibility. In particular:

- preserve `main.2` only if its pre-cleanup backlog/history is intentionally public;
- treat PR #28 public-opening preparation as stale relative to current v24 unless it is rebuilt/rebased on current `main`;
- keep PR #29 iOS/Windows work explicitly preview/experimental until its own build/device acceptance;
- keep PR #32 v26 draft until Samsung/device acceptance.

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

## Correct first-public messaging

A future public repository should state:

- DMC Native Reader is a read-only native resource viewer/inspector for user-owned Devil May Cry 3 HD files;
- Android is the accepted production shell;
- current production modules are MOD, SCM, DDS and PTX;
- DMC Rengine is the canonical reverse/read-side authority for promoted format logic;
- semantic completeness is evidence-gated;
- archived/experimental formats are not current support claims;
- iOS/Windows/Web remain preview/future directions until separately accepted.

This wording can expand only when the corresponding capability has actually been promoted to accepted `main`.
