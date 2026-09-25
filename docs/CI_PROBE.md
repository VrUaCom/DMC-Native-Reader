# CI Probe / Hosted Runner Status

Last updated: 2026-09-10.

## Purpose

This file originally existed to prove that pull requests in the standalone `VrUaCom/DMC-Native-Reader` repository could start the Android GitHub Actions path and produce verified APK artifacts.

That original probe is now historical. Current CI should be judged by **what actually executed**, not merely by a workflow's overall red/green label.

## Current classification rule

Recent v23/v24 and v26-candidate workflow attempts have included GitHub-hosted jobs that failed before executing any build/test step and provided no useful compiler/test log. Such a result is:

- **not a green CI pass**;
- **not evidence of a source-code failure**;
- an Actions runner/execution-path blocker until a job actually starts and yields actionable logs.

Do not claim hosted CI green when no steps executed. Do not diagnose code from a runner failure that produced no code/build evidence.

## Promotion evidence when hosted runners are blocked

For accepted v24, the actual executed evidence was:

- seven passing local/native C++20 regressions;
- clean Android arm64 build;
- APK package/version/ABI verification;
- ZIP/signature/module/JNI boundary checks;
- physical Android device acceptance of all four supported families and PTX texture application.

For candidate v26, nine portable/native regressions and clean APK verification pass, while physical-device acceptance is still pending.

## Desired hosted CI behavior

When GitHub-hosted jobs execute normally, maintained workflows should run the relevant subset of:

1. portable/native regressions;
2. canonical/vendor provenance checks;
3. module-registry/fail-closed checks;
4. MOD/SCM model pipeline and projection checks;
5. DDS/PTX/companion checks;
6. feature-specific tests such as UV gallery/session inspection;
7. Android NDK/Gradle build;
8. package/version/manifest/JNI/module/signing/ZIP verification;
9. artifact publication only after preceding gates succeed.

A real failing test/build log must be fixed or explicitly invalidated by evidence before promotion. A no-step runner failure must be tracked as infrastructure separately from code correctness.
