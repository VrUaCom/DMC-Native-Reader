# DMC Native Reader

Native Android reader for Devil May Cry 3 HD Collection resources.

## Main baseline

`main` is intentionally narrow. The supported Native Reader 1.0 surface is exactly:

- **MOD** — canonical `dmc-rengine-cpp` structural reader -> Architecture v2 adapter -> `RenderScene` / `InspectionDocument`.
- **SCM** — canonical `dmc-rengine-cpp` structural reader -> Architecture v2 adapter -> `RenderScene` / `InspectionDocument`.
- **DDS** — bounded DMC3 DDS validation and generic `ImagePreview`.
- **PTX** — bounded texture-bundle reader with generic DDS child resources and parent navigation.

Everything else is excluded from the main module registry until it is promoted to the same modular contract.

## Architecture rule

```text
file / child resource
        |
        v
bounded core probe
        |
        v
NativeModuleRegistry
        |
        +--> MOD adapter --> RenderScene + InspectionDocument
        +--> SCM adapter --> RenderScene + InspectionDocument
        +--> DDS module  --> ImagePreview + InspectionDocument
        `--> PTX module  --> ChildResource[] + InspectionDocument
        |
        v
Generic JNI Session
        |
        v
Capability-driven Android UI
```

No wildcard format module, no private Java parser, and no legacy `DecodeResult -> Mesh -> RenderScene` compatibility bridge is allowed in `main`.

## Archived work

Branch **`main.2`** preserves the pre-cleanup multi-format state and is marked **до опрацювання**. HITS, TXT/index, DCA, LIG/LIG2, PAC/PNST, NBZ, EFM/MRP/SHW and the wider recognition catalog remain there as backlog/reference until they receive canonical modular promotion.

## Authority

`MOD` and `SCM` parsing authority is vendored from `VrUaCom/dmc-rengine-cpp`. Unknown/unpromoted formats fail closed in the Native Reader 1.0 pipeline.
