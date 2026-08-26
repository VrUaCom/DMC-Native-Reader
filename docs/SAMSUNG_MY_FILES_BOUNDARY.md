# Samsung My Files routing boundary

## Resolved on v8

Physical Samsung testing of v8 reports that tapping a real `.mod` / `.scm` in
Samsung My Files now opens DMC Native Reader. The "Search in Play Store?"
unsupported-file dialog no longer intercepts the tap.

This falsifies the v7 working hypothesis below, which held that My Files never
consults the standard resolver for these extensions. It clearly does — the
earlier failures were a filter-matching gap on this device, not a bypass.

The only routing change between v7 and v8 is the `android:pathSuffix` filter
(API 31+), so it is the probable cause: unlike `pathPattern`, suffix matching
is applied to the decoded path and therefore survives SAF document paths whose
separators arrive percent-encoded (`primary%3ADownload%2Ffile.mod`). This is
**probable, not proven** — it was not established that v7 itself was installed
on the device and re-tested immediately before v8, so a v7-vs-v8 A/B on the
same handset would be needed to attribute the fix conclusively.

The sections below are kept as the historical record of how the boundary was
narrowed.

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

## v8 decision: stop depending on the file manager

Device feedback after v7 was unchanged: tapping a real `.mod` / `.scm` still
does not reach the app. Per the v7 rule above, v8 does **not** add further
equivalent manifest filters. Two things changed instead.

### Routing hardening (bounded)

One genuinely non-equivalent route was added: `android:pathSuffix` (API 31+).
Unlike `pathPattern`, suffix matching is applied to the decoded path, so it
still fires for SAF document URIs whose path carries an encoded `:` or `/`
(`primary%3ADownload%2Ffile.mod`). This is the last APK-level route worth
adding; anything beyond it is a duplicate of an existing filter.

### Built-in browser (the actual fix)

`DmcBrowserActivity` locates `.scm` / `.mod` without involving a file manager:

- direct filesystem scan under all-files access (`MANAGE_EXTERNAL_STORAGE`),
  which is the only permission exposing arbitrary non-media files — SCM/MOD are
  invisible to `MediaStore`, so `READ_MEDIA_*` cannot substitute for it;
- or a persisted Storage Access Framework tree the user picks once, requiring
  no permission at all.

Both sources feed the same JNI read-only descriptor path, so the decoder and
renderer are exercised identically regardless of how the file was reached.
This converts the My Files behaviour from a blocker into a cosmetic routing
gap: the user can always open their resources.

Note that the direct-scan mode returns an absolute path rather than a
`file://` URI. A file URI placed in an Intent triggers StrictMode's
`FileUriExposedException` on API 24+, even between two activities of the same
package.

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

### APK v8
- install over v6/v7 (same package identity and signer);
- direct launch succeeds;
- real-handler path probes all PASS;
- record `system MIME: mod=... scm=...`;
- **primary:** `Browse DMC files` -> grant access or pick the DMC folder ->
  the real `.scm`/`.mod` are listed -> tap one -> decoder accepts -> non-empty
  geometry renders -> rotate / pinch zoom / reset / wireframe work;
- **secondary:** tap real `.mod/.scm` in Samsung My Files; if routed, capture
  the actual incoming Intent and provider diagnostics.

A failure of the secondary route alone is no longer a milestone blocker; it is
recorded as an OEM resolver gap.

### System integration

Superseded for the routing goal: v8 routes from My Files without touching the
system image, so this ladder is no longer required to open DMC files. It
remains the only way to make `.scm` / `.mod` first-class Android file types
system-wide (correct type name and icon in every file manager), which is a
separate, optional goal.

If that goal is pursued:
- patch framework MIME map;
- build/boot a controlled Android image or equivalent system-level deployment;
- confirm `MimeTypeMap.getMimeTypeFromExtension("scm") == application/vnd.dmc.scm`;
- confirm MOD mapping according to selected policy;
- confirm My Files / DocumentsUI no longer classifies the file as unsupported;
- confirm DMC reader/viewer consumes the resource.
