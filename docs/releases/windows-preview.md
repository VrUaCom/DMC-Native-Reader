# DMC Native Reader for Windows — Preview

This is the first native Windows shell for DMC Native Reader.

## Product identity

- name: **DMC Native Reader**
- platform: Windows x64
- shell: native Win32
- semantic/parser authority: shared C++20 Architecture v2 core
- supported promoted families: **MOD / SCM / DDS / PTX**

The Windows application does not contain a separate Windows-only parser. It compiles the same `NativeModuleRegistry`, canonical MOD/SCM adapters, DDS/PTX modules, DMC Rengine vendor readers and CPU renderer used by the other Native Reader shells.

## Capabilities

- File > Open and drag-and-drop opening;
- command-line file opening, including Windows **Open with** handoff;
- MOD/SCM 3D rendering;
- mouse drag rotation and wheel zoom;
- wireframe and reset controls;
- typed Inspector output;
- DDS preview;
- PTX child texture navigation with Left/Right keys;
- fail-closed handling of unsupported/unpromoted resources;
- read-only source access.

## Windows Open with integration

The preview ZIP contains two optional per-user PowerShell helpers:

- `Register-OpenWith.ps1` — adds DMC Native Reader to **Open with** for `.mod`, `.scm`, `.dds` and `.ptx`;
- `Unregister-OpenWith.ps1` — removes only the Native Reader registration.

The registration is stored under the current user's `HKCU\Software\Classes` tree. It deliberately **does not set Native Reader as the default application** for any format and does not overwrite an existing DDS/image association.

The script expects `DMC-Native-Reader.exe` to remain beside it. Users can still use File > Open, drag-and-drop, or invoke the EXE with a resource path without registering anything.

## Controls

- mouse drag — rotate 3D resource;
- mouse wheel — zoom;
- `W` — wireframe;
- `R` — reset view;
- `I` — Inspector;
- Left / Right — change PTX child texture;
- Ctrl+O — open file.

## Build from source

From a Visual Studio Developer environment or a machine with Visual Studio C++ build tools and CMake:

```powershell
cmake -S windows -B build/windows -A x64
cmake --build build/windows --config Release
```

The executable is produced as `DMC-Native-Reader.exe`.

The CI preview path may also cross-build the same Win32/C++20 source with MinGW x86-64 on an Ubuntu runner. This is a build-host difference only; it does not introduce a second parser or platform-specific resource semantics.

## Preview package

Accepted preview package name:

```text
DMC-Native-Reader-Windows-v1.0.0-preview.zip
```

The package is expected to contain:

```text
DMC-Native-Reader.exe
Register-OpenWith.ps1
Unregister-OpenWith.ps1
LICENSE.txt
THIRD_PARTY_NOTICES.md
```

## Preview status

The Windows package is a technical preview until it has passed the same real-corpus/device acceptance process as the Android v1.0 baseline. It must not be described as a stable Windows v1 release before that acceptance pass is complete.
