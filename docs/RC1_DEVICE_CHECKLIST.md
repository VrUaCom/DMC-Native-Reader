# DMC Native Reader v1.0.0 RC1 — Samsung final smoke checklist

Use only the exact RC1 debug artifact produced from the final PR head.

## Install / identity

- install over the current development build without uninstalling;
- application label is `DMC Native Reader`;
- no legacy `v8` suffix;
- Info / idle diagnostics report `1.0.0-rc1`.

## MOD

- open a real `.mod` from Samsung My Files;
- geometry renders;
- rotate and zoom work;
- wireframe toggle works;
- hierarchy button works only when spatial authority is available;
- Inspector shows skin/weights and TextureSlot / legacy GS CLAMP data.

## SCM

- open a real `.scm`;
- geometry placement remains correct;
- rotate / zoom / wireframe work;
- scene hierarchy overlay works;
- Inspector shows texture binding / GS sampler state.

## PTX / DDS

- open `pl000_000.ptx` or another validated real PTX;
- child count and texture thumbnails appear;
- tap a child DDS and confirm the full image preview;
- `←` returns to the same parent gallery;
- Android Back follows the same parent-session behavior;
- standalone DDS opens through the same ImagePreview path;
- no crash, hang or obvious memory-pressure failure.

## Regression sanity

- opening a non-renderable resource after MOD/SCM does not leave stale 3D geometry;
- Info remains available for accepted inspection resources;
- status/navigation bars do not overlap controls.

## PASS rule

RC1 device acceptance is PASS when the four primary surfaces (MOD, SCM, PTX, DDS) complete this smoke sequence with no release-blocking regression.

Production signing is a separate final stable-release gate and is not proven by this development-signed RC debug APK.
