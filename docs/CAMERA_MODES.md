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
  it holds) follow in turn, up to x6. The HUD draws the zone rings once the
  finger leaves the ring and shows the multiplier over the knob.
* **Back to orbit keeps the place.** Leaving fly sets the orbit pivot one
  framing distance ahead of the eye along the view; the orbit camera then
  stands where the flight ended, looking the same way, and turns round that
  point. Reset view and opening another file frame the model again.

Core: `camera_control.h` owns the fly eye, the orbit pivot, the stick
response with its zones and the fly step (`enter_fly`, `leave_fly`,
`reset`, `stick_response`, `stick_zone_radii`, `fly_tick`); the frame reads
them through `ViewControls::fly` / `eye` / `has_pivot` / `pivot` and
`ViewState::fly` / `fly_eye` / `orbit_pivot` (`view_frame`). Android
forwards finger offsets and the screen density (JNI `enterFly`, `leaveFly`,
`resetCamera`, `flyTick`, `stickZoneRadii`) and draws what it gets. Tests:
`fly_camera_test.cpp`, `camera_control_test.cpp` (back to orbit stands at
the flight's end within 0.05 units and turns round the pivot; the zones
give x1.5 half through the first, hold x2 across its rest zone, cap at x6;
a boosted move covers six times the ground).

## Shadow button

The shadow button appears only when the opened file has shadow files bound
(SHW hulls: Dante `pl000`, Vergil `pl001`, weapons such as `plwp_sword`), and
it switches only those shadows. Models without SHW (e.g. `em000`, `em028`),
stages and collision views have no shadow button and no made-up shadow (the
old mesh-outline fallback is gone); a model still stands on the plain floor,
a `.hits` collision view has no floor.
