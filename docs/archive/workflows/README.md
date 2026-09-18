# Archived GitHub Actions workflows

These files are historical evidence only. GitHub Actions does not execute files outside `.github/workflows/`.

## Archived release paths

- `release-v1.legacy.yml`
  - historical Android v1.0.1 production-signing workflow;
  - NDK r28-era assumptions;
  - restored production signing secrets;
  - obsolete package/version expectations.

- `publish-platform-releases.legacy.yml`
  - historical Android v1.0.1 + Windows preview build/publish workflow;
  - Android production signing;
  - `contents: write` + `gh release` publication;
  - obsolete Android/Windows version/toolchain assumptions.

## Binding rule

Do **not** move either file back into `.github/workflows/`.

During the v33 C++23 migration, active workflows are limited to:
- `.github/workflows/android.yml` — canonical candidate evidence gate;
- `.github/workflows/dds-ptx-v1.yml` — manual targeted diagnostic only;
- `.github/workflows/v1-hardening.yml` — manual hardening diagnostic only.

There is intentionally **no active production release workflow** until Phase 7 (#40) after Review Gate E (#55).

Phase 7 must create the single current Android signing/publication authority from the actual #55-reviewed Path A/Path B architecture, exact post-signing verifier contract, expected production certificate, exact APK SHA, Samsung acceptance and explicit Viktor approval.

Historical workflows remain available here and in Git history for audit/reference only.
