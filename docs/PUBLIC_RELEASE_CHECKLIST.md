# DMC Native Reader — Public Repository Opening Checklist

This checklist separates **opening the source repository** from publishing an **official production-signed APK**.

## Product baseline

- [x] Native Reader v1 architecture frozen.
- [x] v1 real-device acceptance completed for MOD, SCM, DDS and PTX feature paths.
- [x] `1.0.0-rc1` release-candidate branch defined.
- [x] System-integrated Android file opening path documented.
- [x] Native C++20 `NativeModuleRegistry` architecture documented.
- [x] 71 recognized DMC families represented by 71 explicit module contracts.
- [x] Core modding readers documented: MOD, SCM, DDS, PTX, TXT and `.index`.
- [x] Additional promoted readers documented: HITS, DCA, LIG/LIG2, PAC, PNST and NBZ.
- [x] Partial/evidence-gated status is explicit for EFM, MRP and SHW.
- [x] SO is not misrepresented as a completed v1 semantic reader.
- [x] Unknown-family behavior is fail-closed.
- [x] Generic `ImagePreview` / `ChildResource` nested-resource UI is documented.
- [x] v1 RC automated release gates exist.

## Public-facing repository material

- [x] README explains the project, current RC line and evidence policy.
- [x] Architecture and evidence policy are visible from the repository front page.
- [x] Format support is described by maturity instead of a misleading supported/unsupported binary.
- [x] Capcom / Devil May Cry affiliation disclaimer is present.
- [x] Project contains no Capcom game archives, proprietary game files or game executable binaries.
- [x] Contribution guide added.
- [x] Security policy added.
- [x] Bug / real-file debug issue templates added.
- [x] Development signing-key boundary documented.
- [x] Public roadmap and changelog added.
- [x] RC release/device/evidence checklists added.

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

> System-integrated native Android reader for Devil May Cry 3 HD resources — modular C++20 readers for MOD, SCM, DDS, PTX, stage/config files, containers and more.

Recommended topics:

`devil-may-cry` `dmc3` `reverse-engineering` `modding` `android` `cpp` `file-format` `binary-analysis` `dds` `game-modding`

### 3. Review old branches before visibility change

**OPEN — owner/admin cleanup decision required.**

Repository visibility applies to historical branches as well as `main`. Review development branches before public visibility and decide which are intentionally part of the public development record.

Do not assume that a clean `main` hides content reachable from another branch/ref.

### 4. Review commit metadata / personal-email exposure

**OPEN — privacy review recommended.**

Historical Git commits may contain author/committer email metadata. Before public visibility, review whether the existing history exposes a personal email address that should instead use a GitHub `noreply` identity.

If privacy cleanup is required, rewrite/sanitize history before opening the repository. Changing the account's future commit-email setting does not retroactively rewrite existing commits.

### 5. Production signer

The committed `keys/dmc-native-reader-test.jks` is intentionally a **disposable development-only key** used for internal/debug APK update compatibility.

It is not a production secret and must **never** be used as the trust root for an official stable release.

Before publishing a production APK:

- generate/provision a separate production signing key;
- keep the private key outside Git history;
- store CI signing material in protected repository/environment secrets;
- document the production certificate fingerprint;
- decide the migration path from development-signed APKs (normally uninstall/reinstall unless a supported signing migration is configured);
- record the final production APK SHA-256.

The RC pipeline intentionally builds an unsigned release APK until this authority exists.

### 6. Visibility change

Keep the repository private until the owner has reviewed:

- README wording;
- license choice;
- old branches/history;
- commit metadata/privacy;
- public issue policy;
- public contribution policy;
- development-vs-production signing distinction.

After those are accepted, repository visibility can be changed to `Public` by the owner/admin.

## Public v1 messaging

Recommended messaging for the v1 line:

- modular architecture and primary real-device flows are established;
- MOD, SCM, DDS and PTX have real native reader/preview surfaces;
- 71 known families have explicit registry contracts;
- semantic completeness remains evidence-gated per family;
- unsupported semantics are not hidden behind generic parsing;
- the product remains read-only;
- production signing and public repository licensing are separate owner-controlled release gates.

The project should not claim that every recognized family is fully reversed.
