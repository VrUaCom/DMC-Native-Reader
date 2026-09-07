# DMC Native Reader for Windows — Preview

This is the first native Windows shell for DMC Native Reader.

## Product identity

- name: **DMC Native Reader**
- platform: Windows x64
- shell: native Win32
- semantic/parser authority: shared C++20 Architecture v2 core
- supported promoted families: **MOD / SCM / DDS / PTX**

The Windows application does not contain a separate Windows-only parser. It compiles the same NativeModuleRegistry, canonical MOD/SCM adapters, DDS/PTX modules, DMC Rengine vendor readers and CPU renderer used by the other Native Reader shells.

## Capabilities

- File > Open and drag-and-drop opening;
- MOD/SCM 3D rendering;
- mouse drag rotation and wheel zoom;
- wireframe and reset controls;
- typed Inspector output;
- DDS preview;
- PTX child texture navigation with Left/Right keys;
- fail-closed handling of unsupported/unpromoted resources;
- read-only source access.

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

## Preview status

The Windows package is a technical preview until it has passed the same real-corpus/device acceptance process as the Android v1.0 baseline. It must not be described as a stable Windows v1 release before that acceptance pass is complete.
