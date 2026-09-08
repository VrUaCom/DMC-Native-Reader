# Samsung My Files routing boundary

> Historical device-routing evidence from the pre-v1 development line. Version labels below (`v6`, `v7`) refer to internal test builds, not current supported releases. The stable product baseline is v1.0.0. This document is retained because the OEM routing observation remains useful when diagnosing Samsung My Files behavior.

## Device evidence through v6

Physical Samsung testing established the following sequence:

1. v6 installs successfully under the canonical package identity `com.dmcrengine.nativereader`.
2. Launching the app directly succeeds.
3. Runtime `PackageManager` probes report that this package resolves constructed VIEW intents for:
   - `application/octet-stream`
   - `audio/x-mod`
   - arbitrary provider MIME
   - untyped `content://`
   - untyped `file://`
4. Tapping the real `.mod` or `.scm` in Samsung My Files does **not** launch the app and does not show the normal Android chooser. My Files directly shows its "Search in Play Store?" unsupported-file dialog.
5. Because `MainActivity` remains at `ACTION_MAIN` after the tap, there is no evidence that a VIEW intent reached the package.

This classifies the unresolved boundary above the native decoder and above normal Android package resolution: Samsung My Files is performing a custom unsupported-file decision before, or instead of, the normal resolver for these extensions.

## v7 APK-level hardening

v7 exercised the strongest practical APK-only registration path available at that stage:

- concrete exported `DmcOpenActivity` rather than an `activity-alias`;
- canonical and fallback MIME handlers;
- typed and untyped `content://` / `file://` routes;
- explicit `.mod` / `.scm` `pathPattern` routes including dotted-path variants;
- runtime verification that `DmcOpenActivity` itself is returned by PackageManager;
- framework `MimeTypeMap` diagnostics for `mod` and `scm`;
- provider authority / MIME / path diagnostics when a file is opened through SAF.

If a physical Samsung goes directly to the My Files Play Store fallback while PackageManager probes return OK, adding more equivalent manifest filters is unlikely to solve the problem. That result indicates the stock My Files application may not be consulting the standard resolver for the unsupported-extension path on that build.

## System-level/native correction research

The original product goal explored a stronger option than an APK association: teaching Android itself that selected DMC resource extensions are first-class file types.

In AOSP the framework MIME map is built from `frameworks/base/mime/java-res/android.mime.types`. `DefaultMimeMapFactory` loads Debian mappings, Android mappings, then vendor mappings. Android mappings can intentionally override earlier extension mappings; vendor mappings are generated as put-if-absent entries and cannot override an Android override.

Possible mappings for a dedicated DMC-aware Android image:

```text
application/vnd.dmc.scm scm
application/vnd.dmc.mod mod
```

Because `.mod` is overloaded by other ecosystems, such an override is appropriate only for a dedicated/controlled DMC-aware deployment. A general-purpose Android distribution would need a disambiguation policy rather than globally taking ownership of `.mod`.

A framework/system-image patch is qualitatively different from an APK intent filter: it changes extension-to-MIME resolution before a file manager chooses a viewer.

## Current product boundary

Stable Native Reader v1.0.0 remains an ordinary Android application with explicit MOD/SCM/DDS/PTX routing declarations. System-image/framework modifications are research and are not required to build or use the public app.
