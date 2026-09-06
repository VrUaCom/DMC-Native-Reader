# DMC Native Reader — Public Repository Opening Checklist

This checklist separates **opening the source repository** from publishing an **official signed distribution APK**.

## Product baseline

- [x] Native Reader v1 debug-baseline milestone frozen.
- [x] System-integrated Android file opening path documented.
- [x] Native C++ `NativeModuleRegistry` architecture documented.
- [x] 71 recognized DMC families represented by 71 explicit module contracts.
- [x] Core modding readers documented: MOD, SCM, DDS, PTX, TXT and `.index`.
- [x] Additional promoted readers documented: HITS, DCA, LIG/LIG2, PAC, PNST and NBZ.
- [x] Partial/evidence-gated status is explicit for EFM, MRP and SHW.
- [x] SO is not misrepresented as a completed v1 semantic reader.
- [x] Unknown-family behavior is fail-closed.
- [x] v1 exact-head CI / Android APK gate passed before entering debug phase.

## Public-facing repository material

- [x] Public README explains what the application is and why it exists.
- [x] Architecture and evidence policy are visible from the repository front page.
- [x] Format support is described by maturity instead of a misleading binary supported/unsupported claim.
- [x] Capcom / Devil May Cry affiliation disclaimer is present.
- [x] Project contains no Capcom game archives, proprietary game files or game executable binaries.
- [x] Contribution guide added.
- [x] Security policy added.
- [x] Bug / real-file debug issue templates added.
- [x] Development signing-key boundary documented.

## Manual gates before switching repository visibility to Public

### 1. Choose the source-code license

**OPEN — owner decision required.**

No license is currently selected. Do not label the project "open source" until a license is explicitly committed.

Possible directions to evaluate separately:

- permissive open-source license;
- copyleft open-source license;
- source-available / custom terms;
- public repository with all rights reserved.

This is a legal/product decision and is intentionally not selected by an implementation agent.

### 2. Repository About text

Recommended GitHub **About** description:

> System-integrated native Android reader for Devil May Cry 3 HD resources — modular C++ parsers for MOD, SCM, DDS, PTX, stage/config files, containers and more.

Recommended topics:

`devil-may-cry` `dmc3` `reverse-engineering` `modding` `android` `cpp` `file-format` `binary-analysis` `dds` `game-modding`

### 3. Development signer

The committed `keys/dmc-native-reader-test.jks` is intentionally a **disposable development-only key** used for update compatibility across internal/debug APKs.

It is not a production secret and must **never** be used as the trust root for an official public release.

Before publishing a production APK:

- generate/provision a separate production signing key;
- keep the production private key outside Git history;
- store CI signing material in protected repository/environment secrets;
- document the production certificate fingerprint;
- decide the migration path from development-signed APKs (normally uninstall/reinstall unless a supported signing migration is configured).

Opening the source repository and publishing a production-signed APK are therefore two separate milestones.

### 4. Visibility change

Keep the repository private until the owner has reviewed:

- README wording;
- license choice;
- public issue policy;
- public contribution policy;
- development-vs-production signing distinction.

After those are accepted, repository visibility can be changed to `Public` by the owner/admin.

## First public-debug milestone

The recommended first public phase is **Native Reader v1 / Debug & Corpus Validation**.

Public messaging should be:

- architecture milestone achieved;
- core popular DMC3 modding formats have real native readers;
- 71 known families have explicit registry contracts;
- semantic completeness is still evidence-gated per format;
- real-world device/corpus testing is now the primary goal.

The project should not claim that every recognized family is fully reversed.
