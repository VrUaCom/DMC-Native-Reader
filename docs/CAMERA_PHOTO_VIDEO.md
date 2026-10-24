# Photo and video camera

The reader has a photo camera and a video camera for the view: the picture
without the interface, through a lens and with a look of its own. The core
owns all of it (`spider/camera.h`, `spider/camera.cpp`): every setting with
its starting value, the sizes, the framing, the lens, the exposure, the
colour and the viewfinder. The Android shell keeps the chosen options
(SharedPreferences `camera_<id>`), lays the settings window out, counts the
self-timer down, saves the files and runs the encoder (`ViewRecorder`).

## Where

- **Camera mode:** the 📷 button beside ⋮ (top right) goes in, and the
  same button (or ↩) goes back to the scene. Set the scene up, shoot, go
  back, set it up again. In camera mode the two bottom rows (the motion
  strip and the tools) are the camera's bar:
  - A row of dials from the core (`camera::dials()`), each showing its value
    the way a camera does: Lens, Aperture, Mode, ISO, Shutter, EV, WB, Frame,
    Focus, CPL, Style, File.
  - A tapped dial opens its settings at the top of the view, over it (at
    most 55 % of its height, scrolling past that). The bars below stay where
    they are and the view keeps its size.
  - Dials and options are equal squares. Option names are short (the core's
    `Setting::options`), and what they mean is the setting's hint under its
    name (`Setting::hint`). Long dials (ISO,
    shutter) are sliders. Focus also opens the focus distance, blades and
    bokeh; CPL its strength; Style its contrast, saturation and sharpness;
    File the megapixels and the video size.
  - The actions: 🎛 settings, ▶/⏸ the scene, the big 📷 shutter and 🎥/⏹
    record, 🔄 reset view, ↩ back.
  - Camera mode is the core's setting, so the settings window switches it
    too.

- ⋮ menu squares: 📷 *Take a picture*, 🎥 *Record a video* (⏹ *Stop
  recording* while it runs), 🎛 *Camera settings* (always there).
- The three-finger tap takes the same picture as 📷.
- Pictures go to `Pictures/DMC Native Reader`, videos to `Movies/DMC Native
  Reader` (Android 10+; older systems: the app's own folders). Videos have no
  sound.

## A camera free in space

- In camera mode the camera stands at its own eye: the fly camera without
  the sticks. It does not orbit a point.
  - One finger turns it where it stands; what is under the finger follows
    the finger.
  - Pinch zooms. Two fingers move it sideways and up/down. Hold one finger
    and slide another to move it forward/back.
  - The gyroscope and walking turn and move it.
- The drone button adds the sticks to the same free camera.
- Back in the scene, it orbits again from where the camera stands, so the
  picture stays the same. If the drone was flying, it keeps flying.
- A view opened in camera mode gets the free camera too (where the view has
  a fly camera: stages and collision views). Reset view goes back to the
  start, still free.

## Camera mode gestures

| Gesture | Does |
|---|---|
| Pinch | Zoom (focal length) |
| One finger | Turn the camera; with the gyroscope it aims without moving the phone |
| Two fingers drag | Move the camera sideways, up and down |
| Two fingers twist | Turn the polarizer (when it is on) |
| Two-finger tap | Level the camera (no roll) and re-centre the gyroscope |
| Three fingers twist | Roll the camera (the picture turns with the fingers) |
| Three fingers up / down | Exposure compensation, a step per 50 dp |
| Three-finger tap | Take a picture |
| Double tap | Focus there (a small render's depth under the finger) |
| Long press | AF lock: keep the focus the auto focus found; again — auto |
| Slide along the right edge | The focus ring: up farther, down nearer, in log steps |
| Volume button | Shutter; hold it to start / stop a video |
| Walking (🚶 on) | Your steps, crouching and standing move the camera |

The three-finger sideways swipe (variants) and the four-finger tap (follow)
are off in camera mode. ℹ in the camera bar shows all of this in the app.

## Gyroscope (3DoF)

- 🧭 in the camera bar, or 🎛 → Gyroscope. It works in camera mode.
- The phone's orientation comes from the game rotation vector (no compass,
  so no jumps), with the screen's own rotation taken out. The camera looks
  out of the back of the phone.
- The core (`camera::gyro_turn`) turns the readings into how far the camera
  turns, not where it points. A finger, the drone's sticks and the phone all
  turn it together, and turning the phone back turns the camera back
  (× the sensitivity).
  - Turning right grows the yaw; looking up lowers the pitch.
  - Looking more than about 75° up or down holds the yaw, because the
    heading means nothing there.
- Smoothing takes a share of each turn per reading: off 100 %, low 50 %,
  medium 25 %, high 12 %. Sensitivity: 0.5×, 1×, 1.5×, 2×.
- Horizon:
  - **Locked**: the phone's roll is ignored, so pictures stay level, like a
    gimbal.
  - **Follows the phone**: the picture rolls with the phone, so in the hand
    the scene stays level with the world, and the picture is tilted.
- **Roll** is rendered exactly. A frame large enough to hold the turned one
  is drawn with the same focal length (`ViewState::tile` with a centred
  projection, so the GPU draws it too), then sampled back turned. Pictures,
  videos and RAW all carry it. The settings line shows GYRO and ROLL.
- Walking and crouching (6DoF) are ARCore's (below).

## Walking (ARCore, 6DoF)

- 🚶 in the camera bar, or 🎛 → Walking. The first time, a note explains
  ARCore (Google Play Services for AR, Google's privacy policy; the camera
  picture is never shown or kept) and the camera permission is asked. Google
  Play Services for AR is installed on request. A phone that cannot run
  ARCore says so; the gyroscope still turns the camera.
- `ArWalker` runs an ARCore session on its own thread with a 1 × 1 pbuffer
  GL context, because ARCore writes the camera image to a texture nobody
  draws. Plane finding and light estimation are off.
  - Every frame the phone's position (metres, gravity-aligned) is compared
    with the last. The step is split into forward and right along the
    ground, as the phone faces, and up.
  - The core (`camera::walk`) moves the drone's eye by that:
    `camera_control::walk_eye` moves along the ground the way the camera
    faces (looking down does not sink it), up along the world's up.
  - Scale: a scene unit is a centimetre, times the walk scale (0.25 … 20 m
    of scene per metre walked).
  - Height: follows you (crouch, stand) or fixed.
- Walking flies the drone, so it turns fly on, and the sticks keep working.
  The gyroscope keeps turning the camera. When tracking is lost, the next
  found pose starts fresh, with no jump.
- It stops when the app pauses, camera mode is left or walking is turned
  off.
- ARCore is an optional dependency (`com.google.ar:core:1.48.0`, the
  manifest meta-data `com.google.ar.core = optional`, the camera feature not
  required). The app runs without it.

## Drone

The fly camera (left stick flies, right stick looks) is the drone: its button (the drone picture) is in
the tools and in the camera bar. In camera mode the gyroscope looks around
from it.

## Settings (starting value in bold)

| Section | Setting | Options |
|---|---|---|
| Camera | Camera mode | **Off**, On (the view through the lens with the look, the frame, the grid, the settings line; two fingers turn the polarizer) |
| | Frame (pictures and videos) | **As the view**, 16:9, 3:2, 4:3, 1:1, 21:9, 2.35:1, 9:16, 2:3 |
| | Grid | Off, **thirds**, 4 × 4, golden ratio, centre cross |
| | Settings line | **Show**, Hide |
| Picture | File | **PNG**, JPEG, WebP lossless, RAW (DNG), RAW (DNG) + JPEG |
| | JPEG quality | 100, **95**, 90, 85, 75, 60 |
| | RAW data | **Linear RGB** (every editor), Bayer RGGB (one colour a pixel, like a sensor) |
| | Size (long side) | **As the view**, 1280, 1920, 2560, 3840, 5120, 6144, 7680 (8K) |
| | Megapixels (instead of the size) | **By the size**, 2, 5, 8, 12, 16, 20, 24, 33, 40, 48, 64, 100, 124, 150, 200 MP (in the picture's shape, a side at most 24576) |
| | Self-timer | **Off**, 2, 5, 10 s |
| | Shots per press | **1**, 3, 5, 10 (a quarter second apart) |
| | Subtitles in the picture | **Show**, Hide |
| Video | Size | 720p, **1080p**, 1440p, 2160p (the short side; the long side at most 4096) |
| | Frame rate | 24, 25, **30**, 50, 60 |
| | Bit rate | **Auto** (0.12 bit per pixel for H.264, 0.07 for H.265), 4 … 100 Mbit/s |
| | Codec | **H.264**, H.265 |
| | Key frame every | 0.5, **1**, 2, 5 s |
| | Stop after | **No limit**, 15 s, 30 s, 1, 5, 10 min |
| | Motion blur | **Off**, by the shutter (the frames before blend in) |
| | Subtitles in the video | **Show**, Hide |
| Lens | Focal length (full frame) | **As the view (pinch)**, 14, 18, 24, 28, 35, 50, 85, 105, 135, 200, 300, 400 mm |
| | Vignetting | **Off**, by the aperture, strong |
| | Distortion | **Off**, by the focal length (barrel when wide, pincushion when long) |
| | Colour fringing | **Off**, weak, strong |
| Aperture | Aperture (f-number) | f/0.5, f/0.95, f/1.2, f/1.4, f/2, f/2.8, **f/4**, f/5.6, f/8, f/11, f/16, f/22 (exposure in manual mode, vignetting, depth of field) |
| | Depth of field | **Off**, On |
| | Focus | **Auto (the middle of the frame)**, 0.5, 1, 2, 3, 5, 10, 20 m, infinity |
| | Aperture blades | **Round**, 9, 8, 7, 6, 5 (the shape of the bokeh) |
| | Bokeh highlights | **Soft**, bright |
| Filter | Polarizer | **Off**, On (circular; in camera mode two fingers turn it) |
| | Polarizer strength | Weak, **medium**, strong |
| Exposure | Mode | **Auto** (the rendered picture, moved by the compensation), Manual |
| | ISO | **Auto**, then thirds of a stop 25, 32, 40, 50 … 102400, then whole stops 204800, 409600, 819200, 1638400, 3276800, 4000000 (a slider) |
| | Shutter | Thirds of a stop 30", 25", 20" … 1", 0.8" … 1/4 … **1/60** … 1/2000 (a slider) |
| | Compensation | −3 … **0** … +3 EV |
| | Highlights | **Clip**, soft roll-off |
| | Sensor noise | Off, **by the ISO** (none at ISO 100), film grain |
| Gyroscope | Gyroscope | **Off**, On |
| | Horizon | **Locked**, follows the phone |
| | Sensitivity | 0.5×, **1×**, 1.5×, 2× |
| | Smoothing | Off, low, **medium**, high |
| Walking (ARCore) | Walk and crouch | **Off**, On |
| | A metre walked moves the camera | 0.25, 0.5, **1**, 2, 5, 10, 20 m |
| | Height | **Follows you**, fixed |
| Colour | White balance | **As rendered**, 5500, 6500, 7500, 3200, 4000, 5800, 2000, 10000 K |
| | Picture style | **Standard**, Vivid, Neutral, Monochrome, Sepia, Film, Cinema |
| | Contrast, saturation | −2 … **0** … +2 |
| | Sharpness | **Off**, low, medium, high |

The starting values leave the picture exactly as the view renders it.

## How it works

- **One camera.** Pictures and videos come from the same camera: the same
  frame, lens, aperture, filter, exposure and look. Only what a picture and
  a video are made into differs (file, megapixels, resolution, codec). The
  📷 and 🎥 squares stay separate. A choice is kept by its option's name, so
  a dial that gains values keeps what was chosen. Dials longer than 12
  values (ISO, shutter, megapixels, focal length) are sliders.
- **Polarizer.** It is part of the optics, so a RAW keeps it. Each pixel's
  light is partly polarized: 15 % of everything, up to 85 % more for whitish
  glare and highlights, up to 60 % for the blue of a sky. The filter holds
  back strength × polarized × sin²(ring − the light's angle). That angle
  turns slowly across the frame, as a sky's does. What it holds back darkens
  the pixel in linear light and deepens its colour. In camera mode the
  two-finger twist turns the ring instead of the view; half a turn is the
  same filter. The settings line shows CPL and the angle. In manual exposure
  it takes about 1.5 stops.

- **Framing.** A capture of another shape than the view is the largest rect
  of that shape in the view, and is rendered at its own size with the zoom
  scaled by the view's short side over the frame's short side. It shows
  exactly what the viewfinder's frame shows. A cutscene's camera is scaled
  the same way.
- **Focal length.** The view's zoom 1 is about 22 mm (a vertical half angle
  of 0.5 rad over 12 mm, half the short side of a full-frame sensor). A fixed
  focal length replaces the zoom, the cutscene camera's too.
- **Exposure.** In manual mode, ISO 100 at 1/60 and f/4 is the rendered
  picture; the gain is ISO/100 × shutter × 60 × (4/N)². Auto ISO picks the
  ISO that gives a gain of 1 (50 … 25600).
- **Developing order.** First the lens pass (distortion and fringing, radial,
  the corners kept). Then the per-channel curves: linear light × exposure ×
  white balance, the highlights, back to sRGB, the contrast S-curve and the
  film lift. Then saturation, the style tint, vignetting and noise (stronger
  in the shadows, a new pattern every frame), then sharpening.
- **White balance.** The picture counts as daylight (5500 K). A setting T
  multiplies by rgb(5500) / rgb(T) (black-body colours), normalised to the
  same brightness.
- **Video.** While recording, the render thread draws every frame at the
  video's size through the camera. The view shows it in the frame, and the
  recorder encodes the latest frame at the frame rate. The encoder is checked
  first, so a size or codec the phone cannot encode is a clear message.

- **Depth of field.** The capture is rendered with each pixel's distance
  from the camera. The GPU keeps its depth, so a software pass of the same
  frame gives it. A scene unit counts as a centimetre. The focus is the chosen
  distance, or the median distance in the middle of the frame (shown as AF in
  the settings line). A thin lens gives each pixel's circle of confusion:
  f² / (N (s − f)) · |z − s| / z on a 24 mm short side, capped at 5 % of the
  picture. Each pixel gathers up to 128 golden-angle points in its circle,
  pushed out to the blades' polygon. A nearer pixel reaches over a farther
  one but not the other way round, so a sharp subject keeps its edge. Bright
  bokeh weighs highlights up to 3 times. The viewfinder skips it while the
  view moves. On a 4-core desktop a 4K picture takes about 0.8 s.

- **Past one render (100 MP and more).** A picture with a side over 8192 is
  drawn in tiles of at most 4096 and put together. Every tile carries the
  whole picture's focal length and has its projection centre where the
  picture's centre falls in it (`ViewState::tile`; the software renderer
  draws tiles, the GPU frame has no off-centre projection). On st002, tiles
  against one render differ by 0.002 on average. 124 MP takes about 4 s on
  a 4-core desktop. Pictures are rendered and saved on a worker, one after
  another. A 124 MP picture needs about 1.5 GB of memory; when it is not
  there, the app says so.
- **RAW (DNG).** DNG is Adobe's open RAW. Lightroom, Camera Raw/Photoshop,
  darktable, RawTherapee, Snapseed and LibRaw open it.
  - The first IFD is a developed preview (at most 1024 long) for galleries.
  - The SubIFD holds the RAW: 16-bit linear light × the exposure × the lens's
    vignetting, plus the sensor noise, clipped at full scale. The depth of
    field, distortion and fringing are in it.
  - The white balance is the AsShotNeutral. The camera's colours are linear
    sRGB (ColorMatrix1 is XYZ D65 → sRGB).
  - ISO, shutter, f-number and focal length are tagged.
  - Colour, curves, style and sharpening are left to the editor.
  - Linear RGB is PhotometricInterpretation LinearRaw. Bayer is a CFA RGGB
    mosaic for the editor to demosaic.
  - Checked with LibRaw 0.22: both open, the Bayer pattern is read as RGGB,
    the white balance comes from the file.
  - RAW + JPEG writes both from one render.
- **NEF.** NEF is Nikon's own RAW: sensor data of Nikon cameras with Nikon's
  maker notes and compression. A file made here would have to pretend to be
  a Nikon camera, and Lightroom or NX Studio would not open it as one. DNG
  is the open RAW every editor opens; Nikon's own software is the exception.

Not yet: a photo's motion blur.
