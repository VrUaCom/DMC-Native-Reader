# PTX Runtime Import Review Template v33

Use this template for the dedicated PTX architecture review and post-implementation review.

## A. Repository boundary
- [ ] `dmc-rengine-cpp` was read only.
- [ ] No Rengine branch/commit/issue/PR/CMake/test/doc/API/target was changed.
- [ ] Imported code provenance points to read-only commit `50d070e158e484937238d9cb02b2bc6affb2f502`.

## B. Architecture
- [ ] Existing serialized PTX framing/TextureSet/DDS path remains the only file parser/decode authority.
- [ ] Runtime compatibility is an internal optional layer of the same PTX product module.
- [ ] No second user-facing PTX NativeModule was created.
- [ ] Runtime state is lazy and ordinary preview does not initialize it.

## C. Import scope
- [ ] Only confirmed pool/manager/placement/reservation/cache behavior was imported.
- [ ] Reverse-only harness/data was not shipped unnecessarily.
- [ ] Unknown palette/finalizer/cleanup behavior remains unresolved/extension-point only.
- [ ] No public API exposes raw EXE addresses or reverse-only structs.

## D. C++23 / failure contract
- [ ] Imported implementation follows the target-scoped ISO C++23 contract.
- [ ] Meaningful failure states use typed fail-closed results where appropriate.
- [ ] `noexcept` boundaries do not hide allocation/diagnostic exceptions.
- [ ] No exception crosses JNI.

## E. Regression
- [ ] Existing PTX/DDS/PNG/gallery/attachment/render regressions remain unchanged.
- [ ] `ptx_runtime_compat_test` covers initialization, placement, failure, reservation, key reset and preserved pool state.
- [ ] Serialized source PTX bytes remain unchanged by runtime inspection.

## F. Weight / duplicates
- [ ] No second PTX parser or duplicate runtime implementation exists.
- [ ] One DSO contract preserved.
- [ ] APK <= 4 MiB pre-gate remains intact.
- [ ] Installed `StorageStats.getAppBytes()` <= 4 MiB remains mandatory downstream.
- [ ] Any new native/APK bytes are attributed to required runtime compatibility code/data.

## G. Review decision
Classify findings as blocker / correction / optimization / deferred.

Decision: `GO` / `NO-GO`

If `NO-GO`, return to the PTX implementation issue. If `GO`, pass the exact HEAD/evidence into Review Gate #41.
