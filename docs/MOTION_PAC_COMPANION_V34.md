# MOT playback, PAC assembly and companion-MOD placement (v34 candidate)

Date: 2026-09-23
Scope: read-only viewer. Nothing here modifies, repacks or writes a DMC file.
Archive editing (NBZ, repacking) stays in GDSpaces.

Canonical executable referenced by the Rengine evidence: `dmc3.exe`,
SHA-256 `e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082`.

## 1. What changed for the user

| Symptom on v33 | v34 candidate |
| --- | --- |
| MOT is staged in the strip but does not play ("playback runtime is not promoted yet") | Tapping a MOT card binds it to the open MOD (or every MOD part with the same skeleton) and plays it in a loop; tapping it again pauses/resumes. |
| Hair / coat / other extra `.mod` land in the wrong place | Companion MODs stay in the character's model space. MOD header `+0x13` is reported, no longer used as a geometry root. |
| `.pac` is not readable | A `.pac` opens as an assembled character/scene (all MODs, paired PTX, MOT library). `⋮ → Browse .PAC files…` lists every slot, and each recognized slot opens in its own viewer (MOD, SCM, PTX, DDS, MOT, nested PAC). |

## 2. Where the reverse lives

The reverse of the original game (EXE addresses, byte receipts, canonical
implementation) is kept in **dmc-rengine-cpp**, branch
`claude/devil-microy3-decompile-port-2v8pne`:

- `docs/research/dmc3-mot-animated-local-and-default-joint-scope-2026-09-23.md`
- `include/dmc_rengine/analysis/mot/animated_local.hpp` (`0x140310310`)
- `include/dmc_rengine/analysis/mot/key_decode.hpp` → `select_cached_segment2`
- `include/dmc_rengine/analysis/mot/track_evaluation.hpp` → `evaluate_compression2_track`
- `tests/mot_animated_local_tests.cpp`

Native Reader holds only the C++23 port and the product code. The vendored
Rengine pin predates those headers, so `motion/animated_local.*` and
`evaluate_compression2_track` in `motion_clip.*` are C++23 ports kept in
lock-step with the Rengine versions until the pin moves.

## 3. Behaviour

### 3.1 Companion MOD placement (hair, coat, accessories)

MOD header `+0x13` only anchors the model's shadow; it never roots geometry
(see the Rengine notes), so it is reported, not applied. Parts start in their
source coordinates.

**Player coat (v35).** Rengine
`docs/research/dmc3-player-coat-attachment-2026-09-23.md`: IPlayer classes load
the coat from PAC slot 12 with the body texture of slot 0, force the coat root
local to identity and every frame install body joint 3's world as the coat
root. `pac_assembly` does the same for archives named `pl*`:

- `motion/part_attachment` hangs the slot-12 part's skeleton from host node 3
  (`CompositePlacementMode::HostJointSkeleton`, identity root local) and
  re-skins it with its own inverse rest matrices;
- `apply_motion_frame` re-poses attached parts after the body moves, so the
  coat follows MOT playback;
- the only assumption left is that the player's joint table index 3 is MOD
  node 3; cloth simulation is not reproduced, so the coat keeps its rest shape.

**Weapons (v36).** Rengine `docs/research/dmc3-player-weapon-attachment-2026-09-23.md`
and `profiles/dmc3/player_attachment_contract.hpp`: a weapon root is
`local(T, R) × player.joint(j)` with the state-0 record of its class
(Rebellion `plwp_sword.pac`: joint 3, T(-14.5, 32, -14), R(-1.658, 0, 3.403);
Yamato `plwp_vergilsword.pac`: joint 13; Agni & Rudra, Nevan, Force Edge,
Nero's sword, Beowulf-less laser…). In the app: open a character PAC, then
`⋮ → Add weapon / .PAC…`. `assemble_archives` re-assembles the character with
every added archive; catalogued weapon PACs hang from the body joint with the
recorded offset and follow MOT playback; other archives keep their source
coordinates. Weapon MODs without a skeleton are moved rigidly onto the root.

**Controls (v36).** Drag now turns the model the way the finger moves
(yaw and pitch signs were inverted), which also removes the "inside-out" depth
impression; the projection itself was already consistent (nearer = larger,
depth test agrees).

**Community-made archives (v37).** Tested on a modded `pl011.pac` (layout
identical to retail: slot 0 PTX, slot 1 body, slot 12 companion, slot 13
cloth text). Its PTX descriptors are written by a community tool: format
word, secondary dimensions and reciprocal floats are zeroed or copied, so the
strict Rengine texture-slot validator reports `descriptor_mismatch`. The
viewer then reads the bundle leniently — header count, per-slot sector spans,
the 0x70 descriptor size and the DDS itself — while still rejecting
structural faults (non-zero sector padding, trailing bytes, bad DDS). The
session reports `descriptors=community-tool(lenient)`.

**Non-canonical marker (v38).** Anything shown through such a path is recorded
in `Session::non_canonical_notes` (texture attach, shared bank, per-part
attach, directly opened PTX). The app shows an orange ▲ in the title bar while
notes exist; tapping it lists them, and the info sheet repeats them under
"▲ NOT CANONICAL (shown anyway)". JNI: `nonCanonicalNotes`. Default camera now
starts in front of the model (DMC3 models face +Z).

**Weapon PNST, effect banks and Euler order (v39).** Weapon archives
(`plwp_*.pac`) are `PNST` containers with the PAC slot layout (slot 0 PTX,
slot 1 weapon MOD, slot 2 nested PNST of effect models, slot 3 SHW); they now
open on their own and as added archives. A PNST nested inside any archive is an
effect bank (Rengine `docs/research/dmc3-em028-nevan-assembly-2026-09-23.md`:
CEm028 hands slot 9 to the effect loader `0x1402C04C0`), so its MODs are
counted as `effectModelsSkipped` and never assembled. The MOD rest local is
`Rx x Ry x Rz` as `0x140330450` builds it (Rengine
`dmc3-euler-order-correction-2026-09-23.md`); the vendored
`world_transform.cpp` still expands `Rz x Ry x Rx`, so Native Reader compiles
`modules/rengine_port/world_transform.cpp` in its place until the pin moves
past Rengine `b403108`. Rebellion now hangs blade-down across Dante's back.

**Enemy node constraints (v39).** `em028.pac` (Nevan): body slot 1, hair
slot 4, bat dress slot 5, bat sleeves slot 6, all with PTX slot 0. CEm028's
init links part nodes to body joints (mode 1, identity offset): hair 0/1/2 to
joints 3/4/5, dress 0/1/2/3 to 1/14/2/3, sleeves 0/1/6/2/7 to 14/7/11/8/12.
`motion::attach_part_nodes` places those nodes at the body joint world and
composes the rest local x parent, also after each MOT frame. The hair strands
and the dress are chains simulated in game (`0x1402C9DC0`); here they keep
their rest shape. Contract: Rengine
`profiles/dmc3/enemy_node_constraint_contract.hpp`.

### 3.2 MOT playback

Per frame: evaluate the nine channels of every joint (compression 3 through
Rengine, compression 2 through the port; other compressions keep the rest
value and are counted as `heldAtRest`), build the animated local matrix,
compose `world = local × parent`, skin with `inverseRest × world`. Non-unit
scale is applied to the basis rows only; the parent-scale compensation of the
game is not reproduced yet.

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
- each MOD takes the nearest PTX before it in the same container (else the
  nearest after it), reported as `ptxPairing=nearest-preceding-in-container`.
  For player PACs this is slot 0 for body and coat, as the game loads them;
  when every part pairs with the same PTX it is attached once as a shared bank;
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

The MOT parser, motion groups, animation binding and PAC parser come from the
pinned Rengine checkout and are rebuilt in `dmc_native_reader_rengine_viewer`
(ReaderCore does not list them yet). Everything Native Reader compiles is
C++23: that slice, ReaderCore, the core, JNI and tests all get
`CXX_STANDARD 23` / `CXX_EXTENSIONS OFF` from Native Reader's CMake. The
submodule itself is untouched.

JNI (thin): `assemblePac`, `assemblePacs`, `motionLibraryCount/Name`, `loadLibraryMotion`,
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
- APK v34 / 1.0.7 built with Gradle 9.5.0, AGP 9.3.0, NDK r30
  `30.0.16248370`, SDK 36 (`:app:assembleDebug`, stable test signer);
  `tools/verify_device_apk.py --signing-policy stable-debug` passes
  (single `libdmcviewer.so`, 16 KiB alignment, JNI export parity,
  `modular_native_architecture: pass`).
- **Not done:** nothing was run on a device (`device_test: pending`).

MOT and PAC are no longer listed as banned legacy modules in CI; the remaining
list covers only families that are still unpromoted (HITS, TXT, DCA, LIG2,
NBZ, EFM, MRP, SHW adapters). PNST is read by `formats.pnst.archive-reader`
(v39).

## 7. Next

1. Device check on real `pl*.pac` / `em*.pac`: skeleton match rate, visual
   pose correctness, frame rate of the software renderer.
2. Parent-scale compensation from `0x14030E9B0`.
3. Model Set pairing (`0x1402D83E0`) instead of slot adjacency.
4. SHW shadow hulls drawn from the file (the engine SHW reader is structural).
5. Cloth (CLT/C1D) and enemy chains (`0x1402C9DC0`) — needs a parser and the
   chain parameters before any physics can be shown.
6. Node constraints of other enemy classes (only CEm028 is tabled).
