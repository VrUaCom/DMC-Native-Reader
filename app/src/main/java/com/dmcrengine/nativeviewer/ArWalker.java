package com.dmcrengine.nativeviewer;

import android.app.Activity;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES11Ext;
import android.opengl.GLES20;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;

import com.google.ar.core.ArCoreApk;
import com.google.ar.core.Camera;
import com.google.ar.core.Config;
import com.google.ar.core.Frame;
import com.google.ar.core.Pose;
import com.google.ar.core.Session;
import com.google.ar.core.TrackingFailureReason;
import com.google.ar.core.TrackingState;

/**
 * The phone's steps for the camera mode's walking: ARCore tracks the phone
 * (6DoF) through its camera, on a thread of its own with a 1 x 1 offscreen
 * GL context (ARCore writes the camera image to a texture nobody draws).
 * Each frame gives how far the phone moved, as metres forward and right
 * along the ground the way it faces and up; the core turns that into the
 * camera's walk. The camera's picture is never shown or stored.
 */
final class ArWalker {
    interface Listener {
        /** The phone moved (metres): forward and right along the ground as it faces, up. */
        void onStep(float forward, float right, float up);
        /** Tracking started, waits or was lost (a line to show). */
        void onTrackingState(String text);
    }

    private final Activity activity;
    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private HandlerThread thread;
    private Handler worker;
    private Session session;
    private EGLDisplay display = EGL14.EGL_NO_DISPLAY;
    private EGLContext context = EGL14.EGL_NO_CONTEXT;
    private EGLSurface surface = EGL14.EGL_NO_SURFACE;
    private boolean running;
    private float[] last;
    private TrackingState lastState;

    ArWalker(Activity activity, Listener listener) {
        this.activity = activity;
        this.listener = listener;
    }

    /** What this phone can do: a line to show when it cannot walk, or null when it can (maybe after an install). */
    static String unavailable(Activity activity) {
        final ArCoreApk.Availability availability = ArCoreApk.getInstance().checkAvailability(activity);
        if (availability.isTransient()) return null;  // still checking: try
        if (availability == ArCoreApk.Availability.UNSUPPORTED_DEVICE_NOT_CAPABLE) {
            return "This phone does not support ARCore: walking is not available (the gyroscope still turns the camera)";
        }
        return null;
    }

    /**
     * Opens ARCore and starts following the phone; false with the reason shown when it cannot
     * (an install of Google Play Services for AR is asked for and the caller tries again on resume).
     */
    boolean start(boolean userAskedInstall) {
        if (running) return true;
        try {
            if (session == null) {
                switch (ArCoreApk.getInstance().requestInstall(activity, userAskedInstall)) {
                    case INSTALL_REQUESTED:
                        listener.onTrackingState("Installing Google Play Services for AR…");
                        return false;
                    case INSTALLED:
                    default:
                        break;
                }
                session = new Session(activity);
                final Config config = new Config(session);
                config.setUpdateMode(Config.UpdateMode.LATEST_CAMERA_IMAGE);
                config.setPlaneFindingMode(Config.PlaneFindingMode.DISABLED);
                config.setLightEstimationMode(Config.LightEstimationMode.DISABLED);
                config.setFocusMode(Config.FocusMode.AUTO);
                session.configure(config);
            }
        } catch (Exception error) {
            listener.onTrackingState("ARCore cannot start: " + error.getClass().getSimpleName()
                    + (error.getMessage() != null ? " — " + error.getMessage() : ""));
            closeSession();
            return false;
        }
        running = true;
        last = null;
        lastState = null;
        thread = new HandlerThread("ar-walker");
        thread.start();
        worker = new Handler(thread.getLooper());
        worker.post(this::open);
        return true;
    }

    /** Stops following (the app paused, camera mode left, walking off); start() resumes. */
    void stop() {
        if (!running) return;
        running = false;
        final HandlerThread old = thread;
        final Handler oldWorker = worker;
        if (oldWorker != null) {
            oldWorker.post(() -> {
                try {
                    if (session != null) session.pause();
                } catch (Exception ignored) {
                    // Pausing a session that never resumed.
                }
                releaseGl();
                old.quitSafely();
            });
        }
        thread = null;
        worker = null;
    }

    /** Stops and lets ARCore go (walking turned off, the app closing). */
    void close() {
        stop();
        closeSession();
    }

    private void closeSession() {
        if (session != null) {
            final Session closing = session;
            session = null;
            try {
                closing.close();
            } catch (Exception ignored) {
                // Already closed.
            }
        }
    }

    private void open() {
        try {
            display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
            final int[] version = new int[2];
            EGL14.eglInitialize(display, version, 0, version, 1);
            final int[] attributes = {EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                    EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT, EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8,
                    EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_NONE};
            final EGLConfig[] configs = new EGLConfig[1];
            final int[] count = new int[1];
            EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0);
            if (count[0] < 1) throw new IllegalStateException("no EGL config");
            context = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT,
                    new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE}, 0);
            surface = EGL14.eglCreatePbufferSurface(display, configs[0],
                    new int[]{EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE}, 0);
            EGL14.eglMakeCurrent(display, surface, surface, context);
            final int[] texture = new int[1];
            GLES20.glGenTextures(1, texture, 0);
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texture[0]);
            session.setCameraTextureName(texture[0]);
            session.resume();
        } catch (Exception error) {
            final String text = "ARCore cannot start: " + error.getClass().getSimpleName();
            main.post(() -> listener.onTrackingState(text));
            running = false;
            releaseGl();
            return;
        }
        worker.post(this::tick);
    }

    private void tick() {
        if (!running || session == null) return;
        try {
            final Frame frame = session.update();
            final Camera camera = frame.getCamera();
            final TrackingState state = camera.getTrackingState();
            if (state != lastState) {
                lastState = state;
                final String text = state == TrackingState.TRACKING ? "🚶 Walking: ARCore follows your steps"
                        : state == TrackingState.PAUSED ? "Walking waits: " + reason(camera.getTrackingFailureReason())
                        : "Walking stopped";
                main.post(() -> listener.onTrackingState(text));
            }
            if (state == TrackingState.TRACKING) {
                final Pose pose = camera.getPose();
                final float[] now = {pose.tx(), pose.ty(), pose.tz()};
                if (last != null) {
                    // The phone looks along its -z: its heading on the ground.
                    final float[] z = pose.getZAxis();
                    float fx = -z[0], fz = -z[2];
                    final float length = (float) Math.hypot(fx, fz);
                    if (length > 1.0e-3f) {
                        fx /= length;
                        fz /= length;
                        final float dx = now[0] - last[0], dy = now[1] - last[1], dz = now[2] - last[2];
                        final float forward = dx * fx + dz * fz;
                        final float right = dx * -fz + dz * fx;
                        if (Math.abs(forward) + Math.abs(right) + Math.abs(dy) > 1.0e-5f) {
                            main.post(() -> listener.onStep(forward, right, dy));
                        }
                    }
                }
                last = now;
            } else {
                last = null;  // no jump when it finds itself again
            }
        } catch (Exception error) {
            last = null;
        }
        if (running && worker != null) worker.postDelayed(this::tick, 16L);
    }

    private static String reason(TrackingFailureReason reason) {
        if (reason == null) return "move the phone slowly";
        switch (reason) {
            case INSUFFICIENT_LIGHT: return "too dark";
            case EXCESSIVE_MOTION: return "moving too fast";
            case INSUFFICIENT_FEATURES: return "point the phone at something with detail";
            case CAMERA_UNAVAILABLE: return "the camera is busy";
            default: return "move the phone slowly";
        }
    }

    private void releaseGl() {
        if (display != EGL14.EGL_NO_DISPLAY) {
            EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
            if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface);
            if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context);
            // No eglTerminate: the default display is shared with the renderer's context.
        }
        display = EGL14.EGL_NO_DISPLAY;
        context = EGL14.EGL_NO_CONTEXT;
        surface = EGL14.EGL_NO_SURFACE;
    }
}
