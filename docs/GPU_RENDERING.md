# GPU view rendering (OpenGL ES 3)

The model / stage view can be drawn on the device's graphics chip (Adreno on
Snapdragon phones such as the Galaxy S26 Ultra, Mali / Xclipse elsewhere)
instead of the software rasteriser. Settings → Render → **Renderer** switches
between "GPU (graphics chip)" (default) and "CPU (software)"; the hint under it
shows what draws the frames now, e.g. `GPU: OpenGL ES 3.2 / Adreno (TM) ...`,
and how many frames went each way.

## Split of responsibilities

| Part | Where | What it owns |
| --- | --- | --- |
| `include/dmcresource/view_gpu.h` | Core (portable) | the frame contract: `GpuViewFrame`, `GpuBatch`, `GpuViewBackend`, the switch and the counters |
| `view_renderer.cpp` | Core | builds the frame with the software rasteriser's rules; draws the line overlays; falls back to software |
| `android/gles_view_backend.cpp` | Android shell (`libdmcviewer.so`) | EGL context, shaders, textures, room buffers, MSAA target, read-back |

Core decides what every triangle looks like; the backend only evaluates those
decisions per pixel. `render_view` asks the backend first and keeps the
software path for:

* wireframe and UV-layout views (lines only);
* any frame the backend refuses (no OpenGL ES 3, target too large, GL error) —
  the software picture is then returned unchanged, byte for byte;
* the switch set to CPU.

After eight failed frames in a row the backend stops trying for the session.

## What runs where

GPU (one offscreen pass per frame, 4x MSAA up to ~2.6 Mpx, 2x up to 9 Mpx):

1. opaque room texels (alpha-tested cut-outs < 32, soft textures' texels >= 240);
2. the plain floor (when no room is shown);
3. the model — alpha-blended batches writing depth, then GS ALPHA 2 / 3
   (additive / subtractive) batches without depth;
4. soft room texels and additive / subtractive room geometry (light shafts);
5. the shadow footprint (stencil: each pixel darkened once, depth tolerance
   1% of the framing radius, as the software pass);
6. effect quads (E / P records), image-space and affine as before.

CPU, on top of the read-back picture: attack shapes, room HITS, bones and joint
markers (they never had a depth test). Also on the CPU, once per frame: the
camera light per vertex (`vertex_light`), grouping the posed model into
batches, and the effect quad corners.

The room is grouped and uploaded once and kept on the GPU until the room
changes (`GpuRoomKey`: buffers, counts and a sample of the data). Textures are
uploaded once with mipmaps and evicted after ~900 unused frames or past
768 MiB.

## Differences from the software picture

Measured with the same frame on Mesa llvmpipe (Dante, `pl000.pac`, 960x720):
PSNR 45.9 dB (model), 42.7 dB (smooth textures), 44.7 dB (shadow + bones),
31.2 dB with the `st001.pac` room. The differences are intended:

* model texture coordinates are perspective-correct (the software model pass
  interpolates them affinely);
* smooth textures use trilinear mipmaps and up to 8x anisotropic filtering —
  distant floors no longer shimmer;
* edges are anti-aliased;
* the room's facing light is evaluated per vertex instead of per triangle;
* soft room texels are not sorted back to front (the software pass sorts).

`app/src/test/native/gpu_view_frame_test.cpp` pins the contract with a
recording backend: batches and light terms, the camera matrices against the
software joint markers (< 0.01 px), near / far planes, the overlays on top,
byte-identical fallback, wireframe / UV on the CPU, and the room key.
