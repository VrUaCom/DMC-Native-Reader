package com.dmcrengine.nativeviewer;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;

public final class DmcRenderView extends View {
    private static final int RENDER_WIREFRAME = 1 << 0;
    private static final int RENDER_HIERARCHY = 1 << 1;

    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final ScaleGestureDetector scaleDetector;
    private Bitmap bitmap;
    private long session;
    private float yaw = 0.65f;
    private float pitch = -0.45f;
    private float zoom = 1.0f;
    private int renderFlags;
    private boolean hierarchyAvailable;
    private boolean staticImagePreview;
    private float lastX;
    private float lastY;
    private long lastRenderMs;

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
    }

    public void setSession(long newSession) {
        session = newSession;
        renderFlags = 0;
        hierarchyAvailable = false;
        staticImagePreview = false;
        bitmap = null;
        invalidate();
        if (session == 0) return;

        if (NativeBridge.imagePreviewAvailable(session)) {
            loadStaticImagePreview();
        } else {
            resetView();
        }
    }

    private void loadStaticImagePreview() {
        if (session == 0 || !NativeBridge.imagePreviewAvailable(session)) {
            staticImagePreview = false;
            bitmap = null;
            invalidate();
            return;
        }

        final int width = NativeBridge.imagePreviewWidth(session);
        final int height = NativeBridge.imagePreviewHeight(session);
        final int[] pixels = NativeBridge.imagePreview(session);
        final long expected = (long) width * (long) height;
        if (width <= 0 || height <= 0 || expected <= 0L ||
                expected > Integer.MAX_VALUE || pixels == null ||
                pixels.length != (int) expected) {
            staticImagePreview = false;
            bitmap = null;
            invalidate();
            return;
        }

        bitmap = Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888);
        staticImagePreview = true;
        lastRenderMs = SystemClock.uptimeMillis();
        invalidate();
    }

    public void resetView() {
        yaw = 0.65f;
        pitch = -0.45f;
        zoom = 1.0f;
        if (staticImagePreview) {
            invalidate();
        } else {
            renderNow();
        }
    }

    public void toggleWireframe() {
        if (staticImagePreview) return;
        renderFlags ^= RENDER_WIREFRAME;
        renderNow();
    }

    public boolean isWireframe() {
        return !staticImagePreview && (renderFlags & RENDER_WIREFRAME) != 0;
    }

    public void toggleHierarchy() {
        if (staticImagePreview || !hierarchyAvailable) return;
        renderFlags ^= RENDER_HIERARCHY;
        renderNow();
    }

    public void setHierarchyAvailable(boolean available) {
        final boolean hierarchyWasRequested = (renderFlags & RENDER_HIERARCHY) != 0;
        hierarchyAvailable = !staticImagePreview && available;
        if (!hierarchyAvailable && hierarchyWasRequested) {
            renderFlags &= ~RENDER_HIERARCHY;
            renderNow();
        }
    }

    public boolean isHierarchyVisible() {
        return !staticImagePreview && hierarchyAvailable &&
                (renderFlags & RENDER_HIERARCHY) != 0;
    }

    private int renderWidth() {
        int w = Math.max(64, getWidth());
        int h = Math.max(64, getHeight());
        int max = 720;
        if (w <= max && h <= max) return w;
        float s = Math.min((float) max / w, (float) max / h);
        return Math.max(64, Math.round(w * s));
    }

    private int renderHeight() {
        int w = Math.max(64, getWidth());
        int h = Math.max(64, getHeight());
        int max = 720;
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

        int rw = renderWidth();
        int rh = renderHeight();
        int[] pixels = NativeBridge.render(session, rw, rh, yaw, pitch, zoom,
                renderFlags);
        if (pixels == null || pixels.length != rw * rh) {
            bitmap = null;
            invalidate();
            return;
        }
        bitmap = Bitmap.createBitmap(pixels, rw, rh, Bitmap.Config.ARGB_8888);
        lastRenderMs = SystemClock.uptimeMillis();
        invalidate();
    }

    private void renderThrottled(boolean force) {
        if (staticImagePreview) return;
        long now = SystemClock.uptimeMillis();
        if (force || now - lastRenderMs >= 45) renderNow();
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        if (staticImagePreview) {
            invalidate();
        } else {
            renderNow();
        }
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (bitmap == null) return;

        if (!staticImagePreview) {
            canvas.drawBitmap(bitmap, null,
                    new android.graphics.Rect(0, 0, getWidth(), getHeight()), paint);
            return;
        }

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
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                lastX = event.getX();
                lastY = event.getY();
                return true;
            case MotionEvent.ACTION_MOVE:
                if (!scaleDetector.isInProgress()) {
                    float dx = event.getX() - lastX;
                    float dy = event.getY() - lastY;
                    yaw += dx * 0.008f;
                    pitch += dy * 0.008f;
                    pitch = Math.max(-1.55f, Math.min(1.55f, pitch));
                    lastX = event.getX();
                    lastY = event.getY();
                    renderThrottled(false);
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                renderThrottled(true);
                return true;
            default:
                return true;
        }
    }
}
