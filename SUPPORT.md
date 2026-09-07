# Support

DMC Native Reader is a source-available community resource-viewing tool in the DMC Rengine ecosystem. Support is best-effort and evidence-driven.

## Where to ask

Use GitHub Issues in this repository for:

- reproducible Native Reader bugs;
- Android v1 `Open with` / SAF routing problems;
- MOD/SCM rendering or inspection regressions;
- DDS/PTX validation or preview problems;
- child-resource navigation problems;
- Native Reader platform-shell issues;
- evidence-backed reports that affect the Reader product surface.

Use the central [`VrUaCom/dmc-rengine-cpp`](https://github.com/VrUaCom/dmc-rengine-cpp) repository for engine-level reverse engineering, canonical format semantics, writer/runtime work, or reusable C++20 capabilities that belong in DMC Rengine rather than in a Native Reader-specific adapter.

## Before opening an issue

Please include:

- Native Reader version;
- platform/device and OS version;
- resource family/extension;
- exact behavior or error;
- reproduction steps;
- file size and SHA-256 when safe to share;
- screenshots/logs where useful.

Do not upload proprietary game archives, executable binaries, leaked source, credentials or production signing material.

## Supported v1 product surface

The stable v1.0.0 registry is intentionally limited to:

- MOD;
- SCM;
- DDS;
- PTX.

Android is the stable v1 shell. iOS, Windows and Web are strategic platform directions, not claims of stable v1 distribution support.

A historical branch containing another reader does not mean that format or platform is currently supported in the production release.

## License/support boundary

DMC Native Reader is licensed for personal non-commercial use under the project `LICENSE`, with the separate Capcom Special Grant defined there. Support availability does not grant commercial-use rights.

## Security

For crashes that appear exploitable, parser memory-safety issues, credential exposure or release-signing problems, follow [`SECURITY.md`](SECURITY.md) instead of opening a detailed public issue.
