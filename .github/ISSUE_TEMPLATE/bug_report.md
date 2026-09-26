---
name: Bug report
title: "[BUG] "
about: Report a Native Reader routing, parsing, inspection, rendering or texture problem
labels: bug
---

## Native Reader build

- Platform (Android / Windows):
- Version / versionCode:
- Commit (if self-built):
- Release artifact or development branch:

## Environment

- Device / PC model:
- OS and version:
- Opening path (Open dialog, drag-and-drop, Android SAF/file manager, Open With, other):

## Resource

- Filename:
- Extension / expected family:
- Family shown by Native Reader:
- File size:
- SHA-256 (recommended when safe to share):
- Companion file used, if any:

Current native families include MOD, SCM, DDS, PTX, EventTbl, PAC, MOT, PNST, SHW, TSC, CLT, EFM, motion-script, collision and effect-bank paths. Platform release support may differ.

Do not upload copyrighted game archives or executable binaries unless you have redistribution rights.

## Failure layer

Choose the closest match:

- [ ] platform routing / Open With / file picker
- [ ] family recognition / fail-closed routing
- [ ] structural parsing / rejected resource
- [ ] Inspector / focused information
- [ ] 3D geometry / rotate / zoom / wireframe
- [ ] hierarchy / bones / skin information
- [ ] texture slot / PTX companion application
- [ ] DDS / PTX / TM2 image preview
- [ ] child-resource gallery/navigation
- [ ] UV gallery / UV map
- [ ] PAC / PNST assembly
- [ ] MOT / animation playback
- [ ] shadow / cloth / collision / effects
- [ ] crash / memory-safety / resource exhaustion
- [ ] other

## What happened?

Describe the exact visible result/error and the platform/version where it occurred.

## What did you expect?

Describe the expected behavior and why.

## Reproduction steps

1.
2.
3.

## Additional evidence

If available, include logs, screenshots, parser traces, a minimal legally shareable fixture, or comparison with the corresponding canonical `dmc-rengine-cpp` behavior/evidence.
