package com.dmcrengine.nativeviewer;

import android.graphics.Bitmap;

public final class NativeBridge {
    static {
        System.loadLibrary("dmcviewer");
    }

    private NativeBridge() {}

    public static native long open(int fd, String filename);
    public static native long composeMods(long[] handles, String[] names);
    public static native void close(long handle);
    public static native String info(long handle);
    public static native String buildIdentity();
    /** The start screen's Info, written by the core (version passed in). */
    public static native String welcomeText(String appVersion);
    public static native String diagnostics(long handle);

    // Spider Black Widow is the single Android application/UI-state contract.
    // Java must not reconstruct policy from raw capabilities or diagnostics.
    public static native long blackWidowState(long handle);

    public static native int compositePartCount(long handle);
    public static native String compositePartName(long handle, int index);

    // Native-authoritative read-only projection for Android presentation.
    // Java displays this state but does not reconstruct placement/binding semantics.
    public static native String compositePartState(long handle, int index);

    public static native int compositePartNodeCount(long handle, int partIndex);
    public static native String compositePartNodeName(long handle, int partIndex, int nodeIndex);
    public static native int compositePartDefaultAttachmentSelector(long handle, int partIndex);
    public static native String attachModPartToHostJoint(
            long handle, int hostPartIndex, int childPartIndex, int hostJointIndex);
    public static native String resetModPartPlacement(long handle, int childPartIndex);

    public static native int imagePreviewWidth(long handle);
    public static native int imagePreviewHeight(long handle);
    public static native boolean imagePreview(long handle, Bitmap target);

    // Optional dual preview used by FXBANK and any future resource that has a
    // visual/playback representation plus a separate evidence/info rendering.
    public static native boolean hasDualPreview(long handle);
    public static native boolean infoPreviewActive(long handle);
    public static native boolean setInfoPreviewActive(long handle, boolean active);
    public static native String inspection(long handle);
    public static final int INSPECT_UV = 1;
    public static final int INSPECT_MESHES = 2;
    public static final int INSPECT_HIERARCHY = 3;
    public static native String inspectionTopic(long handle, int topic);

    // Companion-resource orchestration. Java only supplies a file descriptor;
    // native Spider/framing/DDS modules validate PTX and bind decoded texture
    // slots. Composite scenes may consume one shared PTX bank transactionally;
    // explicit per-part attachment remains the deterministic fallback.
    // Texture format change: native Spider texture re-encode action. The
    // result opens as a new session (0 on failure, reason in
    // reencodeTexturesDetail()); a PAC child rebuilds its PAC natively.
    public static native String[] textureFormatNames();
    public static native String[] textureFormatLabels();
    public static native long reencodeTextures(long handle, String format, boolean forceDx10);
    public static native String reencodeTexturesDetail();
    public static native String sourceFileName(long handle);
    public static native boolean writeSource(long handle, int fd);
    public static native boolean attachPtx(long handle, int fd, String filename);
    public static native boolean attachPtxToPart(long handle, int partIndex,
                                                  int fd, String filename);
    public static native String textureAttachmentInfo(long handle);
    // Newline-separated reasons the current view is shown but not read
    // canonically (orange warning). Empty when everything is canonical.
    public static native String nonCanonicalNotes(long handle);

    // Generic nested-resource browser contract. Parent modules publish typed
    // children; Android does not know whether the parent is PTX/PAC/PNST/etc.
    public static native int childResourceCount(long handle);
    public static native String childResourceTitle(long handle, int index);
    public static native int childResourcePreviewWidth(long handle, int index);
    public static native int childResourcePreviewHeight(long handle, int index);
    public static native boolean childResourcePreview(long handle, int index, Bitmap target);
    public static native long openUvGallery(long handle);
    public static native long openChild(long handle, int index);

    // Android allocates/reuses the destination Bitmap. JNI writes native RGBA
    // pixels directly into it, avoiding an intermediate Java int[] frame.
    public static native boolean render(long handle, int width, int height,
                                        float yaw, float pitch, float zoom,
                                        int renderFlags, Bitmap target);

    // Read-only PAC assembly: MODs in model space, slot-adjacent PTX, MOT library.
    public static native long assemblePac(long handle, String archiveName);
    /** A stage archive (st*.pac) as its assembled scene; 0 when it is not a stage. */
    public static native long openStage(int fd, String filename);
    // Spider stage / sound / export actions: the core decides and writes
    // every line; the shell carries out the named platform step.
    /** slot: 0 the base room's set, 1 the opened stage's (stage_slot.h). */
    public static native ActionOutcome loadStageConfig(int fd, String filename, int slot);
    public static native void clearStageConfig(int slot);
    /** File health of the session's file: "<level>\n<report>", level 0 fine, 1 notice, 2 problem. */
    public static native String fileHealth(long handle, String filename);
    /** The archives that come with the stage of a file name: {effect bank, configuration}, or empty. */
    public static native String[] stageArchives(String filename);
    /** The spawn points (POS) of the base room's configuration, by title. */
    public static native String[] baseSpawnPoints();
    /** Puts the model on the base room's spawn point `index`; -1 back on the room's own spot. */
    public static native boolean placeAtSpawn(int index);
    /** 1 when the session is a stage scene with its own companions, 0 when it uses the base room's. */
    public static native int stageSlot(long handle);
    /** "st001" for a stage scene, "" otherwise. */
    public static native String stageStem(long handle);
    /** The base room's four archives (Settings): a CompanionMenu with only its stage ops. */
    public static native CompanionMenu baseRoomMenu(boolean roomLoaded);
    /** The view held at a point (render pixels): marker card, joint, or scrub. */
    public static native ActionOutcome holdView(long handle, int width, int height,
                                                float yaw, float pitch, float zoom, int renderFlags,
                                                float panX, float panY, float roomYaw, boolean follow,
                                                float dolly, float x, float y,
                                                boolean joints, boolean scrub);
    public static native MarkerCard markerCard(int marker);
    /** id 0: the card went away. */
    public static native ActionOutcome runMarkerCardAction(int marker, int id);
    public static native NativeMenu soundMenu(long handle);
    /** Every effect of the loaded banks to play on the view (ids: rows; -1 stops). */
    public static native NativeMenu effectMenu(long handle);
    /** Plays effect row {@code id} of {@link #effectMenu} at the model's feet, looping; -1 stops. */
    public static native ActionOutcome playEffect(long handle, int id);
    public static native SoundPcm soundPcm(long handle, int index);
    /** The ⋮ menu for this session, from the facts only the shell holds. */
    public static native CompanionMenu companionMenu(long handle, boolean rootScene, boolean assembledPacSource,
                                                     boolean stageSource, boolean roomLoaded, boolean roomShown,
                                                     boolean recording);
    /** Runs a ⋮ item the core carries out (stage config); others answer STEP_NONE. */
    public static native ActionOutcome runCompanionAction(long handle, int id);
    /** The picked .ogg of a cutscene (a descriptor); the core's outcome. */
    public static native ActionOutcome loadCutsceneSound(long handle, int fd, String filename);
    /** The cutscene sound mix: {rate, channels}, or null without sound. */
    public static native int[] demoAudioFormat(long handle);
    /** The mix's sample frame at a timeline frame. */
    public static native long demoAudioPosition(long handle, float frame);
    /** `frames` sample frames of the mix from `position` (interleaved), null at its end. */
    public static native short[] demoAudioRead(long handle, long position, int frames);
    /** A gesture takes a cutscene's camera: {yaw, pitch, zoom} of its shot, or null. */
    public static native float[] demoTakeCamera(long handle);
    /** A cutscene's pad vibration at a timeline frame, 0..255 (0 none), chosen by the core. */
    public static native int demoVibration(long handle, float frame);
    /** A cutscene's subtitle at its current frame (lines joined by newlines), or null. */
    public static native String demoSubtitle(long handle);
    /** How to draw it: {centre x, top, size} as fractions of the picture, alpha, front rgb, back rgb; or null. */
    public static native float[] demoSubtitleStyle(long handle);
    /** A stage's start view, chosen by the core (its spawn point's game camera or floor): {yaw, pitch, zoom}, or null. */
    public static native float[] stageStartView(long handle);
    /** The exported picture's {width, height}, chosen by the core. */
    public static native int[] exportImageSize(long handle);
    public static native boolean renderExportImage(long handle, Bitmap target);
    /** HITS sources of a stage scene session. */
    public static native int stageCollisionSourceCount(long handle);
    // handles[0] = character PAC, the rest are added (plwp_*.pac weapons hang
    // from the body joint the game records for them).
    public static native long assemblePacs(long[] handles, String[] names, int variant);
    // Selectable positions of an archive (em000.pac classes and weapons,
    // em028.pac dress states); empty when there is only one look.
    public static native String[] archiveVariantNames(String archiveName);
    public static native int motionLibraryCount(long handle);
    public static native String motionLibraryName(long handle, int index);
    // Physical top-level PAC slot containing the nested MOT pack; -1 when the
    // motion did not come from a nested pack. Used only for UI grouping.
    public static native int motionLibraryPackSlot(long handle, int index);

    // MotionScript playback is separate from raw MOT playback. A PAC may own
    // several independent controllers; every script gets its own Play button
    // beside each MOT it can actually address.
    public static native int motionScriptCount(long handle);
    public static native int motionScriptSlot(long handle, int scriptIndex);
    public static native boolean motionScriptCanPlayMotion(
            long handle, int scriptIndex, int motionIndex);
    public static native String loadLibraryMotionScript(
            long handle, int scriptIndex, int motionIndex);

    // MOT playback. Frames are MOT timeline units (60 per second in DMC3).
    // load* return a human-readable report; hasMotion tells whether it bound.
    public static native String loadLibraryMotion(long handle, int index);
    public static native String loadMotion(long handle, int fd, String filename);
    public static native boolean hasMotion(long handle);
    public static native float motionEndFrame(long handle);
    public static native float motionLoopStartFrame(long handle);
    public static native boolean setMotionFrame(long handle, float frame);
    public static native void clearMotion(long handle);

    // SHW shadow hulls bound to the assembled models (drawn with RENDER_SHADOWS).

    // Attack collision shapes on the body bones (drawn with RENDER_COLLISION):
    // used attack ids, and the selection (-1 = every attack) with its label.
    /** Render with gesture controls: pan (framing radii), room twist, follow. */
    public static native boolean renderEx(long handle, int width, int height, float yaw,
            float pitch, float zoom, int flags, float panX, float panY, float roomYaw,
            boolean follow, float dolly, android.graphics.Bitmap target);
    /**
     * Worker-thread frame: pose the MOT at motionFrame (NaN: keep the pose),
     * render and write RGBA8 into a direct buffer; 0 failed, 1 ok, 2 pose failed.
     */
    public static native int renderToBuffer(long handle, int width, int height, float yaw,
            float pitch, float zoom, int flags, float panX, float panY, float roomYaw,
            boolean follow, float dolly, float motionFrame, java.nio.ByteBuffer out);
    /** "model|joint", "room|joint", "placed|joint" or "none|joint" under (x, y). */
    public static native String pickView(long handle, int width, int height, float yaw,
            float pitch, float zoom, int flags, float panX, float panY, float roomYaw,
            boolean follow, float dolly, float x, float y, boolean place);
    /** Loads the stage's effect bank (st*_effect.pac); its summary, or null. */
    public static native String loadRoomEffects(int fd, String filename, int slot);
    public static native void clearRoomEffects(int slot);
    /** The room drawn with the session has scrolling textures (clouds) or effects. */
    public static native boolean roomAnimated(long handle);
    /**
     * Kinds (distinct record flags) of the HITS shown with the session, one
     * string each: "flags|records|floors|walls|ceilings|rgb".
     */
    public static native String[] collisionKinds(long handle);
    /** The core's account of the collision: {title, summary, "rgb|heading|lines"...}; attack: hitboxes, else HITS. */
    public static native String[] collisionReport(long handle, boolean attack);
    /** {framing distance at dolly 0 (model units), largest dolly} of the session. */
    public static native float[] cameraMetrics(long handle);
    /** Builds the viewer room from a stage archive; its summary, or null. */
    public static native String loadRoom(int fd, String filename);
    public static native void clearRoom();
    public static native int roomSpotCount();
    public static native int nextRoomSpot();
    /** Number of independently retained HITS environment-collision sources. */
    public static native int roomCollisionSourceCount();
    public static native boolean isStageSession(long handle);
    public static native int[] collisionAttackIds(long handle);
    public static native String selectCollisionAttack(long handle, int attack);
    /** Class events the playing enemy accepts (em000 family: "Death"). */
    public static native String[] classEventNames(long handle);
    /** Sends class event {@code index}; false unless a Script Play action runs. */
    public static native boolean triggerClassEvent(long handle, int index);
    /** Draws breakable stage objects broken (bmodel + one-shot beff) or intact. */
    public static native void setRoomBroken(boolean broken);

    /** Draws the view on the graphics chip (OpenGL ES 3) when true, else on the CPU. */
    public static native void setGpuRendering(boolean enabled);

    /** The view's window surface the GPU shows frames on (null: none); returns once the old one is let go. */
    public static native void setPresentSurface(android.view.Surface surface);

    /** Frames of the view straight onto its window (true) or read back into a picture. */
    public static native void setGpuPresent(boolean enabled);

    /** Draw with the DMC3 (game) shaders where the game has one; the core picks them. */
    public static native void setDmc3Shaders(boolean enabled);
    /** Light the DMC3 shaders' stage and models with the stage configuration's LIG2 table (experimental). */
    public static native void setGameLights(boolean enabled);
    /** GPU picture quality: MSAA samples (0 = off), mipmaps, anisotropic filtering (1 = off). */
    public static native void setGpuOptions(int msaaSamples, boolean mipmaps, int anisotropy);
    /** The graphics chip's limits: {max MSAA samples, max anisotropy}; zeros without a GPU. */
    public static native int[] gpuCapabilities();
    /** A frame buffer in native memory (not the Java heap); free it once with freeFrameBuffer. */
    public static native java.nio.ByteBuffer allocateFrameBuffer(long bytes);
    public static native void freeFrameBuffer(java.nio.ByteBuffer buffer);
    /**
     * Draws frames back to back for `seconds` with these view settings (camera turning, motion
     * advancing from motionStart unless NaN) and returns the report: renderer, size, fps.
     */
    public static native String benchmarkView(long handle, int width, int height, float yaw, float pitch,
            float zoom, int flags, float panX, float panY, float roomYaw, boolean follow, float dolly,
            float motionStart, float seconds, String settings);
    /** What draws the frames ("GPU: OpenGL ES 3.2 / Adreno ...") and the frame counts. */
    public static native String rendererInfo();
    // Camera (core camera_control): fly eye, orbit pivot, joystick response.
    /** The fly camera on, where the orbit camera of that view stands. */
    public static native boolean enterFly(long handle, int width, int height, float yaw, float pitch,
            float zoom, int flags, float panX, float panY, float roomYaw, boolean follow, float dolly);
    /** Back to orbit round where the flight looked: {yaw, pitch, panX, panY, dolly}. */
    public static native float[] leaveFly(long handle, float yaw, float pitch);
    /** Orbit the framed model again (Reset view, another file). */
    public static native void resetCamera();
    /** One frame of flying from the sticks' finger offsets (px): {yaw, pitch, moveBoost, moveZone}. */
    public static native float[] flyTick(long handle, float yaw, float pitch,
            boolean moveHeld, float moveDx, float moveDy, boolean lookHeld, float lookDx, float lookDy,
            float density, float dtSeconds);
    /** Where the HUD draws a stick's knob for a finger offset (px): {dx, dy}. */
    public static native float[] stickKnob(float dx, float dy, float density);
    // One gesture per touch: zoom, dolly and camera turn never act together.
    public static final int GESTURE_DOLLY = 2;
    public static final int GESTURE_TURN = 3;
    /** Dolly or turn asks for the touch; false when another gesture owns it. */
    public static native boolean claimGesture(int gesture);
    /** May the pinch move the zoom from the touch's start to `next`? */
    public static native boolean zoomStep(float touchZoom, float next, boolean settling);
    /** Every finger lifted. */
    public static native void endTouch();

    // Photo and video camera (core spider/camera.h).
    /** One row per setting: "id\tsection\tlabel\tstart\toption\toption...". */
    public static native String[] cameraSettings();
    /** The chosen option of every setting, in cameraSettings() order. */
    public static native void setCameraChoices(int[] choices);
    /**
     * Kind 0 a picture, 1 a video, of a view rendered at that size: {width, height, format,
     * quality or bits per second, fps, key frame ms, limit s, timer s, burst, subtitles}.
     */
    public static native int[] cameraPlan(int kind, int viewWidth, int viewHeight);
    public static native boolean renderCapture(long handle, int kind, int viewWidth, int viewHeight, float yaw,
                                               float pitch, float zoom, int renderFlags, float panX, float panY,
                                               float roomYaw, boolean follow, float dolly, float motionFrame,
                                               float motionFps, Bitmap target);
    public static native int renderVideoFrame(long handle, int viewWidth, int viewHeight, float yaw, float pitch,
                                              float zoom, int renderFlags, float panX, float panY, float roomYaw,
                                              boolean follow, float dolly, float motionFrame,
                                              java.nio.ByteBuffer buffer);
    public static native void beginVideo();
    /** Camera mode is on: the bottom bars are the camera's. */
    public static native boolean cameraMode();

    /** The screen keeps its orientation: the gyroscope or walking aims the camera. */
    public static native boolean cameraHoldsOrientation();

    /** What else turning setting `index` to `option` changes: {message, "index\toption", ...}, or null. */
    public static native String[] cameraFollowUps(int index, int option);

    /** A cutscene's camera drives the view: camera mode films through it until a finger takes it. */
    public static native boolean cameraFollowsCuts(long handle);
    /** The camera bar's dials: "setting index[,index ...]\ttitle\tvalue". */
    public static native String[] cameraDials();
    /** Walking (ARCore) on in camera mode. */
    public static native boolean walkActive();
    /** The phone moved (metres: forward, right along the ground as it faces, up): the drone walks. */
    public static native boolean walk(float yaw, float forward, float right, float up);
    /** Gyroscope on in camera mode. */
    public static native boolean gyroActive();
    /** A phone reading (heading, elevation, roll; radians) gives the camera's turn {yaw, pitch}. */
    public static native float[] gyroTurn(float heading, float elevation, float roll);
    public static native void gyroReset();
    /** Rolls the camera by radians (0 reads it). */
    public static native float cameraRoll(float radians);
    public static native void cameraLevel();
    /** Focus where a finger tapped (view render pixels); the line to show. */
    public static native String cameraFocusAt(long handle, int width, int height, float yaw, float pitch, float zoom,
                                              int renderFlags, float panX, float panY, float roomYaw, boolean follow,
                                              float dolly, float x, float y);
    public static native String cameraFocusLock();
    public static native String cameraFocusRing(float steps);
    /** True in camera mode with the polarizer on: two fingers turn it. */
    public static native boolean cameraPolarizerTurnable();
    /** Turns the polarizer by radians (0 reads it); its angle, 0 .. pi. */
    public static native float cameraPolarizer(float turn);
    public static native void setCameraPolarizer(float radians);
    /** A RAW picture (DNG) into the open file fd; developed (or null) gets the developed picture. */
    public static native boolean writeCaptureDng(long handle, int viewWidth, int viewHeight, float yaw, float pitch,
                                                 float zoom, int renderFlags, float panX, float panY, float roomYaw,
                                                 boolean follow, float dolly, float motionFrame, float motionFps,
                                                 int fd, Bitmap developed);
    /** {float[] {shown, x, y, w, h, grid lines x0, y0, x1, y1 ...}, the settings line}. */
    public static native Object[] viewfinder(long handle, int width, int height, float zoom, boolean recording,
                                             int seconds);
}
