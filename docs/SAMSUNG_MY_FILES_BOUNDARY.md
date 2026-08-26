# Samsung My Files routing boundary

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

## v7 final APK-level hardening

v7 exercises the strongest practical APK-only registration path:

- concrete exported `DmcOpenActivity` rather than an `activity-alias`;
- canonical and fallback MIME handlers;
- typed and untyped `content://` / `file://` routes;
- explicit `.mod` / `.scm` `pathPattern` routes including dotted-path variants;
- runtime verification that `DmcOpenActivity` itself is returned by PackageManager;
- framework `MimeTypeMap` diagnostics for `mod` and `scm`;
- provider authority / MIME / path diagnostics when a file is opened through SAF.

If a physical Samsung still goes directly to the My Files Play Store fallback while all v7 PackageManager probes return OK, do not add more equivalent manifest filters. That result proves the stock My Files application is not consulting the standard resolver for the unsupported extension path on that build.

## System-level/native correction

The original product goal is stronger than an APK association: teach Android itself that DMC SCM/MOD are first-class file types.

In AOSP the framework MIME map is built from `frameworks/base/mime/java-res/android.mime.types`. `DefaultMimeMapFactory` loads Debian mappings, Android mappings, then vendor mappings. Android mappings can intentionally override earlier extension mappings; vendor mappings are generated as put-if-absent entries and cannot override an Android override.

Canonical DMC mappings for a custom system image:

```text
application/vnd.dmc.scm scm
application/vnd.dmc.mod mod
```

Because `.mod` is overloaded by other ecosystems, this override is appropriate only for a dedicated DMC-aware Android image or another explicitly controlled deployment profile. A general-purpose production Android distribution would need a different disambiguation policy rather than globally stealing `.mod`.

A framework/system-image patch is qualitatively different from an APK intent filter: it changes the extension-to-MIME decision used by Android framework code before a file manager chooses a viewer. It therefore matches the project's original native-integration goal.

## Acceptance ladder

### APK v7
- install over v6;
- direct launch succeeds;
- real-handler path probes all PASS;
- record `system MIME: mod=... scm=...`;
- tap real `.mod/.scm` in Samsung My Files;
- if routed, capture actual incoming Intent and provider diagnostics;
- confirm decoder + geometry render.

### System integration
If Samsung bypass persists:
- patch framework MIME map;
- build/boot a controlled Android image or equivalent system-level deployment;
- confirm `MimeTypeMap.getMimeTypeFromExtension("scm") == application/vnd.dmc.scm`;
- confirm MOD mapping according to selected policy;
- confirm My Files / DocumentsUI no longer classifies the file as unsupported;
- confirm DMC reader/viewer consumes the resource.
