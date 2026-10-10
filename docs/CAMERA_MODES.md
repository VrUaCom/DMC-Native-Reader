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
* Opening another file, Reset view or leaving the stage / collision view
  returns to the orbit camera.

Core: `ViewState::fly` / `fly_eye`, `camera_basis`, `fly_move`,
`view_camera_eye` (`view_renderer.h`), `ViewControls::fly` / `eye` and
`session_camera_eye` (`resource_session.h`). Every frame, pick and benchmark
reads the same fly state (JNI `setFlyCamera`, `flyMove`, `cameraEye`). Test:
`fly_camera_test.cpp` (switching keeps the picture to 0.02 px, forward is the
image centre, right / up are screen right / up, turning keeps the eye).

## Shadow button

The shadow button appears only when the opened file has shadow files bound
(SHW hulls: Dante `pl000`, Vergil `pl001`, weapons such as `plwp_sword`), and
it switches only those shadows. Models without SHW (e.g. `em000`, `em028`),
stages and collision views have no shadow button and no made-up shadow (the
old mesh-outline fallback is gone); a model still stands on the plain floor,
a `.hits` collision view has no floor.
