package com.dmcrengine.nativeviewer;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.VelocityTracker;
import android.view.View;

public final class DmcRenderView extends View {
    private static final int RENDER_WIREFRAME = 1 << 0;
    private static final int RENDER_HIERARCHY = 1 << 1;
    private static final int RENDER_UV_LAYOUT = 1 << 5;
    private static final int RENDER_SHADOWS = 1 << 6;
    private static final int RENDER_COLLISION = 1 << 7;
    private static final int RENDER_ROOM = 1 << 8;
    private static final int RENDER_PREVIEW = 1 << 13;
    // Fast preview: while a finger moves, the view spins or a motion plays,
    // frames render at half size with nearest texels; 150 ms after the last
    // touch a full-quality frame follows.
    private boolean fastPreview = true;
    private boolean touching;
    private Bitmap previewBitmap;
    private boolean showingPreview;
    private final Runnable fullFrame = () -> {
        if (!moving()) renderNow();
    };

    // Gestures (each can be switched off in the Gestures window).
    public static final int G_PAN = 1;            // two fingers drag: pan
    public static final int G_DOUBLE_TAP = 1 << 1; // reset view / place on room floor
    public static final int G_EDGE_MOTION = 1 << 2; // swipe from a side edge: prev / next MOT
    public static final int G_TAP_PAUSE = 1 << 3;  // tap the model: pause / resume
    public static final int G_FLING = 1 << 4;      // flick: inertia turn
    public static final int G_SCRUB = 1 << 5;      // long press + drag: scrub frames
    public static final int G_TWIST = 1 << 6;      // two-finger twist: turn the room / model
    public static final int G_THREE_SWIPE = 1 << 7; // three fingers sideways: prev / next position
    public static final int G_TOP_UI = 1 << 8;     // swipe down from the top edge: hide / show UI
    public static final int G_BONE = 1 << 9;       // long press: joint under the finger
    public static final int G_SCREENSHOT = 1 << 10; // three-finger tap: save a PNG
    public static final int G_FOLLOW = 1 << 11;    // four-finger tap: camera follows / stays
    public static final int G_ALL = (1 << 12) - 1;

    /** What the gestures ask of the activity. */
    public interface GestureListener {
        void onStepMotion(int direction);
        void onStepVariant(int direction);
        void onToggleUi();
        void onScreenshot(Bitmap image);
        void onGestureNotice(String text);
    }

    private GestureListener gestureListener;
    private int gestures = G_ALL;
    private float panX;
    private float panY;
    private float roomYaw;
    private boolean follow;

    // Touch state.
    private static final int EDGE_NONE = 0, EDGE_LEFT = 1, EDGE_RIGHT = 2, EDGE_TOP = 3;
    private static final float EDGE_DP = 48.0f;
    private int edgeStart = EDGE_NONE;
    private int maxPointers;
    private float downX, downY;
    private long downTime;
    private float multiX, multiY;          // centroid of the fingers down
    private float multiStartX, multiStartY;
    private float multiShift;              // three-finger sideways travel
    private boolean multiShiftTaken;
    private float twistAngle;
    private int multiCount;
    private boolean scrubbing;
    private boolean scrubWasPlaying;
    private float scrubFrame;
    private float lastMotionFrame;
    private VelocityTracker velocity;
    private float spinYaw, spinPitch;      // inertia, radians per second
    private long spinLastMs;
    private final Runnable spinTick = new Runnable() {
        @Override public void run() {
            final long now = SystemClock.uptimeMillis();
            final float dt = Math.min(0.05f, (now - spinLastMs) / 1000.0f);
            spinLastMs = now;
            yaw -= spinYaw * dt;
            pitch = Math.max(-1.55f, Math.min(1.55f, pitch - spinPitch * dt));
            final float decay = (float) Math.pow(0.08, dt);  // about -92 % per second
            spinYaw *= decay;
            spinPitch *= decay;
            if (!motionPlaying) renderThrottled(false);
            if (Math.abs(spinYaw) + Math.abs(spinPitch) > 0.05f) {
                postOnAnimation(this);
            } else {
                spinYaw = 0.0f;
                spinPitch = 0.0f;
                renderNow();
            }
        }
    };

    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final ScaleGestureDetector scaleDetector;
    private final GestureDetector tapDetector;
    private Bitmap bitmap;
    private long session;
    // DMC3 models face +Z while the camera looks along +Z, so yaw 0 shows the
    // back. Start from a front three-quarter view.
    private static final float DEFAULT_YAW = (float) Math.PI - 0.65f;
    private float yaw = DEFAULT_YAW;
    private float pitch = -0.45f;
    private float zoom = 1.0f;
    private int renderFlags;
    // The chosen room (Settings): kept across sessions; native skips it for
    // stages (SCM / archives holding SCM) and in wireframe / UV views.
    private boolean roomVisible;
    private boolean hierarchyAvailable;
    private boolean staticImagePreview;
    private float lastX;
    private float lastY;
    // Finger that turns the camera; others only pinch.
    private int activePointerId = MotionEvent.INVALID_POINTER_ID;
    private long lastRenderMs;

    // MOT playback: frames are MOT timeline units, 60 per second in DMC3.
    private static final float MOTION_FRAMES_PER_SECOND = 60.0f;
    // Viewer settings (SettingsDialog): render size, motion frame interval
    // and speed, quality flags (native RenderFlag bits 9-12), shadows at open.
    private int maxRenderSide = 720;
    private long motionMinFrameMs = 33;
    private float motionSpeed = 1.0f;
    private int settingsFlags;
    private boolean shadowsAtOpen = true;
    private boolean motionPlaying;
    private long motionStartMs;
    private float motionEndFrame;
    private float motionLoopStartFrame;
    private final Runnable motionTick = new Runnable() {
        @Override public void run() {
            if (!motionPlaying || session == 0) return;
            final long now = SystemClock.uptimeMillis();
            if (now - lastRenderMs >= motionMinFrameMs) {
                lastMotionFrame = currentMotionFrame(now);
                if (!NativeBridge.setMotionFrame(session, lastMotionFrame)) {
                    motionPlaying = false;
                    return;
                }
                renderNow();
            }
            postOnAnimation(this);
        }
    };

    public DmcRenderView(Context context) {
        super(context);
        setBackgroundColor(0xff121216);
        scaleDetector = new ScaleGestureDetector(context,
                new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                    @Override public boolean onScale(ScaleGestureDetector detector) {
                        if (staticImagePreview) return false;
                        zoom *= detector.getScaleFactor();
                        zoom = Math.max(0.15f, Math.min(8.0f, zoom));
                        renderThrottled(false);
                        return true;
                    }
                });
        tapDetector = new GestureDetector(context, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onDown(MotionEvent e) {
                return true;
            }

            @Override public boolean onSingleTapConfirmed(MotionEvent e) {
                if (!singleFingerGesture() || !enabled(G_TAP_PAUSE) || session == 0
                        || !NativeBridge.hasMotion(session)) {
                    return false;
                }
                if (!pick(e.getX(), e.getY(), false).startsWith("model")) return false;
                if (motionPlaying) {
                    pauseMotion();
                    notice("\u23f8 Paused");
                } else {
                    resumeMotionAt(lastMotionFrame);
                    notice("\u25b6 Playing");
                }
                return true;
            }

            @Override public boolean onDoubleTap(MotionEvent e) {
                if (!singleFingerGesture() || !enabled(G_DOUBLE_TAP)) return false;
                final String hit = (renderFlags & RENDER_ROOM) != 0 ? pick(e.getX(), e.getY(), true) : "";
                if (hit.startsWith("placed")) {
                    notice("Model placed here");
                    renderNow();
                } else {
                    panX = 0.0f;
                    panY = 0.0f;
                    resetView();
                    notice("View reset");
                }
                return true;
            }

            @Override public void onLongPress(MotionEvent e) {
                if (!singleFingerGesture()) return;
                String text = "";
                if (enabled(G_BONE)) {
                    final String hit = pick(e.getX(), e.getY(), false);
                    final int bar = hit.indexOf('|');
                    if (bar >= 0 && bar + 1 < hit.length()) text = "\ud83e\uddb4 " + hit.substring(bar + 1);
                }
                if (enabled(G_SCRUB) && session != 0 && NativeBridge.hasMotion(session)) {
                    scrubbing = true;
                    scrubWasPlaying = motionPlaying;
                    pauseMotion();
                    scrubFrame = lastMotionFrame;
                    text = text.isEmpty() ? "Scrub: drag sideways" : text + " \u00b7 drag to scrub";
                }
                if (!text.isEmpty()) {
                    performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS);
                    notice(text);
                }
            }
        });
    }

    public void setGestureListener(GestureListener listener) {
        gestureListener = listener;
    }

    public void setGestures(int mask) {
        gestures = mask;
    }

    public boolean isFollowing() {
        return follow;
    }

    public void setFollow(boolean value) {
        follow = value;
        renderNow();
    }

    private boolean enabled(int gesture) {
        return (gestures & gesture) != 0;
    }

    private boolean singleFingerGesture() {
        return maxPointers <= 1 && edgeStart == EDGE_NONE && !isUvLayoutVisible();
    }

    private void notice(String text) {
        if (gestureListener != null) gestureListener.onGestureNotice(text);
    }

    /** What is under a view pixel ("model|joint", "room|…", "placed|…", "none|…"). */
    private String pick(float viewX, float viewY, boolean place) {
        if (session == 0 || getWidth() <= 0 || getHeight() <= 0) return "none|";
        final int rw = renderWidth();
        final int rh = renderHeight();
        final String hit = NativeBridge.pickView(session, rw, rh, yaw, pitch, zoom,
                renderFlags | settingsFlags, panX, panY, roomYaw, follow,
                viewX * rw / getWidth(), viewY * rh / getHeight(), place);
        return hit == null ? "none|" : hit;
    }

    /** Continue the bound MOT from `frame` (after a pause or a scrub). */
    public void resumeMotionAt(float frame) {
        if (session == 0 || staticImagePreview || !NativeBridge.hasMotion(session)) return;
        motionEndFrame = NativeBridge.motionEndFrame(session);
        motionLoopStartFrame = NativeBridge.motionLoopStartFrame(session);
        motionStartMs = SystemClock.uptimeMillis()
                - Math.round(Math.max(0.0f, frame) * 1000.0f / (MOTION_FRAMES_PER_SECOND * motionSpeed));
        if (!motionPlaying) {
            motionPlaying = true;
            postOnAnimation(motionTick);
        }
    }

    /** The picture on screen, for a screenshot. */
    public Bitmap snapshot() {
        if (showingPreview) {
            touching = false;
            renderNow();  // a full-quality frame for the picture
        }
        if (bitmap == null || bitmap.isRecycled()) return null;
        return bitmap.copy(Bitmap.Config.ARGB_8888, false);
    }

    private boolean canUseStaticImagePreview() {
        if (session == 0) return false;
        final BlackWidowState state = BlackWidowState.fromNative(
                NativeBridge.blackWidowState(session));
        return state.canPreviewImage;
    }

    private void releaseBitmap() {
        if (bitmap != null) {
            bitmap.recycle();
            bitmap = null;
        }
        if (previewBitmap != null) {
            previewBitmap.recycle();
            previewBitmap = null;
        }
        showingPreview = false;
    }

    private Bitmap previewTarget(int width, int height) {
        if (previewBitmap != null && !previewBitmap.isRecycled()
                && previewBitmap.getWidth() == width && previewBitmap.getHeight() == height) {
            return previewBitmap;
        }
        if (previewBitmap != null) previewBitmap.recycle();
        try {
            previewBitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        } catch (IllegalArgumentException | OutOfMemoryError error) {
            previewBitmap = null;
        }
        return previewBitmap;
    }

    private boolean moving() {
        return touching || motionPlaying || spinYaw != 0.0f || spinPitch != 0.0f;
    }

    private Bitmap writableBitmap(int width, int height) {
        if (width <= 0 || height <= 0) return null;
        if (bitmap != null && !bitmap.isRecycled()
                && bitmap.getWidth() == width && bitmap.getHeight() == height) {
            return bitmap;
        }
        releaseBitmap();
        try {
            bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            return bitmap;
        } catch (IllegalArgumentException | OutOfMemoryError error) {
            bitmap = null;
            return null;
        }
    }

    private void clearStaticImagePreview() {
        staticImagePreview = false;
        releaseBitmap();
        invalidate();
    }

    private float rawMotionFrame(long nowMs) {
        return (nowMs - motionStartMs) * (MOTION_FRAMES_PER_SECOND * motionSpeed / 1000.0f);
    }

    /** Applies the viewer settings; a playing motion keeps its frame. */
    public void applySettings(int maxSide, long frameMs, float speed, int flags, boolean shadows,
                              boolean preview) {
        fastPreview = preview;
        final long now = SystemClock.uptimeMillis();
        if (motionPlaying && speed > 0.0f && speed != motionSpeed) {
            final float frame = rawMotionFrame(now);
            motionStartMs = now - Math.round(frame * 1000.0f / (MOTION_FRAMES_PER_SECOND * speed));
        }
        maxRenderSide = Math.max(128, Math.min(1024, maxSide));
        motionMinFrameMs = Math.max(8, frameMs);
        motionSpeed = speed > 0.0f ? speed : 1.0f;
        settingsFlags = flags;
        shadowsAtOpen = shadows;
        if (!staticImagePreview) renderNow();
    }

    private float currentMotionFrame(long nowMs) {
        final float elapsed = rawMotionFrame(nowMs);
        if (motionEndFrame <= 0.0f || elapsed <= motionEndFrame) return elapsed;
        final float loopSpan = motionEndFrame - motionLoopStartFrame;
        if (loopSpan <= 0.0f) return motionEndFrame;
        return motionLoopStartFrame + ((elapsed - motionEndFrame) % loopSpan);
    }

    /** Start looping the MOT currently bound to the native session. */
    public boolean startMotion() {
        if (session == 0 || staticImagePreview || !NativeBridge.hasMotion(session)) return false;
        motionEndFrame = NativeBridge.motionEndFrame(session);
        motionLoopStartFrame = NativeBridge.motionLoopStartFrame(session);
        motionStartMs = SystemClock.uptimeMillis();
        lastMotionFrame = 0.0f;
        if (!motionPlaying) {
            motionPlaying = true;
            postOnAnimation(motionTick);
        }
        return true;
    }

    /** Freeze on the current pose (the motion stays bound). */
    public void pauseMotion() {
        final boolean was = motionPlaying;
        motionPlaying = false;
        removeCallbacks(motionTick);
        if (was) {
            removeCallbacks(fullFrame);
            postDelayed(fullFrame, 150);
        }
    }

    public boolean isMotionPlaying() {
        return motionPlaying;
    }

    public void setSession(long newSession) {
        pauseMotion();
        session = newSession;
        // Shadows start on; native ignores the flag when no SHW is bound.
        renderFlags = (shadowsAtOpen ? RENDER_SHADOWS : 0) | (roomVisible ? RENDER_ROOM : 0);
        hierarchyAvailable = false;
        staticImagePreview = false;
        releaseBitmap();
        invalidate();
        if (session == 0) return;
        if (BlackWidowState.fromNative(NativeBridge.blackWidowState(session)).uvMapView) {
            renderFlags = RENDER_UV_LAYOUT;
        }

        if (canUseStaticImagePreview()) {
            loadStaticImagePreview();
        } else {
            resetView();
        }
    }

    private void loadStaticImagePreview() {
        if (!canUseStaticImagePreview()) {
            clearStaticImagePreview();
            return;
        }

        final int width = NativeBridge.imagePreviewWidth(session);
        final int height = NativeBridge.imagePreviewHeight(session);
        final long expected = (long) width * (long) height;
        if (width <= 0 || height <= 0 || expected <= 0L ||
                expected > Integer.MAX_VALUE) {
            clearStaticImagePreview();
            return;
        }

        final Bitmap target = writableBitmap(width, height);
        if (target == null || !NativeBridge.imagePreview(session, target)) {
            clearStaticImagePreview();
            return;
        }

        staticImagePreview = true;
        lastRenderMs = SystemClock.uptimeMillis();
        invalidate();
    }

    public void resetView() {
        yaw = DEFAULT_YAW;
        pitch = -0.45f;
        zoom = 1.0f;
        panX = 0.0f;
        panY = 0.0f;
        roomYaw = 0.0f;
        spinYaw = 0.0f;
        spinPitch = 0.0f;
        if (staticImagePreview) {
            invalidate();
        } else {
            renderNow();
        }
    }

    public void toggleWireframe() {
        if (staticImagePreview || isUvLayoutVisible()) return;
        renderFlags ^= RENDER_WIREFRAME;
        renderNow();
    }

    public boolean isWireframe() {
        return !staticImagePreview && !isUvLayoutVisible() &&
                (renderFlags & RENDER_WIREFRAME) != 0;
    }

    public void toggleHierarchy() {
        if (staticImagePreview || isUvLayoutVisible() || !hierarchyAvailable) return;
        renderFlags ^= RENDER_HIERARCHY;
        renderNow();
    }

    public void setHierarchyAvailable(boolean available) {
        final boolean hierarchyWasRequested = (renderFlags & RENDER_HIERARCHY) != 0;
        hierarchyAvailable = !staticImagePreview && available;
        if ((!hierarchyAvailable || isUvLayoutVisible()) && hierarchyWasRequested) {
            renderFlags &= ~RENDER_HIERARCHY;
            renderNow();
        }
    }

    public boolean isHierarchyVisible() {
        return !staticImagePreview && !isUvLayoutVisible() && hierarchyAvailable &&
                (renderFlags & RENDER_HIERARCHY) != 0;
    }

    public void toggleShadows() {
        if (staticImagePreview || isUvLayoutVisible()) return;
        renderFlags ^= RENDER_SHADOWS;
        renderNow();
    }

    public void setRoomVisible(boolean visible) {
        roomVisible = visible;
        if (isUvLayoutVisible()) return;
        if (visible) {
            renderFlags |= RENDER_ROOM;
        } else {
            renderFlags &= ~RENDER_ROOM;
        }
        if (!staticImagePreview) renderNow();
    }

    public void refreshRoom() {
        if (!staticImagePreview && !isUvLayoutVisible()) renderNow();
    }

    public void setCollisionVisible(boolean visible) {
        if (staticImagePreview || isUvLayoutVisible()) return;
        if (visible) {
            renderFlags |= RENDER_COLLISION;
        } else {
            renderFlags &= ~RENDER_COLLISION;
        }
        renderNow();
    }

    public boolean isCollisionVisible() {
        return !staticImagePreview && !isUvLayoutVisible() &&
                (renderFlags & RENDER_COLLISION) != 0;
    }

    public boolean isShadowVisible() {
        return !staticImagePreview && !isUvLayoutVisible() &&
                (renderFlags & RENDER_SHADOWS) != 0;
    }

    public boolean isUvLayoutVisible() {
        return !staticImagePreview && (renderFlags & RENDER_UV_LAYOUT) != 0;
    }

    private int renderWidth() {
        int w = Math.max(64, getWidth());
        int h = Math.max(64, getHeight());
        int max = maxRenderSide;
        if (w <= max && h <= max) return w;
        float s = Math.min((float) max / w, (float) max / h);
        return Math.max(64, Math.round(w * s));
    }

    private int renderHeight() {
        int w = Math.max(64, getWidth());
        int h = Math.max(64, getHeight());
        int max = maxRenderSide;
        if (w <= max && h <= max) return h;
        float s = Math.min((float) max / w, (float) max / h);
        return Math.max(64, Math.round(h * s));
    }

    public void renderNow() {
        if (session == 0 || getWidth() <= 0 || getHeight() <= 0) return;
        if (staticImagePreview) {
            invalidate();
            return;
        }

        final boolean preview = fastPreview && moving() && (renderFlags & RENDER_UV_LAYOUT) == 0;
        final int rw = preview ? Math.max(64, renderWidth() / 2) : renderWidth();
        final int rh = preview ? Math.max(64, renderHeight() / 2) : renderHeight();
        final Bitmap target = preview ? previewTarget(rw, rh) : writableBitmap(rw, rh);
        if (target == null || !NativeBridge.renderEx(
                session, rw, rh, yaw, pitch, zoom, renderFlags | settingsFlags | (preview ? RENDER_PREVIEW : 0),
                panX, panY, roomYaw, follow, target)) {
            releaseBitmap();
            invalidate();
            return;
        }
        showingPreview = preview;
        lastRenderMs = SystemClock.uptimeMillis();
        invalidate();
        if (preview) {
            removeCallbacks(fullFrame);
            postDelayed(fullFrame, 150);
        }
    }

    private void renderThrottled(boolean force) {
        if (staticImagePreview) return;
        long now = SystemClock.uptimeMillis();
        if (force || now - lastRenderMs >= 45) renderNow();
    }

    @Override protected void onDetachedFromWindow() {
        pauseMotion();
        removeCallbacks(spinTick);
        removeCallbacks(fullFrame);
        super.onDetachedFromWindow();
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        // The side edges belong to the system back gesture; keep a 200 dp band
        // in the middle of each edge for the motion swipes (Android 10+).
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
            final float density = getResources().getDisplayMetrics().density;
            final int edge = Math.round(EDGE_DP * density);
            final int band = Math.round(100.0f * density);
            final int mid = h / 2;
            final java.util.ArrayList<android.graphics.Rect> rects = new java.util.ArrayList<>();
            rects.add(new android.graphics.Rect(0, Math.max(0, mid - band), edge, Math.min(h, mid + band)));
            rects.add(new android.graphics.Rect(Math.max(0, w - edge), Math.max(0, mid - band), w, Math.min(h, mid + band)));
            setSystemGestureExclusionRects(rects);
        }
        if (staticImagePreview) {
            invalidate();
        } else {
            renderNow();
        }
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!staticImagePreview) {
            final Bitmap shown = showingPreview ? previewBitmap : bitmap;
            if (shown == null || shown.isRecycled()) return;
            canvas.drawBitmap(shown, null,
                    new android.graphics.Rect(0, 0, getWidth(), getHeight()), paint);
            return;
        }
        if (bitmap == null || bitmap.isRecycled()) return;

        final float sx = (float) getWidth() / (float) bitmap.getWidth();
        final float sy = (float) getHeight() / (float) bitmap.getHeight();
        final float scale = Math.min(sx, sy);
        final float drawWidth = bitmap.getWidth() * scale;
        final float drawHeight = bitmap.getHeight() * scale;
        final float left = (getWidth() - drawWidth) * 0.5f;
        final float top = (getHeight() - drawHeight) * 0.5f;
        canvas.drawBitmap(bitmap, null,
                new RectF(left, top, left + drawWidth, top + drawHeight), paint);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (staticImagePreview) return true;

        scaleDetector.onTouchEvent(event);
        // A new touch resets the state below before the tap detector sees it
        // (a double tap is recognised on the second ACTION_DOWN).
        if (event.getActionMasked() != MotionEvent.ACTION_DOWN) tapDetector.onTouchEvent(event);
        if (velocity == null) velocity = VelocityTracker.obtain();
        velocity.addMovement(event);
        final float density = getResources().getDisplayMetrics().density;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: {
                touching = true;
                removeCallbacks(spinTick);
                spinYaw = 0.0f;
                spinPitch = 0.0f;
                activePointerId = event.getPointerId(0);
                lastX = event.getX(0);
                lastY = event.getY(0);
                downX = lastX;
                downY = lastY;
                downTime = event.getEventTime();
                maxPointers = 1;
                scrubbing = false;
                multiShift = 0.0f;
                multiShiftTaken = false;
                final float edge = EDGE_DP * density;
                edgeStart = EDGE_NONE;
                if (enabled(G_TOP_UI) && lastY < edge) {
                    edgeStart = EDGE_TOP;
                } else if (enabled(G_EDGE_MOTION) && lastX < edge) {
                    edgeStart = EDGE_LEFT;
                } else if (enabled(G_EDGE_MOTION) && lastX > getWidth() - edge) {
                    edgeStart = EDGE_RIGHT;
                }
                tapDetector.onTouchEvent(event);
                return true;
            }
            case MotionEvent.ACTION_POINTER_DOWN:
                // More fingers: pinch / pan / twist, no turning.
                maxPointers = Math.max(maxPointers, event.getPointerCount());
                scrubbing = false;
                edgeStart = EDGE_NONE;
                rebaseToPointer(event, activePointerId);
                beginMulti(event, -1);
                if (event.getPointerCount() >= 3) {
                    multiStartX = multiX;
                    multiStartY = multiY;
                }
                return true;
            case MotionEvent.ACTION_POINTER_UP: {
                // The finger that stays becomes the turning finger, measured
                // from where it is now (no jump after a pinch).
                final int up = event.getActionIndex();
                // Three or more fingers: their travel counts until the first
                // one lifts (the centre jumps as fingers leave).
                if (event.getPointerCount() >= 3 && !multiShiftTaken) {
                    multiShift = multiX - multiStartX;
                    multiShiftTaken = true;
                }
                if (event.getPointerId(up) == activePointerId) {
                    final int keep = up == 0 ? 1 : 0;
                    activePointerId = event.getPointerId(keep);
                }
                rebaseToPointer(event, activePointerId);
                beginMulti(event, up);
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                if (isUvLayoutVisible()) return true;
                final int count = event.getPointerCount();
                if (count >= 2) {
                    moveMulti(event);
                    return true;
                }
                final int index = event.findPointerIndex(activePointerId);
                if (index < 0) return true;
                final float x = event.getX(index);
                final float y = event.getY(index);
                if (edgeStart != EDGE_NONE && maxPointers == 1) {
                    // Not the edge swipe after all (vertical, or the wrong way):
                    // it turns the view like any drag.
                    final float mx = x - downX, my = y - downY;
                    if (Math.hypot(mx, my) > 16.0f * density) {
                        final boolean along = edgeStart == EDGE_TOP ? (my > Math.abs(mx))
                                : edgeStart == EDGE_LEFT ? (mx > Math.abs(my)) : (-mx > Math.abs(my));
                        if (!along) edgeStart = EDGE_NONE;
                    }
                }
                if (scrubbing) {
                    // About 8 px per frame, clamped to the motion.
                    scrubFrame = Math.max(0.0f, scrubFrame + (x - lastX) / (8.0f * density / 2.5f));
                    final float end = NativeBridge.motionEndFrame(session);
                    if (end > 0.0f && scrubFrame > end) scrubFrame = end;
                    lastMotionFrame = scrubFrame;
                    if (NativeBridge.setMotionFrame(session, scrubFrame)) renderThrottled(false);
                } else if (edgeStart == EDGE_NONE && !scaleDetector.isInProgress()) {
                    float dx = x - lastX;
                    float dy = y - lastY;
                    // Grab-and-turn: the surface under the finger follows it.
                    yaw -= dx * 0.008f;
                    pitch -= dy * 0.008f;
                    pitch = Math.max(-1.55f, Math.min(1.55f, pitch));
                    renderThrottled(false);
                }
                lastX = x;
                lastY = y;
                return true;
            }
            case MotionEvent.ACTION_UP: {
                touching = false;
                finishGesture(event, density);
                activePointerId = MotionEvent.INVALID_POINTER_ID;
                renderThrottled(true);
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                touching = false;
                activePointerId = MotionEvent.INVALID_POINTER_ID;
                scrubbing = false;
                recycleVelocity();
                renderThrottled(true);
                return true;
            default:
                return true;
        }
    }

    private void recycleVelocity() {
        if (velocity != null) {
            velocity.recycle();
            velocity = null;
        }
    }

    /** Baselines of the fingers that stay down (`skip`: the one lifting). */
    private void beginMulti(MotionEvent event, int skip) {
        float sx = 0.0f, sy = 0.0f;
        int n = 0;
        int first = -1, second = -1;
        for (int i = 0; i < event.getPointerCount(); ++i) {
            if (i == skip) continue;
            sx += event.getX(i);
            sy += event.getY(i);
            ++n;
            if (first < 0) first = i; else if (second < 0) second = i;
        }
        multiCount = n;
        if (n == 0) return;
        multiX = sx / n;
        multiY = sy / n;
        if (second >= 0) {
            twistAngle = (float) Math.atan2(event.getY(second) - event.getY(first),
                    event.getX(second) - event.getX(first));
        }
    }

    private void moveMulti(MotionEvent event) {
        final int n = event.getPointerCount();
        if (n != multiCount) beginMulti(event, -1);
        float sx = 0.0f, sy = 0.0f;
        for (int i = 0; i < n; ++i) {
            sx += event.getX(i);
            sy += event.getY(i);
        }
        final float cx = sx / n, cy = sy / n;
        if (n == 2 && maxPointers == 2) {
            boolean changed = false;
            if (enabled(G_PAN)) {
                // Camera-plane pan in framing radii: the content under the
                // fingers follows them (2.96 = camera distance / focal ratio).
                final float unit = 2.96f / (zoom * Math.max(1, Math.min(getWidth(), getHeight())));
                panX -= (cx - multiX) * unit;
                panY += (cy - multiY) * unit;
                changed = true;
            }
            if (enabled(G_TWIST)) {
                final float angle = (float) Math.atan2(event.getY(1) - event.getY(0), event.getX(1) - event.getX(0));
                float delta = angle - twistAngle;
                if (delta > Math.PI) delta -= (float) (2.0 * Math.PI);
                if (delta < -Math.PI) delta += (float) (2.0 * Math.PI);
                twistAngle = angle;
                if (Math.abs(delta) > 0.0005f) {
                    // With a room the model turns in it; without, the view turns.
                    if ((renderFlags & RENDER_ROOM) != 0) roomYaw -= delta; else yaw -= delta;
                    changed = true;
                }
            }
            if (changed) renderThrottled(false);
        }
        multiX = cx;
        multiY = cy;
    }

    private void finishGesture(MotionEvent event, float density) {
        final float dx = event.getX() - downX;
        final float dy = event.getY() - downY;
        final long duration = event.getEventTime() - downTime;
        if (scrubbing) {
            scrubbing = false;
            if (scrubWasPlaying) resumeMotionAt(scrubFrame);
            notice("Frame " + Math.round(scrubFrame));
            recycleVelocity();
            return;
        }
        if (maxPointers >= 4) {
            if (enabled(G_FOLLOW) && duration < 500) {
                follow = !follow;
                notice(follow ? "\\ud83c\\udfa5 Camera follows the model" : "\\ud83c\\udfa5 Camera stays in place");
                renderNow();
            }
        } else if (maxPointers == 3) {
            final float mx = multiShiftTaken ? multiShift : multiX - multiStartX;
            if (enabled(G_THREE_SWIPE) && Math.abs(mx) > 80.0f * density && gestureListener != null) {
                gestureListener.onStepVariant(mx < 0 ? 1 : -1);
            } else if (enabled(G_SCREENSHOT) && duration < 500 && Math.abs(mx) < 24.0f * density
                    && gestureListener != null) {
                final Bitmap image = snapshot();
                if (image != null) gestureListener.onScreenshot(image);
            }
        } else if (maxPointers == 1) {
            if (edgeStart == EDGE_TOP && dy > 60.0f * density) {
                if (gestureListener != null) gestureListener.onToggleUi();
            } else if (edgeStart == EDGE_LEFT && dx > 80.0f * density) {
                if (gestureListener != null) gestureListener.onStepMotion(-1);
            } else if (edgeStart == EDGE_RIGHT && dx < -80.0f * density) {
                if (gestureListener != null) gestureListener.onStepMotion(1);
            } else if (edgeStart == EDGE_NONE && enabled(G_FLING) && velocity != null) {
                velocity.computeCurrentVelocity(1000);
                final float vx = velocity.getXVelocity();
                final float vy = velocity.getYVelocity();
                if (Math.hypot(vx, vy) > 900.0f * density / 2.5f) {
                    spinYaw = vx * 0.008f;
                    spinPitch = vy * 0.008f * 0.5f;
                    spinLastMs = SystemClock.uptimeMillis();
                    postOnAnimation(spinTick);
                }
            }
        }
        recycleVelocity();
    }

    private void rebaseToPointer(MotionEvent event, int pointerId) {
        final int index = event.findPointerIndex(pointerId);
        if (index < 0) return;
        lastX = event.getX(index);
        lastY = event.getY(index);
    }
}
