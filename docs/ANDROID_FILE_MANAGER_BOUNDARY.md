# Android / OEM file-opening boundary

Last updated: 2026-09-10.

## Current resolved product status

This document began as a v6/v7 investigation of OEM file manager behavior. Those sections are preserved below as **historical routing evidence**; they are no longer the current unresolved product status.

By the accepted Native Reader v24 baseline, physical Android device testing confirms that the supported production families **MOD, SCM, DDS and PTX open successfully in Native Reader**, and PTX model texture application works. The owner accepted v24 on 2026-09-10 and Android reported 2.32 MB installed size.

Therefore:

- do not describe OEM routing for supported v24 files as generally unresolved;
- keep the earlier v6/v7 evidence because it documents an OEM/file-manager boundary encountered during development;
- treat any new routing regression as device/Android/file-manager specific and reproduce it against the current build before changing manifest policy.

The current production package remains `com.dmcrengine.nativereader`.

## Historical evidence — v6 routing investigation

Physical Android device testing established the following sequence at that stage:

1. v6 installed successfully under `com.dmcrengine.nativereader`.
2. Direct app launch succeeded.
3. Runtime `PackageManager` probes resolved constructed VIEW intents for several fallback MIME/URI combinations.
4. Tapping the real `.mod` or `.scm` in that OEM file manager build did not launch the app or show the normal chooser; My Files displayed its own unsupported-file/Play Store path.
5. `MainActivity` remained at `ACTION_MAIN`, so there was no evidence that a VIEW intent reached Native Reader.

At that time this correctly classified the failure above the native decoder and above ordinary package resolution: the OEM file manager appeared to make its own unsupported-file decision.

## Historical evidence — v7 APK-level hardening

v7 exercised a stronger package-registration surface:

- concrete exported `DmcOpenActivity`;
- canonical and fallback MIME handlers;
- typed/untyped `content://` and `file://` routes;
- explicit `.mod` / `.scm` path-pattern routes;
- runtime verification of the resolved handler;
- framework MIME diagnostics;
- provider authority/MIME/path diagnostics for SAF/opened resources.

The important lesson remains valid: if an OEM file manager bypasses Android's standard resolver before dispatch, adding equivalent manifest filters repeatedly is not a parser fix and may not change behavior.

## System-level integration research

The stronger historical product question was whether Android itself could learn custom DMC SCM/MOD MIME mappings. A controlled Android/system-image profile can map custom extensions at the framework MIME layer, which is qualitatively different from an application intent filter.

This remains research for controlled deployments, not a requirement for the accepted v24 Android app. `.mod` is overloaded by other ecosystems, so a global system mapping must be used only under an explicit DMC-aware deployment policy.

## Current regression procedure

If file opening fails on a modern build:

1. record Native Reader version/versionCode and commit;
2. record device, Android version and file manager/provider;
3. test direct app launch;
4. test Android system file picker / SAF when applicable;
5. record the incoming intent/provider MIME/path if Native Reader receives one;
6. distinguish routing failure from parser/session failure;
7. retest MOD, SCM, DDS and PTX separately rather than assuming one extension represents the whole routing surface;
8. only change manifest/system routing when evidence identifies that layer as the failure.

Historical v6/v7 behavior must not override newer physical-device acceptance evidence.
