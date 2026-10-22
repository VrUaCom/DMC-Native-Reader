# Cutscenes (demo PAC)

The Reader opens a cutscene archive (`demo\...\mNN_xNN.pac`, `CSceneDemo`)
as its scene: the stage, the characters and their weapons play their
motions over the timeline, seen through the cutscene camera. Everything is
read from the bytes; only ids, offsets and layouts are recorded here.

## Archive layout (reversed from the retail cutscenes)

A PAC:

| slot | holds |
| --- | --- |
| 0 | the script: `# Load` and `# Set` lists of `Kind [track] file.txt` lines |
| 1 | the names of the clip texts in slot 2 (CRLF lines, `# End`) |
| 2 | a PAC of clip texts, one per `Set` line that has data |
| 3, 5, 7, ... | a name list: the files of the next slot, in order; a line starting with `;` is commented out and has no member |
| 4, 6, 8, ... | the container (PAC or PNST): models and textures (`.scm .mod .efm .ptx .shw .clt`), motions (`.mot`), hide animations (`.hid`), cameras (`.cam`), effect / sound ids (`G 48`, `E 257`, ...) |

`*_movie.pac` carries only the PadVibe and Message clips of a cutscene that
plays as a movie.

### Clip texts

`# Clip` blocks of `Key<TAB>values` closed by `# End`. Every clip has
`SetFrame` (where it starts on the timeline), `SkipFrame`, `CutFrame`,
`ClipScale` (its time scale) and `Life` (its length at scale 1). A clip
covers `SetFrame .. SetFrame + Life * ClipScale` and plays its own time
`(frame - SetFrame) / ClipScale` (checked: `cm011.cam` at scale 3.3 ends
exactly where the next camera clip starts). Frames are game frames, 60 a
second.

Kinds: `Camera` (`Camera file.cam`), `Model NN` (`Model`, `Texture`,
`Shadow`, `Cloth`, `Pos`, `Rot`, `CnsPos track joint`), `Motion NN`
(`Motion file.mot`), `Hide NN` (`HideKind A|H|D`, `Hide file.hid`),
`Effect`, `EffectI NN`, `Light`, `PadVibe`, `Message` (`No`), `ScrEfc`,
`Program`, `Sound`, `WorkRate`.

### `.cam`

| at | holds |
| --- | --- |
| +0x00 | u32 0x60 (header size) |
| +0x04 | `CAM\0` |
| +0x08 | u32 mask of animated channels |
| +0x10 | f32 frame range start, end, start, end |
| +0x20 | 14 f32 base values: group translate xyz, group rotate xyz, eye xyz, target xyz, field of view (degrees), roll (degrees) |
| +0x60 | u32 track count, then one track per set mask bit, in channel order |

A track: u16 byte size, u16 key count, u8 kind (the bytes after it are
noise). Kind 1 keys are 16 bytes `(u32 frame | 0x8000, value, in slope,
out slope)`, cubic Hermite with slopes per frame; kind 0 keys are 8 bytes
`(u32 frame, value)`, linear. Key frames are the range's frames; when the
range starts below zero (`cm_c010.cam`, -70) they count from its start.
The eye and target are moved by the group translate (rotation: x, y, z
order, zero in every retail camera). The stage configuration's CAM member
is unrelated (its tag is at +0).

### `.hid`

| at | holds |
| --- | --- |
| +0x00 | u32 header size |
| +0x04 | `HID\0` |
| +0x0C | f32 frame range start, end, start, end |
| +0x1C | u8 object count N, then N bytes: 1 when the object has a track |
| header | u32 track count, then one track per flagged object, in object order |

A track: u16 byte size, u16 key count, u32 kind (7), f32 state (-1 shown,
0 hidden), f32 1.0, then 4 bytes a key: a big-endian u16 frame of the range
(it equals the range start in every single-key file) and a u16 flag.
`DispOn.hid` / `DispOff.hid` hold 100 tracks: objects 0..99 shown /
hidden. Several stage files show one object and hide another (a piece
swapped for its broken version). The object index is the model's object
(SCM: object k hangs from scene node k + 1).

## Playback

`demo_session.h` builds the scene (`open_session` of a `DEMO` archive):

* SCM tracks are stage pieces: they go to the room pass in their own
  coordinates (`stage_room::assemble_room` over the opened pieces, each with
  its own PTX, so every piece's vertices and triangles are known).
* Stage pieces play their Motion clips too. An SCM scene node has the MOD
  joint record (translation, rotation xyz), so the MOT binds to the nodes;
  its channels are turned into node matrices the SCM way (its rotation order
  is not the MOD joints': `MotionClip::evaluate_channels`), composed down the
  node tree, and every object moves rigidly with its node (SCM meshes are
  local to their node). A piece whose rebuilt rest pose does not match its
  own stays still. The room's `revision` changes whenever it moves or hides,
  so the GPU rebuilds its cached buffers.
* MOD / EFM tracks are composed (`compose_mod_sessions`, source
  coordinates) and get their own `Texture`.
* Each model track plays its Motion clips: the clip that started last among
  the covering ones, held at its end between clips, at
  `CutFrame + local time`. Body MOTs of the lighter cutscene body variants
  have longer channel domains than the model; they drive its leading joints
  (`load_cutscene_motion`, as 0x140310A80 walks the model's joints).
* `CnsPos track joint`: the model is authored at its host's rest pose and
  follows the joint's move from rest (vertex x inverse rest world x current
  world).
* Visibility, per object of every track (models and stage pieces): every
  Hide clip set up to the frame applies in order (H hides the model, D shows
  it, A applies its `.hid`: its objects take its states). The meshes draw the
  shown objects' triangles only.
* The camera: the covering Camera clip that started last; between clips the
  last one that ended holds its final pose. The view stands at the eye,
  looks at the target, and the field of view becomes the viewer's zoom
  (`tan(0.5) / tan(fov / 2)`; the viewer's focal length spans the shorter
  image side).

The timeline is the session's one motion (`motion_player.h`): its library
entry `Cutscene mNN_xNN` binds it, `apply_motion_frame` seeks it, so the
viewer's play / pause, tap-to-pause and scrub drive the cutscene.

### The camera

While the cuts drive the view, the core puts the shot's camera into every
render (`prepare_view`). The first camera gesture (turn, pinch, two- or
three-finger gestures, or ✈) takes it (`demoTakeCamera`): the free camera
stands at the shot's eye and continues from its direction and field of
view, so the user can look around and zoom in a pause or while it plays,
and fly through the scene. `⋮ → 🎬 Cutscene camera` gives the view back to
the cuts.

### The camera inside the scene

Shots stand over a shoulder or inside a model (`cm036`: the eye in Dante's
head). The model pass clips its triangles at the near plane in camera space
(the room pass and the GPU pass already did), so geometry at and behind the
eye is cut, never projected across the picture.

## Not yet

The per-key flags of `.hid` tracks (a state change inside a clip), effects,
lights, screen effects, messages, sound. A cutscene that
does not carry its stage (`m01_c00` comments out `st000.scm`: the game uses
the shop it is in) shows it when that stage is picked as the room.

Tests: `demo_test.cpp` (synthetic bytes).
