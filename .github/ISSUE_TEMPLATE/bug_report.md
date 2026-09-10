---
name: Bug report
title: "[BUG] "
about: Report a Native Reader routing, parsing, inspection, rendering or texture problem
labels: bug
---

## Native Reader build

- Version / versionCode:
- Commit (if self-built):
- Accepted `main` or development candidate/branch:

## Device

- Device model:
- Android version:
- File manager / opening path (Samsung My Files, system Files/SAF, other):

## Resource

- Filename:
- Extension / expected production family (MOD / SCM / DDS / PTX):
- Family shown by Native Reader:
- File size:
- SHA-256 (recommended when safe to share):
- Companion file used, if any:

Do not upload copyrighted game archives or executable binaries unless you have redistribution rights.

## Failure layer

Choose the closest match:

- [ ] Android routing / `Open with`
- [ ] production family recognition / fail-closed routing
- [ ] structural parsing / rejected resource
- [ ] Inspector / focused information
- [ ] 3D geometry / rotate / zoom / wireframe
- [ ] hierarchy / bones / skin information
- [ ] texture slot / PTX companion application
- [ ] DDS image preview
- [ ] PTX or child-resource gallery/navigation
- [ ] UV gallery / UV map (development candidate where applicable)
- [ ] crash / memory-safety / resource exhaustion
- [ ] other

## What happened?

Describe the exact visible result/error. If the feature belongs to a development candidate, say whether the problem occurs on host tests, APK validation or the physical device.

## What did you expect?

Describe the expected behavior and why.

## Reproduction steps

1.
2.
3.

## Additional evidence

If available, include logs, screenshots, parser trace, a minimal legally shareable fixture, or comparison with the corresponding canonical `dmc-rengine-cpp` behavior/evidence.
