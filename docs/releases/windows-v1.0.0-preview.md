# Windows v1.0.0 Preview — resource inspection and guides

This page connects the existing [Windows technical preview release](https://github.com/VrUaCom/DMC-Native-Reader/releases/tag/windows-v1.0.0-preview) to practical DMC Modding Hub guides. It documents that public snapshot, not a new release or a new device-acceptance result.

## Scope

- Game: **Devil May Cry 3: Special Edition** inside **Devil May Cry HD Collection**.
- Platform: **Windows x64**, portable **v1.0.0 Preview**.
- Release-documented resources: **MOD / SCM / DDS / PTX**, read-only.
- Source snapshot: `c74589fe27e405341489cdea56e5227215da1612`.

The release documents interactive MOD/SCM viewing, render overlays, DDS/PTX image preview, PTX companion attachment and child-resource navigation. It remains an earlier snapshot than the newer Android line; Windows feature parity and equivalent physical-device acceptance are not established.

## Choose your workflow

| Task | Guide |
| --- | --- |
| Confirm the game edition and prepare a workspace | [Start modding DMC3](https://vruacom.github.io/dmc-rengine-cpp/guides/start-modding-dmc3/) |
| Obtain an extracted resource before inspection | [NBZ → PAC/PNST → leaf resource](https://vruacom.github.io/dmc-rengine-cpp/guides/extract-nbz/) |
| Open and inspect a model | [MOD inspection](https://vruacom.github.io/dmc-rengine-cpp/guides/inspect-mod/) |
| Open and inspect a scene | [SCM inspection](https://vruacom.github.io/dmc-rengine-cpp/guides/inspect-scm/) |
| Inspect a texture or texture bundle | [DDS/PTX workflow](https://vruacom.github.io/dmc-rengine-cpp/guides/extract-textures/) |

Start from the [DMC Modding Hub](https://vruacom.github.io/dmc-rengine-cpp/) for the wider guide index. Check [Downloads](https://vruacom.github.io/dmc-rengine-cpp/downloads/) for the exact ZIP, size and SHA-256, and [Capabilities](https://vruacom.github.io/dmc-rengine-cpp/capabilities/) for operation-specific limits.

Extraction is separate from viewing. These links do not add NBZ unpacking, editing, writing or repacking to Native Reader. Use working copies and preserve archive/member provenance when extracting resources.

The release's Open-With registration scripts are optional; see its original instructions for registration and reversal.

Documentation links reviewed: **2026-11-02**. No binaries, tags or release acceptance claims were changed by this documentation update.
