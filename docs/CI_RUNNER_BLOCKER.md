# Cross-platform preview CI runner blocker

Date: 2026-09-07

PR: #29 — Add iOS and Windows Native Reader preview shells

Latest cross-platform run: `34152409174`

Observed behavior:

- `Ubuntu runner probe` fails before executing any step;
- `Windows x64 preview (MinGW)` fails before executing any step;
- `iOS unsigned preview` fails before executing any step;
- job step lists are empty and no job logs are produced;
- re-running failed jobs reproduces the same pre-step failure;
- the release publication job is correctly skipped because its required builds did not succeed.

This establishes that the current blocker is outside the iOS/Windows source compilation path. The repository/account GitHub Actions execution path must be restored before CI can prove or publish either preview.

Do not:

- relabel the historical iOS IPA as the new application;
- create a Windows preview release without a successful build;
- mark iOS or Windows stable based on source presence alone.

Once Actions runners execute normally, the required order is:

1. run the Ubuntu probe;
2. build Windows x64 preview;
3. build unsigned iOS preview;
4. inspect build logs and fix any real compiler regressions;
5. download and verify both artifacts;
6. update `ios-unsigned-latest` in place with the newly built IPA;
7. create/update `windows-preview-latest` with the Windows ZIP;
8. perform real-device/corpus acceptance before any stable promotion.
