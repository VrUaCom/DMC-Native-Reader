# Support

DMC Native Reader is a community reverse-engineering/modding tool. Support is best-effort and evidence-driven.

## Where to ask

Use GitHub Issues for:

- reproducible Native Reader bugs;
- Android `Open with` / SAF routing problems;
- MOD/SCM rendering or inspection regressions;
- DDS/PTX validation problems;
- evidence-backed format/corpus reports.

Use the companion [`VrUaCom/dmc-rengine-cpp`](https://github.com/VrUaCom/dmc-rengine-cpp) repository for canonical reverse/evidence work that is not specific to the Android product.

## Before opening an issue

Please include:

- Native Reader version;
- Android device/version;
- resource family/extension;
- exact behavior or error;
- reproduction steps;
- file size and SHA-256 when safe to share;
- screenshots/logs where useful.

Do not upload proprietary game archives, executable binaries, leaked source, credentials or production signing material.

## Supported product surface

The stable v1 registry is intentionally limited to:

- MOD;
- SCM;
- DDS;
- PTX.

A historical branch containing another reader does not mean that format is currently supported in the production Android application.

## Security

For crashes that appear exploitable, parser memory-safety issues, credential exposure or release-signing problems, follow [`SECURITY.md`](SECURITY.md) instead of opening a detailed public issue.
