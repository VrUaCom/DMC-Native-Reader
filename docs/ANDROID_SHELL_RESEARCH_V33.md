# DMC Native Reader — Android Shell Research v33

Date: 2026-09-18
Repository: `VrUaCom/DMC-Native-Reader`
Research snapshot: `708813b60ab4f78120ef4b9601e11870b9b8fb46`
Status: **PRE-GATE-D RESEARCH / INPUT TO #44, #54, #55**
Implementation authority: Project gates still apply. This document does not unlock Phase 6.

## 1. Decision summary

The default implementation strategy for Phase 6 should be **Path B-first**:

`one minimal ReaderPlatformActivity extends android.app.NativeActivity + C++23 native application shell`

The managed shim exists only to forward Android framework callbacks that are not exposed by the public `ANativeActivityCallbacks` table but are required by the current product UX.

Current evidence identifies two independent callback gaps:

1. SAF/result-returning workflows need Activity result delivery.
2. Repeated external VIEW/EDIT/SEND delivery to an already-existing Activity uses `onNewIntent`.

The public NDK `ANativeActivityCallbacks` table covers lifecycle/window/input/configuration/low-memory callbacks but exposes neither a generic `onActivityResult` nor `onNewIntent` callback.

Therefore zero-DEX Path A remains a valid optimization/research target, but it is **not the default implementation assumption**. Path A may replace Path B only after supported public mechanisms are demonstrated end-to-end for both callback families with current product parity.

Forbidden: hidden/private APIs, reflection hacks, generated/obfuscated DEX used cosmetically, or deleting file-picker/external-open UX just to claim zero Java.

## 2. Current managed-code census

At the snapshot:
- authored Kotlin source: **0** (`app/src/main/kotlin` absent);
- authored Java source: **6 files**;
- explicit Gradle Java/Kotlin dependencies: none declared in `app/build.gradle.kts`;
- Android plugin: `com.android.application`;
- `buildFeatures.buildConfig = true`;
- manifest Activity components: **2**;
- JNI Java declarations: **23 native methods** in `NativeBridge.java`.

### Authored Java files

| File | Bytes | Current responsibility | Target disposition |
|---|---:|---|---|
| `MainActivity.java` | 55,267 | Activity, UI, navigation, SAF, URI lifecycle, composition/staging state, export | split: native controller + native presentation + tiny framework callback bridge |
| `DmcRenderView.java` | 9,291 | Bitmap/Canvas presentation, drag/pinch, camera/render flags | replace with `ANativeWindow` + native input/presentation |
| `ChildResourceBrowserView.java` | 4,845 | GridView gallery/presentation/accessibility captions | replace with native presentation; preserve accessibility baseline |
| `BlackWidowState.java` | 4,670 | passive bitmask projection only | delete once native presentation consumes Black Widow state directly |
| `NativeBridge.java` | 2,932 | `System.loadLibrary` + 23 JNI declarations | retire; Path B keeps only exact platform callback JNI allowlist |
| `DmcOpenActivity.java` | 1,164 | OEM/Samsung external-open bridge, including `onNewIntent` | fold required behavior into minimal `ReaderPlatformActivity extends NativeActivity` |

Total authored Java snapshot: **78,169 bytes source text**.

This is source-size context only; it is not equivalent to DEX bytes.

## 3. Current Android manifest surface

Current manifest uses:
- `MainActivity` as exported launcher, `launchMode="singleTop"`;
- `DmcOpenActivity` as exported external-open/share component;
- explicit VIEW/EDIT MIME filters for SCM/MOD/PTX/DDS/EventTbl;
- content/file extension routing;
- generic provider fallback;
- SEND intent routing.

The current `DmcOpenActivity` exists for OEM/Samsung compatibility and explicitly forwards:
- initial `getIntent()` in `onCreate`;
- repeated intents in `onNewIntent`;
- action/type/data/categories/flags/ClipData/URI grants.

Any native-shell migration must preserve this observable behavior before deleting the bridge.

## 4. MainActivity ownership analysis

Snapshot size: ~1,391 lines.

Observed Android/platform density:
- `Uri`: 53 references;
- `Intent`: 44;
- `ParcelFileDescriptor`: 10;
- `Toast`: 39;
- `startActivityForResult`: 6;
- `ACTION_OPEN_DOCUMENT`: 5;
- `ACTION_OPEN_DOCUMENT_TREE`: 1;
- `ACTION_CREATE_DOCUMENT`: 1;
- `onActivityResult`: present;
- `onNewIntent`: present.

### 4.1 Product semantic blockers that belong to Phase 5

The following current Java state must **not** be carried into the Phase-6 shim:

- `ArrayList<Uri> modelPartUris`
- `ArrayList<Uri> modelPartPtxUris`
- `Uri sharedModelPtxUri`
- `ArrayList<StagedAsset> stagedAssets`
- `selectedMotionIndex` where it identifies a semantic staged asset
- per-part PTX association implemented as a parallel URI array indexed by model-part position.

Concrete examples:
- MOD composition dedup/order currently compares `Uri` collections;
- `rememberPtxForPart()` stores PTX by part index into `modelPartPtxUris`;
- `reattachSavedPtxToComposite()` iterates that parallel array and calls native attachment using the same integer part index;
- `StagedAsset` identity is currently `Uri + role`.

These are direct inputs to Phase 5 (#39): WorkspaceGraph stable IDs/bindings must replace semantic URI/vector-position authority before Java retirement.

### 4.2 Portable application/controller state

Move to portable C++ controller after Phase-5 identity migration:
- active session handle/lifetime;
- navigation stack;
- pending product action state;
- selected tool/motion identity after stable-ID migration;
- presentation model;
- composition workflow sequencing;
- child navigation;
- export job state that is not Android document-handle ownership.

### 4.3 Android platform transport

Keep in `platform/android`, not product Core:
- SAF launch/result transport;
- `Uri` / `ClipData` / persistable grants;
- `ParcelFileDescriptor` acquisition;
- ContentResolver/document tree/create output transport;
- incoming Intent conversion;
- Android lifecycle/window/input/insets;
- user-visible platform error bridge.

### 4.4 Presentation-only responsibilities

Replace natively:
- toolbar/buttons/popups;
- title/info surfaces;
- child-resource gallery;
- motion/tool strip;
- dialogs/notices;
- render surface;
- visual toggle state sourced from native Black Widow/presentation model.

## 5. DmcRenderView migration contract

Current Java view owns:
- Bitmap allocation/reuse/recycle;
- Canvas scaling;
- yaw `0.65`, pitch `-0.45`, zoom `1.0`;
- zoom clamp `0.15..8.0`;
- pitch clamp approximately `-1.55..1.55`;
- drag sensitivity `0.008`;
- render throttle ~45 ms;
- render dimension cap 720;
- wireframe/hierarchy/UV flags;
- static image preview mode;
- pinch via `ScaleGestureDetector`.

Phase 6 first target is not a GPU rewrite.

Target:
`existing C++ RGBA renderer -> native presentation buffer -> ANativeWindow`

Required parity:
- static preview fit-center behavior;
- 3D preview;
- UV layout;
- hierarchy/wireframe toggles;
- drag/pinch camera behavior;
- resize/rotation/content-rect correctness;
- no use-after-release on `ANativeWindow`.

## 6. ChildResourceBrowserView migration contract

Current Java gallery is presentation-only:
- 2-column recycled grid;
- title from native child API;
- preview Bitmap from native API;
- item click returns index/title;
- each tile sets `contentDescription = label`.

Native replacement must preserve:
- selection semantics;
- preview/title behavior;
- no format heuristics;
- an accessibility replacement or explicit Gate-E disposition.

Phase 5 should ensure child semantic identity is not presentation index where ownership requires a stable ID.

## 7. Black Widow state

`BlackWidowState.java` is already a passive projection of a native bitmask.

It must not be ported 1:1 as a second C++ policy object.

Target:
- native Black Widow remains authority;
- native presentation consumes a typed native projection directly;
- Java bit constants disappear.

## 8. JNI surface

Current `NativeBridge.java` declares 23 native methods and `app_native.cpp` implements the corresponding JNI surface.

Current JNI also transports Android `Bitmap` for:
- main render;
- static image preview;
- child preview.

### Path B target JNI

Do not preserve the 23-method Java facade.

The final minimal managed bridge should expose only framework callbacks that genuinely require managed overrides, expected to be approximately:
- activity result -> native platform event;
- new intent -> native platform event.

Exact names/signatures are implementation details and must be verifier-allowlisted.

All product/session/render operations become internal C++ calls rather than Java-declared native methods.

The runtime DSO also exports `ANativeActivity_onCreate`.

## 9. Path B-first proposed shell

### Managed layer

Proposed concept:

`ReaderPlatformActivity extends android.app.NativeActivity`

Responsibilities only:
- call `super` where Android contract requires it;
- forward `onActivityResult(...)` as an opaque platform event;
- forward `onNewIntent(...)` as an opaque platform event;
- no View tree;
- no toolbar;
- no resource graph;
- no session navigation;
- no Black Widow logic;
- no DMC/file-format policy;
- no renderer;
- no PTX/MOD composition logic.

Do not add `System.loadLibrary` solely for this shim if NativeActivity manifest loading already owns library loading.

### Native entry

Manifest should use supported NativeActivity metadata:
- Activity class = the minimal subclass;
- `android.app.lib_name = "dmcviewer"`;
- default function entry `ANativeActivity_onCreate`, unless a reviewed reason exists for a different public entry.

The native shell installs lifecycle/window/input callbacks and owns the C++ controller/platform services.

### Initial vs repeated incoming intents

Initial intent:
- native layer can inspect the underlying Activity object through supported JNI/public Activity methods at startup.

Repeated delivery:
- minimal subclass forwards `onNewIntent` to native.

This separation must be device-tested with Samsung My Files/provider behavior.

## 10. Path A optimization gate

Path A may remove the final subclass only if supported public mechanisms prove both:
1. result-returning SAF/create/tree flows;
2. repeated external open/share intent delivery while the app instance is already alive.

Static claims are insufficient.

Final Path-A package proof additionally requires:
- authored Java/Kotlin application source = 0;
- generated managed application source that contributes DEX = 0;
- DEX entries = 0;
- `android:hasCode=false`;
- NativeActivity manifest wiring;
- `ANativeActivity_onCreate` export;
- no AAR/JAR contributes DEX.

## 11. BuildConfig finding

Current Gradle enables:

`buildFeatures { buildConfig = true }`

`MainActivity` uses `BuildConfig.VERSION_NAME`.

For Path A, this is a managed-code generator that must be explicitly removed/disabled or proven absent from the final DEX package. Preferred zero-DEX direction:
- disable generated BuildConfig;
- expose version/product metadata through native/package metadata instead of a generated Java class.

For Path B, remove BuildConfig use from product/controller logic anyway; the shim should not need it.

## 12. Resource/theme note

Current custom `AppTheme` is an Android resource based on `android:style/Theme.Material.NoActionBar`.

Resources are not themselves product Java semantics. Do not delete resources merely to cosmetically reduce Java.

Any resource removal/theme replacement must be justified by:
- package size;
- native-shell presentation;
- system-bar/accessibility behavior.

## 13. Thread/lifetime contract required before implementation

Before promoting native shell code, define:
- one owner thread for portable controller/session state;
- how Activity callbacks enqueue/cross to that owner;
- `ANativeActivity.env` main-thread limitation;
- global/local JNI reference ownership;
- Intent/Uri lifetime translation;
- `ANativeWindow_acquire/release`;
- `AInputQueue` attach/detach;
- surface generation/resize invalidation;
- destroy/recreate stale-event rejection;
- shutdown order for render/session/document events.

No raw framework pointer or local JNI reference may become a long-lived product identity.

## 14. Recommended implementation order after Gate D

1. **Phase-5 prerequisite:** remove Java URI/index semantic authority into stable WorkspaceGraph IDs/bindings.
2. Freeze Gate-D full managed/DEX census and behavior baseline.
3. Create portable C++ application/controller model.
4. Add Android native platform shell and `ANativeActivity_onCreate`.
5. Define thread/window/input lifetime contract and tests.
6. Replace `DmcRenderView` with `ANativeWindow` presentation + native gestures.
7. Replace child browser/native presentation; capture accessibility parity.
8. Move open/export/document transport into native Android service layer.
9. Introduce minimal `ReaderPlatformActivity extends NativeActivity` callback bridge.
10. Move initial/external-intent routing to native platform events.
11. Remove `BlackWidowState.java`, `ChildResourceBrowserView.java`, `DmcRenderView.java`, `MainActivity.java`, `DmcOpenActivity.java`, `NativeBridge.java` as their responsibilities reach parity.
12. Ensure only the minimal Path-B subclass remains.
13. Run Gate E.
14. Only then attempt Path-A shim removal if public-API proof exists and value justifies it.

## 15. Acceptance summary

### Path B expected default
- one minimal NativeActivity subclass;
- no Java View/widget/UI/product state;
- exact managed class/method allowlist;
- exact JNI callback allowlist;
- any DEX => final ART `speed` stress StorageStats policy;
- all lifecycle/SAF/external-open/accessibility/device scenarios pass.

### Path A optional optimization
- zero authored Java/Kotlin;
- zero app DEX;
- `android:hasCode=false`;
- no generated/dependency DEX;
- NativeActivity wiring + `ANativeActivity_onCreate`;
- supported public replacement for BOTH activity-result and repeated-new-intent delivery;
- baseline-only StorageStats with ART marked `NOT_APPLICABLE_ZERO_DEX`.

## 16. Evidence classification

Confirmed from current repository snapshot:
- six authored Java files, no authored Kotlin tree;
- exact current manifest components/intent filters;
- MainActivity URI/index semantic state listed above;
- DmcOpenActivity `onNewIntent` behavior;
- BuildConfig generation enabled;
- 23 NativeBridge methods;
- DmcRenderView and child-browser responsibilities.

Confirmed from public Android/NDK contracts:
- NativeActivity supports native entry via manifest native-library/function metadata;
- default native function is `ANativeActivity_onCreate`;
- `ANativeActivityCallbacks` public table has lifecycle/window/input callbacks but no general `onActivityResult` or `onNewIntent`.

Still evidence-gated:
- exact final Path-B shim method count/signatures;
- Samsung behavior after merging DmcOpenActivity responsibilities into a NativeActivity subclass;
- whether a supported zero-DEX route can close both callback gaps;
- final accessibility implementation;
- final package/DEX/installed-size deltas.

This document is research input. Gate #44 remains the authority that decides whether Phase 5 has removed all managed product-semantic blockers and may unlock #54.
