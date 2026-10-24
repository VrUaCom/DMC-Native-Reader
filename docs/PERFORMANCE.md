# Performance: where a frame's time goes, and the plan

## What a frame does now (GPU renderer)

1. The render thread takes the session lock and poses the motion (cloth,
   cutscene).
2. `prepare_view`: the camera, the presentation mesh, effects, the room.
3. `build_gpu_frame`: the model's geometry is written down every frame (the
   room is cached on the GPU).
4. GLES draws into an offscreen framebuffer (MSAA resolved).
5. `glReadPixels` reads the whole frame back. The CPU waits for the GPU here.
6. The CPU draws the lines, markers and labels into the picture. In camera
   mode, the photo camera develops it (lens, polarizer, colour, noise).
7. The pixels are copied to a Java buffer, then into a Bitmap, and Android
   uploads that Bitmap to the GPU again to show it.

The picture goes GPU → CPU → GPU every frame.

## Measuring (v109)

The core's frame profile (`frame_profile.h`) times every stage: pose,
prepare, GPU build, GPU draw (submitting), read back (the GPU finishing plus
the read), CPU raster, overlays, camera, copy out. The benchmark
(Diagnostics → Benchmark) prints the average per frame under its numbers, and
it now measures the frame the view draws, the camera's included. Run it on
the phone and the stages say what to do first.

## Done in v109

- The photo camera's passes run on every core: the curves and colour pass,
  the lens pass, sharpening (the polarizer and the depth of field already
  did).
  - The lens pass shares its bilinear taps between channels (one sample a
    pixel without fringing).
  - Sharpening is fixed point.
  - The polarizer's sines come from row and column tables (the light's angle
    is a row part plus a column part).
  - On a 4-core desktop a 1080p frame with style, vignetting, noise at ISO
    12800, sharpening, distortion, fringing and the polarizer takes 69 ms
    instead of 212 ms.
- The GPU read back no longer makes a pass over every pixel to set alpha.
  Alpha is cleared to 1 and masked out of drawing.

## Done in v110: the photo camera on the GPU (step 4)

- The core writes the development as numbers (`camera_post.h`,
  `camera::make_post`): the look's curves as a 256-entry table per channel,
  saturation, tint or the cinema grade, vignetting, noise (the same hash),
  sharpening, distortion and fringing, the polarizer's ring and strength,
  and the depth of field's lens, focus and bokeh taps.
- The GLES backend runs it on the frame it drew, with no CPU pass and no
  second software render for the depth:
  1. The drawn frame and its depth are blitted (MSAA resolved) into
     textures.
  2. The auto focus reads a 16 × 16 grid of camera distances in the middle
     (packed into RGBA8, 1 KB back) and the CPU takes the median.
  3. The depth of field gathers up to 128 shaped taps per pixel from the
     colour and the depth texture, a nearer pixel reaching over a farther
     one.
  4. The lens (distortion, fringing), the polarizer and the look.
  5. Sharpening.
  6. The read back of the result.
- The CPU path stays for frames the GPU does not draw: the software
  renderer, a rolled camera (its picture is turned on the CPU), pictures
  past 4096 × 4096, tiles and RAW.
- Checked on the desktop with Mesa's llvmpipe (OpenGL ES 3.2 through EGL
  surfaceless; the app's own backend file). On st002 the GPU and the CPU
  differ by 3.9 on average with no development at all (two rasterisers). With
  the lens, the polarizer, sharpening and the depth of field the difference
  stays there (1.2–5.9). The auto focus agrees (870 against 873 units).

## Done in v113: frames straight to the screen (first part of step 5)

- A `SurfaceView` lies under the view. Its window goes to the GLES backend
  (`GpuViewBackend::set_window`, an `ANativeWindow`), which makes an EGL
  window surface for it from a configuration that draws into both
  pbuffers and windows.
- A view frame asks to be shown (`ViewControls::present`, set only by
  `renderToBuffer`). The core lets it when nothing is drawn on the CPU
  after the GPU: no lines, markers, labels or bones, no software depth, no
  development left to the CPU.
- The backend then blits the resolved (or developed) frame onto the window,
  scaled to it and turned upright, and swaps without waiting for the
  display. The frame comes back with its size and no pixels. JNI answers
  with status bit 4, and the view only draws what goes over it (HUD,
  subtitles, viewfinder) on a transparent background.
- Steps 5 to 7 of the frame are gone for these frames: no `glReadPixels`,
  no copy into Java, no Bitmap upload.
- A cutscene's fade is now part of the frame (`ViewState::fade`). The GPU
  lays it as a quad before the development, and the CPU lays it after its
  own raster, so both paths fade the scene before the camera develops it.
- Everything else still reads back: videos (the encoder takes Bitmaps),
  pictures, RAW, the benchmark, UV views, movies, frames with overlays, and
  any frame when the window is gone or refuses one.
- Settings → Graphics → Show frames switches it off.
- Checked with Mesa (a pbuffer stands in for the window, `PresentStandIn`):
  at the frame's own size the shown frame equals the read back one exactly,
  rows the right way up. Scaled to 1920 × 1080 and to 640 × 360 it differs
  only by the filtering. The GPU fade differs from the CPU fade by 0.15 on
  m03_b00 (the rasterisers alone: 0.58).
- Next: the overlays (lines, markers, labels, bones) as GPU passes, so
  those frames are shown directly too.

## Testing GPU work off the phone

`android/gles_view_backend.cpp` builds on Linux with Mesa's EGL and GLES
(`libegl-dev libgles-dev`, `EGL_PLATFORM=surfaceless`). A probe installs it
and renders through `render_session` like the app, so the GPU passes are
checked picture by picture before they reach the phone. llvmpipe's timings
mean nothing; only the phone's benchmark counts.

## The plan, in order of gain

1. **Asynchronous read back** (pixel pack buffers with fences, two or three
   in turn). The CPU reads the frame before last while the GPU draws the
   next, so the stall at step 5 goes away. It costs one frame of latency.
   Medium work.
2. **Pipelining the CPU and the GPU.** `GpuViewFrame` is already a snapshot
   of everything the GPU needs. One thread poses and prepares frame N+1
   under the session lock while the GL thread draws frame N from its
   snapshot. Medium work.
3. **Skinning on the GPU.** The bone matrices go to the vertex shader, and
   the model's vertices stay on the GPU instead of being rebuilt and
   uploaded every frame (step 3). Medium-large work.
4. **The photo camera as a shader** (done in v110). The curves, colour, vignetting, noise,
   polarizer and lens run as a fragment pass on the GPU frame. The depth of
   field reads the GPU's depth texture instead of drawing the whole frame
   again in software for its depth, which today is the most expensive part
   of camera mode with the depth of field on. Large work.
5. **Showing the frame without a round trip** (first part done in v113;
   the overlays on the GPU are left). GLES draws straight into the
   view's window surface (SurfaceView or TextureView). The HUD, subtitles
   and viewfinder are drawn by a Canvas view on top, and the lines and
   labels move to the GPU. Steps 5 to 7 above disappear. Captures (photo,
   video, RAW) keep the offscreen path. The largest work and the largest
   gain, especially at 4K–8K view resolutions.

Each step is measured with the benchmark before and after, on the phone.
