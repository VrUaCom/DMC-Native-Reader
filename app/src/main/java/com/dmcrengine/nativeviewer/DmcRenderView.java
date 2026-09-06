package com.dmcrengine.nativeviewer;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;

public final class DmcRenderView extends View {
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final ScaleGestureDetector scaleDetector;
    private Bitmap bitmap;
    private long session;
    private float yaw = 0.65f;
    private float pitch = -0.45f;
    private float zoom = 1.0f;
    private boolean wireframe;
    private boolean hierarchy;
    private float lastX;
    private float lastY;
    private long lastRenderMs;

    public DmcRenderView(Context context) {
        super(context);
        setBackgroundColor(0xff121216);
        scaleDetector = new ScaleGestureDetector(context,
                new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                    @Override public boolean onScale(ScaleGestureDetector detector) {
                        zoom *= detector.getScaleFactor();
                        zoom = Math.max(0.15f, Math.min(8.0f, zoom));
                        renderThrottled(false);
                        return true;
                    }
                });
    }

    public void setSession(long newSession) {
        session = newSession;
        hierarchy = false;
        // A structural/non-mesh session intentionally renders no bitmap. Clear
        // any previous frame before asking native code for a new one.
        bitmap = null;
        invalidate();
        if (session == 0) return;
        resetView();
    }

    public void resetView() {
        yaw = 0.65f;
        pitch = -0.45f;
        zoom = 1.0f;
        renderNow();
    }

    public void toggleWireframe() {
        wireframe = !wireframe;
        renderNow();
    }

    public boolean isWireframe() { return wireframe; }

    public void toggleHierarchy() {
        hierarchy = !hierarchy;
        renderNow();
    }

    public void setHierarchyVisible(boolean visible) {
        hierarchy = visible;
        renderNow();
    }

    public boolean isHierarchyVisible() { return hierarchy; }

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
        int rw = renderWidth();
        int rh = renderHeight();
        int[] pixels = NativeBridge.render(session, rw, rh, yaw, pitch, zoom,
                wireframe, hierarchy);
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
        long now = SystemClock.uptimeMillis();
        if (force || now - lastRenderMs >= 45) renderNow();
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        renderNow();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (bitmap == null) return;
        canvas.drawBitmap(bitmap, null,
                new android.graphics.Rect(0, 0, getWidth(), getHeight()), paint);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
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
