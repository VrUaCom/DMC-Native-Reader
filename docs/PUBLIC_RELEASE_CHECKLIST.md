# DMC Native Reader — Public Repository Opening Checklist

This checklist separates **source repository opening**, **GitHub project configuration** and **production APK distribution**.

## Completed in the public-prep branch

- [x] Stable v1.0.0 product surface documented as MOD / SCM / DDS / PTX only.
- [x] Architecture v2 / evidence-aware rules documented.
- [x] README, changelog, contribution and security policies aligned with v1.0.0.
- [x] Public-source debug builds isolated as `com.dmcrengine.nativereader.debug`.
- [x] Committed development test keystore removed from the current tree.
- [x] Signing/private-key file patterns added to `.gitignore`.
- [x] CI rejects tracked key material.
- [x] Production release workflow changed from key generation/upload to protected secret injection.
- [x] Production certificate fingerprint pinned in release CI.
- [x] Production release workflow no longer uploads keystores/passwords.
- [x] Production package remains `com.dmcrengine.nativereader` and ordinary Gradle release output remains unsigned.
- [x] Production certificate and original v1.0.0 APK checksum documented.

## Hard blockers before changing visibility to Public

### 1. Select the source-code license

**OPEN — owner decision required.**

No source-code license is currently selected. Do not market the repository as open source until a `LICENSE` file is committed.

Typical choices to evaluate:

- Apache-2.0 — permissive, explicit patent grant;
- MIT — very short permissive license;
- GPL-3.0 — strong copyleft;
- source-available/custom terms if public code reuse should be restricted.

The license should be chosen deliberately because it controls downstream reuse, forks and commercial redistribution.

### 2. Remove/expire the historical production-key backup artifact

**BLOCKER.**

The private v1.0.0 production-signing workflow previously uploaded a one-day backup artifact containing the production keystore/passphrase.

That artifact was created by workflow run `34113850403` and is scheduled to expire at:

`2026-09-08T10:56:52Z`

Do **not** make the repository public while that artifact is still accessible.

Preferred action: manually delete the key-backup artifact in GitHub Actions now. Otherwise wait until GitHub confirms it has expired before changing repository visibility.

The production key backup itself must remain private and stored separately from GitHub source history.

### 3. Remove the obsolete iOS prerelease

**BLOCKER / public-facing cleanup.**

The repository still has a historical prerelease/tag `ios-unsigned-latest` with asset `DMCReader-unsigned.ipa` and pre-v1 claims that no longer describe the current Android Architecture v2 product surface.

Delete that prerelease and tag before opening the repository so the Releases page does not present an obsolete iOS experiment as a current Native Reader distribution.

If historical iOS work is worth preserving, document it separately as archived research rather than leaving it as the only visible GitHub Release.

### 4. Historical branches and refs

**OPEN — owner/admin cleanup decision required.**

Repository visibility applies to all branches/refs, not only `main`.

The repository contains many historical branches (`architecture/*`, `feature/*`, `fix/*`, `integrate/*`, `test/*`, release branches, `main.2`, old public-opening work, etc.). Decide which are intentionally part of the public development record and prune obsolete/private branches before opening.

At minimum keep:

- `main`;
- `baseline/v1.0.0`;
- active development branches that are intentionally public.

Historical branches that are not useful to public contributors should be removed to reduce confusion and accidental exposure.

### 5. Commit metadata privacy

**OPEN — privacy decision required.**

Existing Git history contains author email metadata from private development.

If exposing that address is acceptable, no history rewrite is necessary. If not, sanitize/rewrite history before switching visibility. Changing the GitHub commit-email preference only affects future commits.

A clean public mirror starting at the v1.0.0 baseline is an alternative if preserving the full private-development history is not valuable.

## GitHub repository configuration before opening

### About description

Recommended:

> Native Android reader for Devil May Cry 3 HD resources — evidence-aware C++20 support for MOD, SCM, DDS and PTX.

### Topics

Recommended topics:

`devil-may-cry-3` `dmc3` `reverse-engineering` `android` `cpp20` `modding` `binary-formats` `model-viewer` `texture-viewer` `game-modding` `dmc-rengine`

### Main-branch protection / ruleset

Recommended rules for `main`:

- require a pull request before merge;
- require conversation resolution;
- block force pushes and deletion;
- require the core architecture workflow;
- require the DDS/PTX core gate;
- require the v1 hardening workflow;
- require branches to be up to date where practical;
- allow repository owner emergency bypass only.

### Merge hygiene

Recommended:

- enable squash merge;
- enable rebase merge if desired;
- disable ordinary merge commits for feature PRs if a linear public history is preferred;
- enable automatic deletion of merged head branches.

### Security settings

Enable before/publication where available:

- private vulnerability reporting;
- Dependabot security updates/alerts;
- secret scanning and push protection;
- dependency graph.

Do not expose production signing secrets to pull requests from forks.

## Protected production signing setup

Create a GitHub Environment named `production` and require owner/reviewer approval.

Configure these secrets from the private v1 production key backup:

- `ANDROID_RELEASE_KEYSTORE_B64` — base64 of the production keystore;
- `ANDROID_RELEASE_STORE_PASSWORD`;
- `ANDROID_RELEASE_KEY_ALIAS`;
- `ANDROID_RELEASE_KEY_PASSWORD`.

The current release workflow verifies the pinned production certificate SHA-256:

`2d82bd3e77b2c1882d3f8143fe8760fc4c834aa65fc5e7b1b12082afcb7718d1`

Do not store the keystore or passwords as repository files or Actions artifacts.

## v1.0.0 GitHub Release

Before announcing the repository publicly, create a GitHub Release for `v1.0.0` and attach:

- the signed `DMC-Native-Reader-v1.0.0.apk`;
- release notes;
- APK SHA-256;
- production certificate SHA-256.

Recorded v1.0.0 APK SHA-256:

`a81ef5555ecc67e0213d2f1f6baa351609a659c5898ba8f71f81d2fc2cefc68c`

## Public opening sequence

1. Merge the public-prep PR after all CI gates are green.
2. Choose and commit the source license.
3. Delete or wait for expiry of the historical production-key backup artifact.
4. Delete the obsolete `ios-unsigned-latest` prerelease/tag.
5. Decide whether to prune/sanitize historical branches and commit metadata.
6. Configure About text, topics and `main` protection/ruleset.
7. Enable GitHub security features/private vulnerability reporting.
8. Configure protected `production` signing secrets/environment.
9. Create the `v1.0.0` GitHub Release with signed APK and evidence.
10. Change repository visibility from Private to Public.
11. Verify the repository as a logged-out visitor: README, CI badges, release asset, issues, license and security links.

Only after these gates should the repository be announced as public/open source.
