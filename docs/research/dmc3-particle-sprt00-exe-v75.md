# P records, class 3 ("Sprt00"): CPtclSprt00 (v75)

Scope: the P records whose definition class byte (`def + 0x01`) is 3. The
other classes (0 Line00, 1 Poly00, 2 Poly01, 4 Line01, 5 Line02) are still not
drawn. Corpus seen so far: 13 class-3 records (em034 and `st002_effect.pac`),
all with `def + 0x128 == 1` and `def + 0x110 == 1`, none with `def + 0x50 == 1`.

## How this was checked

`dmc3.exe` functions were run in a Unicorn emulator on fabricated objects (the
scratch tool is not part of the repository). Positions, velocities, colours,
transforms and the draw matrices of a synthetic record (no game data) were
captured frame by frame and are embedded in
`app/src/test/native/particle_sprt_truth.inc`; `particle_sprt_test` replays the
C++ port against them (14 updates, both gravity modes, 12 particles, emitter plus
two layers, final world-space quad corners). The test fails if any constant
below is changed.

## Record layout (class 3)

`record + 0x00` u32 version 2; the body starts at `+0x10`. `body[0]` is the
definition offset (0x10), `body[4]` the pointer-array slot, `body[8 + 4i]` the
body offset of layer i (`0x140312DC0` relocates them). Definition `D = record + 0x20`
(0x130 bytes); a layer is 0xB0 bytes.

| Offset (D) | Meaning |
| --- | --- |
| +0x00 / +0x01 | 2 / class (3) |
| +0x02 | name ("ec021-40p1"); `es400-00p0` takes the camera-local path (`obj+0xF5 = 1`) |
| +0x20 / +0x30 / +0x40 | emitter translation / rotation (rad, Rz*Ry*Rx) / scale, 4 floats each |
| +0x52 / +0x58 / +0x5E | per-tick s16 x3: translation x 1/16, rotation x 2pi/65536, scale x 1/4096 (`0x140312260`) |
| +0x68 | life, ticks (s32) |
| +0x6C | colour track: 4 keys x 20 bytes `{s16 length, s16, RGBA x 4}`, enable `+0xBC`, loop `+0xBD` |
| +0xC0 | layer count (u16) |
| +0xE2 | s16 x3 / 16: outward push; +0xEE s16 x3 / 16: bias |
| +0xF4 / +0x11C | floats x3: friction per tick / its per-tick decay |
| +0x100 | s16 x3 / 16: spread (box edge); +0x116 s16 x3 / 32: hollow half extent |
| +0x106 | A (sprite animation) record id; the texture is that A's texture |
| +0x108 / +0x10A | u16 / 16: quad half width / height |
| +0x10C | float: gravity per tick (y) |
| +0x110 | 1: gravity vector given in world space |
| +0x128 | 1: one random A frame for the whole effect, 0: A plays (`0x1403228F0`) |

Layer (L): +0x00 / +0x10 / +0x20 translation / rotation / scale, +0x30 / +0x36 /
+0x3C the same s16 motions, +0x48 colour track (enable +0x98, loop +0x99). A
layer re-draws the emitter's particles with its own local transform and colour;
atlas and quad size come from the emitter (the layer init reads the *parent's*
`+0x106`).

## Runtime (all verified by the emulator replay)

* Burst of N = 12 particles (object `+0xF4`, set by `0x140312B20(2, 12)`).
* Init `0x140236D10`: three uniform draws r in [-0.5, 0.5); position = r x spread;
  a point inside the hollow box (`|p| <= hollow` on every axis) is pushed out on
  one axis (`0x140312450`: random start of 6 faces, first face whose hollow edge
  is below spread/2); velocity = p x (push / max(spread, 1)) + bias - p.
* Update `0x1402374F0` (dt = 1, one tick): life -= 1 (endless below -10000), dies
  when negative; colour track; translation / rotation (wrapped to +-pi) / scale
  integration; per particle: friction `0x140314080` (towards zero, never past it),
  velocity += (0, g, 0) (through the inverse world rotation when `+0x110 == 1`),
  position += velocity. Friction decays after its value is read.
* Colour track `0x1402D30E0`: segment i lerps key i to key (i+1)&3 with
  p = 1 - remaining/length, bytes truncated; only the first RGBA group is used and
  copied to all four corners; after segment 2 the track stops (or loops).
* Draw `0x140312F10`: local = (scale rows x Rz*Ry*Rx) with translation row; the
  translation helper `0x140031200` adds the vector's w (1.0) to the row's w, so
  the row-3 w is **2.0**: through the world matrix the translation part is
  doubled and the projective divide halves everything but the world translation
  (positions, sizes, local translation: x0.5). The port divides by w exactly.
* Vertices (non-camera-local path): corner x camera^-1 x world^-1(rotation) x
  emitter-rotation^-1, plus the particle position. The emitter rotation and the
  world rotation therefore cancel for the emitter itself (a camera-facing quad);
  a layer with another rotation tilts and spins its copy of the quads.
  Vertex order BR, BL, TL, TR with w = 1, 0, 2, 3; the UV table at `obj+0x164`
  is (U0,V0) (U1,V0) (U0,V1) (U1,V1) for w = 0..3, so the image is upright with
  the corner y axis pointing to the image bottom. UV = cell x/W .. (x+w-1)/W.
* Vertex colour = track RGBA / 255, modulating the texture (mode 0 of slot 13).

## What the Reader does

`particle::Simulation` (`modules/particle_sprt.cpp`) replays the burst from tick 0
to the entry's age every frame (the state is a pure function of age and record).
`append_effect_particles` (resource_session.cpp) turns the quads into
`EffectSprite`s (oriented corners, tint = vertex colour) with the A frame as UV.

## Approximations (documented, not guessed silently)

* The random draws come from a fixed per-record seed: the retail generator
  (`0x140059390`, four-word LCG mix) is shared and unseeded by the effect.
* Blend: the draw selects blend row 2 of the table at `0x1405D09E0` (a packed
  GS-style state word whose consumer is not traced). Normal alpha blending is
  used; the dark smoke and fire puffs of `st002_effect.pac` / em034 read correctly
  with it (an additive reading would erase the 0x30-tinted smoke).
* The camera inverse is the Reader's mirrored camera basis; the world inverse
  uses the world rotation with the rows normalised (the EXE assumes an
  orthonormal matrix).
* `es400-00p0` (camera-local path) is drawn through the normal path.
