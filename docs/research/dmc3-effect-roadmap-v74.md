# Effect coverage: what is drawn, what is missing (v74)

## Drawn today

| Source | What plays |
| --- | --- |
| CEm034 (Lady) | Shl00/02/04/05 shells and their V/E effects, from EXE-traced triggers |
| Stage layout | `eff V id` + `epos` on objects (st002 drums), from `st*_effect.pac`; texture scroll `uv`; additive light geometry |
| Any FXBANK child | E sprites (modes 1 and 2) and P records of class 3 (CPtclSprt00, see `dmc3-particle-sprt00-exe-v75.md`) in the gallery preview |

## Not drawn

- **P records of classes 0, 1, 2, 4, 5** (Line00, Poly00, Poly01, Line01, Line02; class 3 is drawn). Sizes 336 / 528 / 704 bytes. Observed layout:
  - `+0x00` type (2), `+0x10 / +0x14 / +0x18` element counts (0x10, 0x140 / 0x20, 0x150);
  - `+0x28..+0x33` a short name (`40p0`..`40p4`), then keyed tracks from `+0x84`
    (entries of `u16 flags, u32 frames, float value`);
  - `+0x16C..` scale triples (1.0, 1.2, 1.5, 1.6, 0.5), angles in radians.
  The consumer is the registrar `0x140314B80` and `CParticle`; not traced yet.
  Every V that lists `P` children (for example V543, the rocket explosion)
  is missing its particle part.
- **G records** (generators, 96 bytes): `GRuntimeView` names the offsets; the
  consumer `0x1402EBC10` is not ported.
- **Enemy triggers.** Spawn sites of the effect API (`0x1402E7A90 / AB0 /
  CA0 / A80`) counted in `dmc3.exe` by class, code laid out per class
  (`spawnmap.py`, scratch tool): `CEm000` 998 sites in 567 functions, 70 of
  them in the single state function `0x1401C34C8`. The triggers are class-specific
  state logic, as for CEm034; none is traced besides CEm034.
- **Weapons and Dante.** `plwp_sword*.pac` carry an FXBANK, `pl000.pac` none;
  the player classes that spawn from it are not traced.
- **Stage:** `beff` (break effects), `# SET LIGHT`, doors, hit attributes.

## Suggested order

1. P records (unlocks every V composite that has a particle child).
2. G records.
3. One enemy class end to end (CEm000 state function `0x1401C34C8`).
4. Player weapon effects.
