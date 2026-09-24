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

**Agni & Rudra (v40).** `plwp_2sword.pac` is one MOD: Agni on node 2, Rudra
on node 1, both at the same rest place, so attaching the root hid Rudra inside
Agni. Attach records are two-part (`0x1401FDA80`); CPlWp2Sword's pose
`0x140227CF0` drives node 2 with part 0 and node 1 with part 1 of the state-0
record, which the viewer now reproduces (node constraints with an offset).

**SHW shadows (v40).** Rengine `docs/research/dmc3-shw-shadow-projection-2026-09-23.md`.
A `.shw` opens on its own (`formats.shw.hull-reader`): every closed hull is
drawn as a mesh and the info lists hulls, vertices, triangles, closure and the
joints they follow. In an assembled PAC each SHW binds to the MOD whose node
count equals header `+0x11` (pl000: slot 8 -> body, slot 14 -> coat); its
vertices follow the selected joint's skin matrix, so the shadow moves with MOT
playback and attached parts. The ◐ button (on by default) draws a floor under
the lowest rest vertex and the hulls' footprint on it along the light
direction; the game takes the light from the stage (`[shw+0x60]`), so the
viewer uses a fixed direction (`shadow::kViewerLightDirection`).

**Weapon motion banks (v41).** Dante's weapon motions live in
`motion\pl000\pl000_00_N.pac`; the loader `0x1401DF6BE` picks N from
`0x14058ABC8[weaponId * 4]` and the factory `0x1401DED20` ties ids to classes
(Rebellion 3, Cerberus 4, Agni & Rudra 5, Nevan 6, Beowulf 7, Ebony & Ivory 8,
Shotgun 9, Artemis 10, Spiral 11, Kalina Ann 12, ...). Adding such a file with
`⋮ → Add weapon / .PAC…` puts its motions in the strip as
"<weapon> · slot_NNNN.mot". `pl000_00_0/1.pac` are the same banks as slots 2/3
inside `pl000.pac`. The weapon itself stays in its sheathed record during these
motions; hand-held records are not reversed yet.

**Shared enemy archives (v42).** Rengine
`docs/research/dmc3-em000-family-assembly-2026-09-23.md`: `em000.pac` feeds
five classes (CEm000-CEm004). Each class's init reads its own body slot (1, 5,
8, 13, 18), cloth models with their `.clt` text (3; 7; 10 and 12; 15 and 17)
and weapon (26, 28, 26, 27, 34). The archive now opens as one class at a time,
with every other class's models skipped: cloth roots hang
from body joints 14 / 8 / 12 as `[this+0x3254]`/`[this+0x3258]` say, the
weapon from body joint 9 with the recorded offset (T(-15, -61.4, -18.9),
R(0.207, 0, 0); CEm004 its own), built in 0x1403304A0's Rz·Ry·Rx order.
Models no class loads at spawn (slots 4, 19, 21, 33: likely death / sand) are
not shown. Cloth is simulated since v44 (below).

**Position buttons (v43).** Archives with several in-game looks show one
button per position under the title; tapping one re-assembles that look
(JNI `archiveVariantNames`, `assemblePacs(..., variant)`):

- `em000.pac`: CEm000 A/B, CEm001 A/B, CEm002 A/B, CEm003 A/B, CEm004 — the
  class plus its weapon for `[this+0x670]` 0-1 (A) or 2-3 (B); CEm000 B has no
  cloth (CEm000 draws it for 0-1 only).
- `em028.pac`: "Bats in" (default) and "Bats out". MOD object bit 0 means
  "draw" (loops `0x140303460`/`0x140303DE0`); Nevan sets it on the dress strip
  (slot 5 objects 2-3) only while bats are out, so "Bats in" hides those
  triangles.

**Chain/cloth simulation (v44).** `.clt` text slots are parsed
(`motion/cloth_chain.cpp`; parser `0x1402CA345`/`0x1402CA42A`, defaults
`0x1402CA000`) and every listed bone runs the per-node step `0x1402C9450`:
the axis turns toward the parent, the result is blended with the rest pose by
Stiffness, wind and Gravity change the velocity, LimitLength keeps the bone
length with a SpringForce pull-back, speed is clamped to MaxSpeed and damped by
0.99, and the floor clamp is applied. Bindings: player coat slot 12 <- slot 13,
em028 hair 4 <- 7 and dress 5 <- 8, em000 cloth slot N <- N-1. The solver runs
once per elapsed motion frame (dt 1, at most 6 per update); a new motion
restarts the chains from its first frame and settles them for 30 frames, and a
fresh assembly settles them for 60. Since v46 the player coat collides with
the body the way `0x1402CA2F0` sets it up: six capsules on body joints 3
(chest), 2, 15, 16, 19 and 20 (legs) from `.rdata 0x14058B260`. A node inside
a capsule is pushed onto its surface (`0x1402D0630`) and loses its x/z
velocity. The enemy capsule tables and the `+0x48` object list are not ported. Evidence: rengine
`docs/research/dmc3-cloth-chain-solver-2026-09-23.md`.

**Texture scroll (v45).** `.tsc` texts are parsed like CDrawUV does
(`motion/uv_scroll.cpp`; parser `0x14030A9B0`/`0x14030ABE0`: `.TSC`, only the
`# RELATIVE` part, `$` ends the text). MOD objects whose source flags `+0x10`
carry `ScrlNo + 1` in bits 24-27 (and whose mesh texture equals `TexNo`, if
set) get their UVs offset by the record's value in 1/4096 texture:
types 0/1 linear, 2/3 cosine-eased with MinimumUV drift, types 4/5/10 and
RndUV not ported. The offset runs on a game-frame clock kept by motion
playback. Binding: em028 slots 5 and 6 <- slot 13 (dress lightning strip
scroll 0 flows up, bat fabric scroll 1 crawls sideways). em000's TSC (slot
24) belongs to the EFM model of `CEm005Shl01` and is not shown yet.
Standalone `.tsc` and `.clt` files open in `formats.tsc.scroll-reader` and
`formats.clt.cloth-reader` (inspection: records / cloth blocks); inside a PAC
their slots are now named `slot_NNNN.tsc` / `.clt`. Evidence: rengine
`docs/research/dmc3-tsc-uv-scroll-2026-09-23.md`.

**EFM effect models (v47).** EFM files use the MOD document layout (post-load
`0x1402F7A90` relocates the same header/object/mesh fields as MOD plus mesh
`+0x38`, the COLOR0 stream). They open in `formats.efm.model-reader`, which
routes through the MOD adapter on a copy whose magic reads `MOD `
(`mod_bytes.h`); vertex colours are listed but not yet applied. em000.pac gets
a tenth position, `CEm005Shl01`: the projectile model in slot 23 with PTX 32
(init `0x1400AD620`) and its texture scroll from slot 24. Its tail chain (slot
22, `em005_02.clt`) needs flight to move and is not attached. Effect-bank EFMs
(em028 slot 9) stay out of assemblies but open standalone. Evidence: rengine
`docs/research/dmc3-efm-effect-model-2026-09-24.md`.

**Blend, colour, more scrolls (v48).**
- Every MOD/EFM vertex carries its object's blend mode (source flags `& 0xF`,
  GS ALPHA table `0x1405D0550`): 1/4 alpha, 2 additive, 3 subtractive. The
  renderer adds or subtracts the texel weighted by its alpha without writing
  depth.
- EFM COLOR0 modulates the texel as on PS2 (`texel · colour / 0x80`).
- TSC scrolls now run a per-frame simulator identical to `0x14030C1C0`, adding
  types 4/5 (DirUV ping-pong every TurnTimeUV steps) and type 10 (view-facing;
  the viewer uses a fixed front view).
- Nevan's hair collides with three capsules on body joints 5/4/3
  (`0x140130D9A`). The dress and em000 cloaks have no collision in the game.

**Weapon in hand (v49).** pl000.pac slot 5 is the player motion script
(`motion/motion_script.cpp`): 34 banks (= `pl000_00_N.pac`; pl000.pac slots
2/3/4 are banks 0/1/2), one byte-code script per MOT. Opcode 3 byte 2 (low 6
bits) is the weapon attach state that `0x1401F01F0` copies to
`player+0x39C3`; opcode 0 waits for a frame. Every motion library entry knows
its bank and MOT index, and while it plays the viewer re-points each attached
weapon at the class's record for the current state (tables from the EXE, 24
states per class: Rebellion 0/1 back, 2 right hand joint 9, 3 left hand joint
13). States 0, empty records and special pose branches (Nevan's play poses)
keep the previous record; stopping the motion returns to state 0. Evidence:
rengine `docs/research/dmc3-player-motion-script-2026-09-24.md`.

**Lone MOT view (v50).** A MOT opened on its own has no mesh, hierarchy or
rest pose (those live in the MOD), so instead of an empty 3D view it shows an
image of its curves (`motion/motion_chart.cpp`): rotation, translation and
scale panels with every animated channel from frame 0 to the end frame,
evaluated with the playback evaluators (compression 3/2), one colour per node
and lighter shades for y/z, value ranges and a frame axis. Adding a MOD with
the same node count still plays it in 3D.

**Stand-alone views (v51).** Every file now opens with a picture of what it
holds, even when it has no mesh or pixels of its own (`format_views.cpp`,
`raster_card.cpp`: a small software canvas with a 5x7 font):

| File | View |
| --- | --- |
| MOD / SCM / EFM / SHW | 3D (unchanged) |
| DDS / PTX | image; a PTX saved under another name (e.g. a PAC slot as `.bin`) is now recognised by its bytes |
| MOT | channel curves (v50) |
| PAC / PNST | child list (unchanged) |
| TSC | per scroll record: U/V offset over 240 game frames from the `0x14030C1C0` simulator, plus a checker tile scrolled to frames 0/30/60/90 |
| CLT | per block: the strands (a bone whose node is not the previous + 1 starts a strand; its parent is the fixed root), axis letters, gravity/wind arrows, solver parameters |
| Motion script | pl000.pac slot 5 is now its own format (`MotionScript`, module `formats.motion-script.reader`), identified by structure (header table, 0xFFFF-terminated bank list, each bank's first script starts with opcode 1): per bank the script count, waits, last frame, loops and weapon states; the opcode histogram; the inspection lists every script (MOT bank/index, ops, waits, states with frames) |
| Other accepted files (EventTbl, ...) | information card: title, detail, properties, inspection tree and a hex/ASCII dump |
| Unknown or rejected bytes | raw binary view instead of an error: entropy per block, byte histogram, printable strings, FourCC, first u32 words, ascending offset-table hint, float32 share, record-stride guess, the rejection reason and a hex dump |

The pipeline contract is unchanged: rejected files still leave
`run_decode_pipeline` unaccepted; the binary view is made by `open_session`
and claims no format ("BIN").

**Enemy scripts and collision tables (v52).** The motion script format is
shared by the player and enemies (bind `0x1400594B0`: mode 0 player, mode 1
enemies). A third header table (B, resolver `0x14005A360`) maps each script
action to the MOT that it plays (`id = group·100 + slot`, loop flag), so:

- the script view shows each enemy's action grid (action → MOT id, L = loop,
  \* = weapon state);
- in an assembled enemy PAC every MOT is labelled with the actions that play
  it (e.g. `act 12,15,34 · slot_0002.pac/slot_0012.mot`). Id groups are bound
  to the class's motion PACs: em028 {2, 3} and em000 {35} from the EXE, the
  others by data (the first unused MOT PAC that holds every slot), otherwise
  left unbound;
- the player's weapon states now go through the same table (a MOT
  `pl000_00_N` slot *k* takes the action of bank *N* that plays `N·100+k`).

The pair of slots next to the script is the collision handle
(`ICollisionHandle`, `0x14005C260`): an attack index (4-byte entries: target
mask, bone, shape; the `.colidx` family, recognised as the slot before a shape
table) and 80-byte shape records (2 sphere, 3 box with Euler degrees, 4
capsule; `COLSHAPE`, recognised by content). Both have their own views:
- shapes: front and side projections in bone space;
- index: every attack id.

Player slots 9 and 11 are float parameter blocks; the binary view shows them
as a value grid by offset. Evidence: rengine
`dmc3-enemy-motion-script-2026-09-24.md` and
`dmc3-collision-tables-2026-09-24.md`.

**Hitboxes (v53).** The game's collision debug meshes are system resources
`obj\\debug\\at000–at003.mod` (list `0x1405B0860`). The viewer does not ship
them; `collision_debug.cpp` generates the same shapes with the measured
dimensions:

| Mesh | Shape |
| --- | --- |
| at000 | unit sphere |
| at001 | cube ±1, the same corners as the game's box table `0x1405CEC60` |
| at002 | capsule, radius 1, segment ±0.5 |
| at003 | octagonal prism ±1 |

An assembled character PAC binds its collision handle:
- the shape table (`COLSHAPE`) of the top-level archive, with the index in
  the slot before it;
- the bones of the body (the first top-level MOD): pl000 slots 6+7 on slot 1
  (214 attacks), em000 slots 39+40 (20 attacks), em028 slots 11+12 (2
  attacks).

The ◎ button draws the shapes on the current pose:
- each sphere scaled by its radius;
- each box by its half size and Euler degrees;
- each capsule rebuilt from a, b and its radius.

Tapping ◎ cycles: all attacks → each used attack id (the toast names bone,
shape and mask) → off. A long press turns the overlay off. Attacks are started
by game code (`0x14005C740` with constant ids), so the viewer lets you choose
the id instead of following a timeline.

**Neutral texture (v54).** Geometry without a texture of its own gets the
same texture the game gives its debug meshes. Examples: a lone MOD, SCM or EFM,
or an assembled part without its PTX. In the game that texture is
`obj\\debug\\at.ptx`: 128×64, flat 0x80, the PS2 ×1 modulation. The file is
not shipped; `neutral_texture.cpp` generates it (exact 0x80, alpha 0xFF).

The renderer draws such triangles as texel × COLOR0 (when present) × a
two-sided camera light on the face normal. The form stays readable, and EFM
vertex colours come through as on PS2. The depth-shaded grey remains only
for callers that pass no fallback.

**Smooth lighting (v55).** MOD and SCM normals are kept as `Mesh::normal0`.
Each frame the renderer rebuilds vertex normals from the current posed
positions. It welds coincident vertices whose file normals agree, so strip
seams become smooth while authored hard edges (the cube `at001`) stay hard.
Lighting is Gouraud:
- the neutral texture gets full lighting;
- real textures get mild lighting;
- COLOR0-prelit and additive or subtractive triangles are not lit.

**Effect banks (v55).** Enemy and weapon effect banks have their own family,
`FXBANK`:
- em028 slot 9;
- em000, em006 and em007 slot 41;
- plwp_* slot 2.

The EXE loader `0x1402C04C0` reads slot 0 (the manifest) with the `.tsc`
tokenizer. For each `<kind> <id>` it hands the next record of slot 1 to the
kind's registrar (A C E G M P T V). An `M` takes two slots, the model and a
16-byte companion. A `#` token ends the manifest.

The viewer lists every named record as a child file:
- `T072.dds`: the 112-byte descriptor is dropped and the DDS decoded, with
  thumbnails;
- `M130.efm` / `M009.mod`: opens in 3D;
- `A009.fxa`: a sprite animation drawn over its texture from the same bank,
  with numbered frame rectangles and the frames in a row;
- E / P / G / V / C: the raw binary view.

Evidence: rengine `dmc3-effect-bank-loader-2026-09-24.md`.

**Tool bar and motion strip (v56).** Tools that do not apply to the open
resource are hidden instead of greyed out. The bottom tool bar sits in a
horizontal scroll view: centred while it fits, scrollable when it does not.
The motion strip only lists MOTs that can drive at least one skinned part of
the assembled model (`motion::motion_can_drive`, the same binding that
playback uses; host-joint parts such as coats do not count). Examples:
- em000 variants 4/5: the 22-node body in slot 8 keeps 77 of the 82 MOTs.
- The 23-node bodies of variants 0–3 bind none of slot 35's 22-node MOTs
  (`mot-channel-domain-differs-from-model-nodes`). That binding remains an
  open research item.

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

JNI (thin): `assemblePac`, `assemblePacs`, `hasShadows`, `motionLibraryCount/Name`, `loadLibraryMotion`,
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
NBZ, MRP adapters). PNST is read by `formats.pnst.archive-reader`
(v39), SHW by `formats.shw.hull-reader` (v40), TSC and CLT by
`formats.tsc.scroll-reader` / `formats.clt.cloth-reader` (v45), EFM by
`formats.efm.model-reader` (v47).

## 7. Next

1. Device check on real `pl*.pac` / `em*.pac`: skeleton match rate, visual
   pose correctness, frame rate of the software renderer.
2. Parent-scale compensation from `0x14030E9B0`.
3. Model Set pairing (`0x1402D83E0`) instead of slot adjacency.
4. Stage light for SHW (`[shw+0x60]`) and the culling tests `0x140320950`/`0x1403206F0`.
5. Enemy chain capsules (other `0x1402CA2F0` callers), `c+0x48` objects,
   WindType, and C1D files.
6. Node constraints of other enemy classes (only CEm028 is tabled).
