# Camera modes and gestures

## Orbit camera (default)

The camera turns round the framed model / stage. Gestures (each can be
switched off in the Gestures window):

| Gesture | Effect |
| --- | --- |
| One finger drag | turn round (yaw / pitch); flick keeps turning |
| Pinch | zoom (zoom and 35 mm lens shown) |
| Two fingers drag | pan the camera plane |
| Two fingers twist | turn the model in the room (the view without a room) |
| Hold one finger, slide another up / down on the other half | dolly along the view axis (distance shown) |
| **Two fingers held together (< 24 mm apart), moved as one** | **move the camera up / down, left / right; no zoom.** Spreading them is still a pinch: the pair is a move only while its span changes less than 0.6 x the distance it travels. |
| **Hold one finger, twist two others round it** | **turn the camera round the model / the stage centre (yaw); the room stays put.** The ring under the holding finger shows the angle ("CAMERA TURN"). A three-finger swipe or tap is not triggered after a turn. |

### Where a stage starts

A stage (`stNNN.pac`) opens with the camera inside it (Spider
`stage_start_view`, JNI `stageStartView` -> {yaw, pitch, zoom}; the view
reset and double tap ask the core again):

- **With the stage's configuration loaded** (`stNNNcfg.pac` from the ⋮
  menu, same stage number): the game's own camera (CAM) for spawn point
  POS 0, its field of view as the zoom (zoom 1 is a 0.5 rad half field of
  view). Loading the configuration resets the view there
  (`ShellStep::ResetView`); a spawn point's card (hold its marker) has
  **Game camera**, which starts the view at that spawn point instead.
- A spawn point no CAM zone holds is seen from behind along its heading
  (`stage_room::start_view`, below).
- **Without a configuration**: the middle of all the stage's HITS
  collision (every record of every source; st006: 475 -800 0 .. 4542 1700
  5500, middle 2508 450 2750), stood on its floor: of the floor records
  (normal.y >= 0.7) under that middle, the one whose height is nearest the
  floors' area-weighted mean height, so a ledge or pit under the middle is
  not taken (st006: y 0). No floor under it: the nearest floor record. The
  layout and the room mesh are not used; they reach far outside the arena
  (st006's terrain goes down to y -5980). The camera stands behind, 520
  units back and a third of that above, and turns round in 30° steps (then
  comes nearer: 400, 280, 180) until neither the HITS collision nor the
  room mesh lies between it and the target.

The orbit then turns round the target; a cutscene's stage keeps the
cutscene's camera.

#### CAM: the game's cameras (`stage_config::read_cam_shots`, `cam_view_at`)

Every CAM record is one camera and the zones where it is used, in a row to
the end of the file (st006cfg: five records, 0x10 -> 0x1240 exactly). The
first word is the type, which fixes the record's head; the zone count is at
+0x14, the flags at +0x40, and the zones close the record (four
(x, y, z, 1) floor corners, the height they reach, 12 zero bytes; 0x50
each).

| Type | Head | Camera |
| --- | --- | --- |
| 0 (fixed) | 0xB0 | eye +0x90, field of view (radians) +0xA0, target +0x50: with flag 0x200 an offset from the player's feet (st006: 0 105 0), else a point |
| 0x100 (rail) | 0x490 | 32 eye points +0x80, 32 target points +0x200, 32 fields of view +0x380, point count +0x480 |
| 0x400 | 0x40 | zones only (other stages) |

The camera for a player is the record one of whose zones holds them (the
smallest zone when several do). On a rail, the player's nearest place on
the target rail picks the eye at the same place on the eye rail. st006:
POS 0 (2500 22 906) -> fixed camera at 3666 774 2524 looking at the
player; POS 1 -> fixed camera at 2699 734 3295 looking at 2479 300 4192;
POS 2 -> the rail, eye 2505 113 1179 looking north along the arena.

Tests: `stage_config_test.cpp` (fixed, rail, overlapping zones, a torn
file; spawn points, Game camera, another stage's configuration ignored),
`core_model_pipeline_test.cpp` (collision middle, a ledge above the floor,
a wall in the way).

## Fly camera (stages and collision views)

Toolbar button **✈** (shown for stage scenes, `.hits` files and whenever stage
collision is shown) switches orbit ↔ fly; the same button goes back.

* The fly camera starts exactly where the orbit camera stands, looking the same
  way (`view_camera_eye`), so the picture does not jump.
* A joystick appears under each finger only while it touches — the same rings
  as under the dolly's holding finger, with a knob under the finger:
  * left half: **move** — up / down = forward / back, left / right = sideways;
  * right half: **look** — left / right = turn about the camera's own axis,
    up / down = tilt.
* Forward follows the view, so flying while looking up or down climbs or
  descends (flight).
* Dead zone 12 %, quadratic response; full stick crosses the framed distance
  in about two seconds; turning 1.8 rad/s, tilting 1.2 rad/s.
* **Boost zones past the ring.** The knob follows the finger out of its
  ring. Past the ring an 18 dp dead band keeps the speed, then active zones
  (28 dp, the multiplier climbs by one across each) and rest zones (14 dp,
  it holds) follow in turn, up to x6. The zones are felt, not drawn: the HUD
  shows only the stick, its knob on the finger inside the ring and on the
  ring past it (`stick_knob`).
* **Back to orbit keeps the place.** Leaving fly sets the orbit pivot one
  framing distance ahead of the eye along the view; the orbit camera then
  stands where the flight ended, looking the same way, and turns round that
  point. Reset view and opening another file frame the model again.

Core: `camera_control.h` owns the fly eye, the orbit pivot, the stick
response with its zones and the fly step (`enter_fly`, `leave_fly`,
`reset`, `stick_response`, `stick_zone_radii`, `stick_knob`, `fly_tick`); the frame reads
them through `ViewControls::fly` / `eye` / `has_pivot` / `pivot` and
`ViewState::fly` / `fly_eye` / `orbit_pivot` (`view_frame`). Android
forwards finger offsets and the screen density (JNI `enterFly`, `leaveFly`,
`resetCamera`, `flyTick`, `stickKnob`) and draws what it gets. Tests:
`fly_camera_test.cpp`, `camera_control_test.cpp` (back to orbit stands at
the flight's end within 0.05 units and turns round the pivot; the zones
give x1.5 half through the first, hold x2 across its rest zone, cap at x6;
a boosted move covers six times the ground).

### One gesture per touch

Pinch zoom, dolly (hold one finger, slide the other) and camera turn (hold
one finger, twist two round it) never act together. The first of them that
engages owns the touch until every finger lifts (`camera::claim_gesture`,
`zoom_step`, `end_touch`). A pinch engages once it has moved the zoom by
4 % since the touch began; smaller changes, and any change while the two or
three fingers are still choosing their gesture, are provisional and undone
when dolly or turn takes the touch. Android asks before each step (JNI
`claimGesture`, `zoomStep`, `endTouch`). Test: `camera_control_test.cpp`.

## Shadow button

The shadow button appears only when the opened file has shadow files bound
(SHW hulls: Dante `pl000`, Vergil `pl001`, weapons such as `plwp_sword`), and
it switches only those shadows. Models without SHW (e.g. `em000`, `em028`),
stages and collision views have no shadow button and no made-up shadow (the
old mesh-outline fallback is gone); a model still stands on the plain floor,
a `.hits` collision view has no floor.
