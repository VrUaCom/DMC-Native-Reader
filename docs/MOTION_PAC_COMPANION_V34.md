# MOT playback, PAC assembly and companion-MOD placement (v34 candidate)

Date: 2026-09-23
Scope: read-only viewer. Nothing here modifies, repacks or writes a DMC file.
Archive editing (NBZ, repacking) stays in GDSpaces.

Canonical executable used for every address below: `dmc3.exe`,
SHA-256 `e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082`.

## 1. What changed for the user

| Symptom on v33 | v34 candidate |
| --- | --- |
| MOT is staged in the strip but does not play ("playback runtime is not promoted yet") | Tapping a MOT card binds it to the open MOD (or every MOD part with the same skeleton) and plays it in a loop; tapping it again pauses/resumes. |
| Hair / coat / other extra `.mod` land in the wrong place | Companion MODs stay in the character's model space. MOD header `+0x13` is reported, no longer used as a geometry root. |
| `.pac` is not readable | A `.pac` opens as an assembled character/scene (all MODs, paired PTX, MOT library). `⋮ → Browse .PAC files…` lists every slot, and each recognized slot opens in its own viewer (MOD, SCM, PTX, DDS, MOT, nested PAC). |

## 2. Companion MOD placement (hair, coat, accessories)

v33 took the child's MOD header byte `+0x13` ("default joint index"), indexed
the **host's** joint array with it and multiplied the entire child mesh by that
joint's full world matrix. The executable does not do that:

- `0x1402FD040` reads `manager+0xFA` (the byte from `+0x13`) and takes only the
  **translation row** (`+0x30`) of `currentWorld[index]`;
- `0x14031FA80` does the same: when a linked manager exists (`manager+0x198`,
  getter `0x1403025E0`, setter `0x140302610`) it indexes the linked manager's
  `currentWorld` with the model's own `+0xFA`, reads row `+0x30`, and stores a
  position into `+0x50`. It is a position probe, not a root transform;
- actor-to-actor geometric attachment (`0x1402DCBAC → 0x140302610`) takes its
  joint number from actor state (`actor+0x1648`), not from the MOD header.

So nothing in the EXE uses `+0x13` to place geometry. Companion MODs are
authored in the character's model space, and the product default is now
`CompositeBuilder: placement=source-coordinates`. The selector is still
reported (`defaultJointSelectors=[...] selectorRole=translation-probe-only`).
The old behaviour stays available only as an explicit opt-in
(`BuildOptions::resolve_default_joint_attachments = true`).

## 3. MOT playback

### 3.1 Animated local matrix (the last open link, now closed)

`0x140310310` runs per motion group. For every joint whose `CMotionJoint+0xF8`
matches the group:

1. each of the nine channels at `joint+0x120 .. +0x220` (stride `0x20`) is
   evaluated by `0x1402E9170` when it has a track (`+0x08`), otherwise
   `current = default` (`+0x04`);
2. `joint+0x108` (animated local) is reset to identity (`.rdata 0x14035D580..`);
3. each rotation channel is quantized through a 16-bit angle:
   `cvttss2si(r * 10430.377)` (`0x4622F982`), low word taken as `int16`, then
   `* 9.58738e-5` (`0x38C90FDC`) — i.e. wrapped modulo 2π;
4. the XYZ Euler basis is built by `0x140330450` — the same helper the MOD
   rest pose uses, so the reader reuses Rengine's `build_local_matrix`;
5. translation channels are written to row 3 (`+0x30/+0x34/+0x38`), `W = 1`.

Scale is a separate pass (`0x14030E9B0`) that only runs when a factor leaves
`(0.99999, 1.00001)`. The reader applies non-unit scale to the basis rows
(`0x14032ED30` convention); the parent-scale compensation inside
`0x14030E9B0` is **not** reproduced yet. Rest defaults come from
`0x14030F800`: T and R from the MOD record, scale `1.0`.

World and skin are the already-canonical chain:
`world[root] = local * rootBase`, `world[child] = local * world[parent]`,
`skin = inverseRest * world` (Rengine `animation_binding`, `world_transform`).

### 3.2 Track evaluation

`0x1402E9170` switches on the track compression (jump table at `0x1402E962C`):

| compression | handler | reader |
| --- | --- | --- |
| 3 | `0x1402E8C80` search + Hermite/linear segment | Rengine `evaluate_compression3_track` |
| 2 | `0x1402E8FB0` search + linear | `evaluate_compression2_track` (new) |
| 0, 1, 6, 7 | other forms | channel held at its rest value, counted as `heldAtRest` |
| 4, 5 | writes 0 | not bound |

`0x1402E8FB0` is instruction-for-instruction the compression-3 search with a
4-byte key stride instead of 8; value = `u16 * q1 / 65535 + q0`, strictly
linear between keys.

### 3.3 Timeline

Frames are MOT timeline units, played at 60 per second. Header `+0x0C` is the
end frame (corpus: mirrors `+0x14`). Header `+0x10` is used as loop start when
it is non-zero and below the end frame (community reading, not promoted).

### 3.4 Which parts move

Every model part whose skeleton size equals the MOT channel domain is animated.
Other parts keep their source pose and are listed as `staticParts` with the
reason. Cloth/hair simulation (CLT/C1D) is not reversed yet, so simulated
coat/hair bones follow only if the MOT carries tracks for them.

## 4. PAC

`PAC\0` relative-slot container, parsed by Rengine `PacParser` (bounded).
Each populated slot becomes a typed child: MOD, SCM, DDS, PTX (accepted only if
the canonical texture-slot framing parses), MOT, EventTbl, nested PAC, SHW
(counted), otherwise `.bin`. Payload bytes are copied; the archive is never
modified.

Assembly (`pac_assembly::assemble_pac`):

- every MOD, including MODs inside nested PACs (depth ≤ 3), becomes one
  composite part in model space;
- a PTX is paired with the MOD that precedes it in slot order. This is a
  **slot-adjacency policy**, reported as `ptxPairing=slot-adjacency`; the Model
  Set pairing (`0x1402D83E0`) is not wired yet;
- MOTs become the session's motion library (motion strip cards);
- SHW records are counted but not drawn yet.

## 5. Module layout (C++23, `DMCNativeReader::Core`)

```
include/dmcresource/motion/skeleton_rig.h     rig retained from the MOD parse
include/dmcresource/motion/animated_local.h   0x140310310 local matrix
include/dmcresource/motion/motion_clip.h      MOT bind + comp-2/3 evaluation (std::expected)
include/dmcresource/motion/motion_player.h    Session pose / clear / framing
include/dmcresource/archive_entry.h           payload classification
include/dmcresource/pac_assembly.h            read-only assembly
modules/module_archive.cpp                    PAC + MOT registry modules
```

The MOT parser, motion groups, animation binding and PAC parser are compiled
from the pinned Rengine checkout into `dmc_native_reader_rengine_viewer`
(ReaderCore does not list them yet); the submodule itself is untouched.

JNI (thin): `assemblePac`, `motionLibraryCount/Name`, `loadLibraryMotion`,
`loadMotion`, `hasMotion`, `motionEndFrame`, `motionLoopStartFrame`,
`setMotionFrame`, `clearMotion`. Pose and draw both run on the UI thread.

## 6. Verification in this change

- Host CTest (g++ 13, C++23): 25/25, including the new `motion_playback`
  (angle wrap, animated local, comp-2 interpolation, world/skin, cache seeks,
  exact rest restore, domain mismatch rejection) and `pac_assembly` (slot
  typing, per-slot opening, single and nested assembly, one MOT driving two
  parts). A mutation of the comp-2 interpolation fails `motion_playback`.
- `tools/test_verify_device_apk.py`: 19/19.
- `app_native.cpp` syntax-checked with g++ `-std=c++23` against JDK `jni.h`.
- **Not done here:** no Android SDK/NDK in this environment, so the APK and
  the Java shell were not built, and nothing was run on a device.

## 7. Next

1. Device check on real `pl*.pac` / `em*.pac`: skeleton match rate, visual
   pose correctness, frame rate of the software renderer.
2. Parent-scale compensation from `0x14030E9B0`.
3. Model Set pairing (`0x1402D83E0`) instead of slot adjacency.
4. SHW shadow hulls drawn from the file (the engine SHW reader is structural).
5. Cloth (CLT/C1D) — needs a parser before any physics can be shown.
