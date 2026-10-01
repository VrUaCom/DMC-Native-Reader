# Effect coverage: what is drawn, what is missing (v74)

## Drawn today

| Source | What plays |
| --- | --- |
| CEm034 (Lady) | Shl00/02/04/05 shells and their V/E effects, from EXE-traced triggers |
| Stage layout | `eff V id` + `epos` on objects (st002 drums), from `st*_effect.pac`; texture scroll `uv`; additive light geometry |
| Any FXBANK child | E sprites (modes 1 and 2) and every P record of the corpus (classes 3, 1, 4, see `dmc3-particle-sprt00-exe-v75.md`) in the gallery preview |

## Not drawn

- **P records of classes 0, 2, 5** (Line00, Poly01, Line02: code exists, no corpus record; classes 1, 3, 4 are drawn). Sizes 336 / 528 / 704 bytes. Observed layout:
  - `+0x00` type (2), `+0x10 / +0x14 / +0x18` element counts (0x10, 0x140 / 0x20, 0x150);
  - `+0x28..+0x33` a short name (`40p0`..`40p4`), then keyed tracks from `+0x84`
    (entries of `u16 flags, u32 frames, float value`);
  - `+0x16C..` scale triples (1.0, 1.2, 1.5, 1.6, 0.5), angles in radians.
  The consumer is the registrar `0x140314B80` and `CParticle`; not traced yet.
  Every V that lists `P` children (for example V543, the rocket explosion)
  is missing its particle part.
- ~~**G records**~~ (generators, 96 bytes): ported in v76, see
  `dmc3-generator-exe-v76.md`.
- **Enemy triggers.** Mapped in `dmc3-effect-triggers-recon-v77.md`. (The
  earlier count "CEm000: 998 sites, 70 in `0x1401C34C8`" was wrong: the class
  spans started at a shared base function; `0x1401C34C8` is part of the
  generic enemy event handler `0x1401C3130`.) em000..em008 send event codes
  from their AI commands and death code; Nevan spawns from a 39-state update.
- **Weapons and Dante.** Constant spawns in CPlDante, the player projectiles
  and CPlWp2Sword; the other plwp ids likely come from `CEfcPub` id
  arithmetic (recon v77).
- **Stage:** `beff` / `bmodel`, `# SET LIGHT` and `# DOOR` decoded (recon v77), not used yet.

## Suggested order

1. ~~P records~~ (done for every corpus record, v75).
2. ~~G records~~ (done, v76).
3. em000 family attack and death effects (recon v77, sections 3-4).
4. Stage `beff` / `bmodel` toggle and the `SET LIGHT` light.
5. Nevan states; CEfcPub and the weapon trails.
