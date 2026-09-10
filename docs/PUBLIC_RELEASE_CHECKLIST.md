# DMC Native Reader — Public Repository Opening Checklist

This checklist separates **source repository opening**, **stable Android production distribution**, and **cross-platform preview distribution**.

## Completed in the public-prep line

- [x] Stable v1.0.0 product surface documented as MOD / SCM / DDS / PTX only.
- [x] Architecture v2 / evidence-aware rules documented.
- [x] Product vision documented: **Make DMC resources feel like ordinary files.**
- [x] DMC Rengine documented as the central decompilation/reimplementation engine and C++20 modding foundation.
- [x] Native Reader documented as the viewing/accessibility product built on DMC Rengine.
- [x] Android / iOS / Windows / Web direction documented with one C++20 semantic authority; Web uses WebAssembly rather than a second JS/TS parser stack.
- [x] README, changelog, contribution, support and security policies aligned with v1.0.0.
- [x] Public-source Android debug builds isolated as `com.dmcrengine.nativereader.debug`.
- [x] Committed development test keystore removed from the current tree.
- [x] Signing/private-key file patterns added to `.gitignore`.
- [x] CI rejects tracked key material.
- [x] Android production release workflow uses protected secret injection rather than generating/uploading production keys.
- [x] Android production certificate fingerprint pinned in release CI.
- [x] Production package remains `com.dmcrengine.nativereader` and ordinary Gradle release output remains unsigned.
- [x] Production certificate and accepted Android v1.0.0 APK checksum documented.
- [x] Custom project license selected and committed: **DMC Native Reader Personal Non-Commercial License 1.0**.
- [x] Third-party/vendored licensing documented separately; vendored DMC Rengine core remains MIT-licensed.
- [x] Canonical Android v1.0.0 release/download URLs are present in README and release notes.
- [x] iOS and Windows preview shells implemented on a dedicated cross-platform branch over the same four-format C++20 core.
- [x] Cross-platform preview CI/publish contract defined.

## Stable Android v1.0.0 download path

GitHub Release page:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/v1.0.0`

Direct APK path:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/download/v1.0.0/DMC-Native-Reader-v1.0.0.apk`

Accepted APK SHA-256:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

Production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

**Important:** these stable URLs become live only after the GitHub Release/tag `v1.0.0` is created and the accepted signed APK is attached.

## Remaining hard blockers before changing visibility to Public

### 1. Remove or expire the historical production-key backup artifact

**BLOCKER.**

Private production-signing workflow run `34113850403` created a one-day backup artifact containing the production keystore/passphrase.

Scheduled expiry:

`2026-09-08T10:56:52Z`

Do **not** make the repository public while that artifact remains accessible.

Preferred action: delete it from GitHub Actions manually now. Otherwise wait until GitHub confirms it has expired. The private backup kept by the owner must remain outside GitHub source history and outside public artifacts.

### 2. Convert the retained iOS preview in place

**BLOCKER / public-facing cleanup. DO NOT DELETE THE RELEASE OR TAG.**

The historical release/tag `ios-unsigned-latest` is retained as the permanent moving **iOS preview** line.

Current obsolete state:

- title: `DMC Reader for iOS — unsigned build`;
- asset: `DMCReader-unsigned.ipa`;
- body claims pre-v1 SCM/MOD/HITS/TXT/index behavior;
- target points at an old pre-v1 implementation.

Required replacement state, only after the current iOS preview build passes:

1. keep tag/release `ios-unsigned-latest`;
2. retarget the moving tag to the accepted current preview commit;
3. rename the release to **DMC Native Reader for iOS — Preview**;
4. replace the old asset with `DMC-Native-Reader-iOS-v1.0.0-unsigned.ipa`;
5. use `docs/releases/ios-preview.md` as the release body;
6. remove the old HITS/TXT/index product claims;
7. verify that the replacement app exposes only MOD / SCM / DDS / PTX through the current Architecture v2 path;
8. keep the release marked **prerelease** because the artifact is unsigned and not App Store/TestFlight production distribution.

Do not simply relabel the existing `DMCReader-unsigned.ipa`. The current app must build successfully first and the binary must actually be replaced.

### 3. Publish the Windows preview after a successful x64 build

**BLOCKER / public-facing completeness for the newly announced Windows preview.**

Required preview line:

- tag: `windows-preview-latest`;
- title: **DMC Native Reader for Windows — Preview**;
- asset: `DMC-Native-Reader-Windows-v1.0.0-preview.zip`;
- release body: `docs/releases/windows-preview.md`;
- state: prerelease.

The package must contain the current native Win32/x64 `DMC-Native-Reader.exe` built against the same four-format C++20 core. Do not publish a placeholder ZIP or untested executable.

### 4. Publish the accepted stable Android v1.0.0 GitHub Release

**BLOCKER.**

Create release/tag `v1.0.0`, target the accepted stable baseline, and attach exactly:

`DMC-Native-Reader-v1.0.0.apk`

The attached APK must match:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

Use `docs/releases/v1.0.0.md` as the release body. After upload, verify the direct download URL above.

This stable release remains Android-only. The iOS and Windows preview assets belong on their own prerelease lines and must not be bundled into the accepted Android v1.0.0 release as if all platforms had equal stability.

### 5. Historical branches and refs

**OPEN — cleanup decision required.**

Repository visibility applies to all branches/refs, not only `main`.

The repository contains many pre-v1 architecture/feature/fix/test/release branches. Before opening, choose which development history is intentionally public.

Recommended minimum public set:

- `main`;
- `baseline/v1.0.0`;
- active public-prep/cross-platform branches until their PRs are merged;
- `main.2` only if the legacy "to be reworked" source is intentionally preserved publicly;
- other active development branches that are intentionally public.

Obsolete architecture/feature/test branches that add no public value should be pruned to reduce confusion.

### 6. Commit metadata privacy

**OPEN — owner privacy decision required.**

Existing Git history contains author email metadata from private development.

If exposing that address is acceptable, no history rewrite is necessary. If not, sanitize/rewrite history before switching visibility. A clean public mirror starting at the accepted v1 baseline remains an alternative.

## Cross-platform build acceptance

Before the preview releases are updated, `.github/workflows/platform-previews.yml` must prove:

### iOS

- XcodeGen project generation succeeds;
- current C++20 Architecture v2 sources compile for iOS;
- unsigned Release `.app` builds;
- IPA packages as `DMC-Native-Reader-iOS-v1.0.0-unsigned.ipa`.

### Windows

- CMake configure succeeds with x64 MSVC;
- current C++20 Architecture v2 sources compile;
- `DMC-Native-Reader.exe` is produced;
- preview ZIP packages as `DMC-Native-Reader-Windows-v1.0.0-preview.zip`.

Passing compilation is necessary but not sufficient to call either platform stable. Real-corpus/device acceptance remains required for platform promotion.

## GitHub repository configuration before opening

### About description

Recommended:

> DMC3 HD resource viewer built on DMC Rengine C++20 — open MOD/SCM models and DDS/PTX textures on Android, with native iOS/Windows previews.

### Topics

Recommended:

`devil-may-cry-3` `dmc3` `dmc-rengine` `reverse-engineering` `cpp20` `android` `ios` `windows` `resource-viewer` `model-viewer` `texture-viewer` `modding` `binary-formats` `webassembly`

### Main-branch protection / ruleset

Recommended rules for `main`:

- require a pull request before merge;
- require conversation resolution;
- block force pushes and deletion;
- require the core architecture workflow;
- require the DDS/PTX core gate;
- require the v1 hardening workflow;
- require the platform preview workflow for changes under `ios/`, `windows/` or portable-session bridge code;
- require branches to be up to date where practical;
- allow repository-owner emergency bypass only.

### Merge hygiene

Recommended:

- enable squash merge;
- enable rebase merge if desired;
- disable ordinary merge commits for feature PRs if a linear public history is preferred;
- enable automatic deletion of merged head branches.

### Security settings

Enable before publication where available:

- private vulnerability reporting;
- Dependabot security updates/alerts;
- secret scanning and push protection;
- dependency graph.

Do not expose Android production signing secrets or future Apple/Windows signing material to pull requests from forks.

## Protected Android production signing setup

Use a GitHub Environment named `production` with owner/reviewer approval.

Configure these secrets from the private v1 production key backup:

- `ANDROID_RELEASE_KEYSTORE_B64`;
- `ANDROID_RELEASE_STORE_PASSWORD`;
- `ANDROID_RELEASE_KEY_ALIAS`;
- `ANDROID_RELEASE_KEY_PASSWORD`.

The release workflow verifies the pinned production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

Do not store the keystore or passwords as repository files or Actions artifacts.

## Public opening sequence

1. Finish public-prep review and ensure required Android/core CI gates are green.
2. Review/merge the cross-platform preview PR without describing iOS/Windows as stable.
3. Merge PR #28 into `main` when its required gates are green.
4. Delete/confirm expiry of the historical production-key backup artifact.
5. Run and pass the iOS + Windows platform preview builds.
6. Replace/rename the retained `ios-unsigned-latest` release in place with the current unsigned iOS preview asset and notes.
7. Create/update `windows-preview-latest` with the successful Windows preview ZIP and notes.
8. Publish stable GitHub Release `v1.0.0` with the accepted signed Android APK and release notes.
9. Verify the Android direct APK download link and SHA-256.
10. Verify the iOS/Windows preview pages clearly say **Preview** and expose no obsolete format claims.
11. Prune or intentionally retain historical branches; resolve commit-email privacy decision.
12. Configure About text, topics and `main` ruleset.
13. Enable GitHub security features/private vulnerability reporting.
14. Configure/verify the protected Android `production` signing environment.
15. Change repository visibility from Private to Public.
16. Verify as a logged-out visitor: README, license, CI badges, stable Android release, iOS/Windows preview pages, issues, support/security links and third-party notices.

Only after these gates should the repository be announced publicly.
