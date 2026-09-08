# DMC Native Reader — Public Repository Opening Checklist

This checklist separates **source repository opening**, **GitHub project configuration**, and **production APK distribution**.

## Completed in the public-prep branch

- [x] Stable v1.0.0 product surface documented as MOD / SCM / DDS / PTX only.
- [x] Architecture v2 / evidence-aware rules documented.
- [x] Product vision documented: **Make DMC resources feel like ordinary files.**
- [x] DMC Rengine documented as the central decompilation/reimplementation engine and C++20 modding foundation.
- [x] Native Reader documented as the viewing/accessibility product built on DMC Rengine.
- [x] Android / iOS / Windows / Web direction documented with one central C++20 semantic authority; Web uses WebAssembly rather than a second JS/TS parser stack.
- [x] README, changelog, contribution, support and security policies aligned with v1.0.0.
- [x] Public-source debug builds isolated as `com.dmcrengine.nativereader.debug`.
- [x] Committed development test keystore removed from the current tree.
- [x] Signing/private-key file patterns added to `.gitignore`.
- [x] CI rejects tracked key material.
- [x] Production release workflow uses protected secret injection rather than generating/uploading production keys.
- [x] Production certificate fingerprint pinned in release CI.
- [x] Production package remains `com.dmcrengine.nativereader` and ordinary Gradle release output remains unsigned.
- [x] Production certificate and accepted v1.0.0 APK checksum documented.
- [x] Custom project license selected and committed: **DMC Native Reader Personal Non-Commercial License 1.0**.
- [x] License explicitly permits personal non-commercial use, prohibits third-party commercial use, and contains a Capcom Special Grant.
- [x] Third-party/vendored licensing documented separately; vendored DMC Rengine core remains MIT-licensed.
- [x] Canonical v1.0.0 release/download URLs are present in README and release notes.

## Canonical v1.0.0 download path

GitHub Release page:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/v1.0.0`

Direct APK path:

`https://github.com/VrUaCom/DMC-Native-Reader/releases/download/v1.0.0/DMC-Native-Reader-v1.0.0.apk`

Accepted APK SHA-256:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

Production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

**Important:** the URLs above become live only after the GitHub Release/tag `v1.0.0` is created and the accepted signed APK is attached. Do not change repository visibility until that is verified from a logged-out/public view.

## Remaining hard blockers before changing visibility to Public

### 1. Remove or expire the historical production-key backup artifact

**BLOCKER.**

Private production-signing workflow run `34113850403` created a one-day backup artifact containing the production keystore/passphrase.

Scheduled expiry:

`2026-09-08T10:56:52Z`

Do **not** make the repository public while that artifact remains accessible.

Preferred action: delete it from GitHub Actions manually now. Otherwise wait until GitHub confirms it has expired. The private backup kept by the owner must remain outside GitHub source history and outside public artifacts.

### 2. Remove the obsolete iOS prerelease

**BLOCKER / public-facing cleanup.**

The repository still exposes historical prerelease/tag `ios-unsigned-latest` with `DMCReader-unsigned.ipa` and pre-v1 claims that no longer represent the v1 product.

Before Public:

1. delete the `ios-unsigned-latest` GitHub Release;
2. delete the `ios-unsigned-latest` tag;
3. verify the Releases page no longer shows the old pre-v1 iOS experiment.

Historical iOS work can remain as development history/documentation, but it must not be presented as a current download.

### 3. Publish the accepted v1.0.0 GitHub Release

**BLOCKER.**

Create release/tag `v1.0.0`, target the accepted stable baseline, and attach exactly:

`DMC-Native-Reader-v1.0.0.apk`

The attached APK must match:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

Use `docs/releases/v1.0.0.md` as the release body. After upload, verify the direct download URL above.

The connected GitHub automation used for repository editing does not have the repository administration/release-write capability required to complete this action safely, so this final release publication remains an owner/admin GitHub action.

### 4. Historical branches and refs

**OPEN — cleanup decision required.**

Repository visibility applies to all branches/refs, not only `main`.

The repository currently contains many pre-v1 architecture/feature/fix/test/release branches. Before opening, choose which development history is intentionally public.

Recommended minimum public set:

- `main`;
- `baseline/v1.0.0`;
- `main.2` only if the legacy "to be reworked" source is intentionally being preserved publicly;
- active development branches that are intentionally public.

Obsolete architecture/feature/test branches that add no public value should be pruned to reduce confusion.

### 5. Commit metadata privacy

**OPEN — owner privacy decision required.**

Existing Git history contains author email metadata from private development.

If exposing that address is acceptable, no history rewrite is necessary. If not, sanitize/rewrite history before switching visibility. A clean public mirror starting at the accepted v1 baseline remains an alternative.

## GitHub repository configuration before opening

### About description

Recommended:

> DMC3 HD resource viewer built on DMC Rengine C++20 — open MOD/SCM models and DDS/PTX textures directly on everyday devices.

### Topics

Recommended:

`devil-may-cry-3` `dmc3` `dmc-rengine` `reverse-engineering` `cpp20` `android` `resource-viewer` `model-viewer` `texture-viewer` `modding` `binary-formats` `webassembly`

### Main-branch protection / ruleset

Recommended rules for `main`:

- require a pull request before merge;
- require conversation resolution;
- block force pushes and deletion;
- require the core architecture workflow;
- require the DDS/PTX core gate;
- require the v1 hardening workflow;
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

Do not expose production signing secrets to pull requests from forks.

## Protected production signing setup

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

1. Finish public-prep review and ensure all required CI gates are green.
2. Merge PR #28 into `main`.
3. Delete/confirm expiry of the historical production-key backup artifact.
4. Delete the obsolete `ios-unsigned-latest` release and tag.
5. Publish GitHub Release `v1.0.0` with the accepted signed APK and release notes.
6. Verify the direct APK download link and SHA-256.
7. Prune or intentionally retain historical branches; resolve commit-email privacy decision.
8. Configure About text, topics and `main` ruleset.
9. Enable GitHub security features/private vulnerability reporting.
10. Configure/verify the protected `production` signing environment.
11. Change repository visibility from Private to Public.
12. Verify as a logged-out visitor: README, license, CI badges, release page, direct APK download, issues, support/security links and third-party notices.

Only after these gates should the repository be announced publicly.
