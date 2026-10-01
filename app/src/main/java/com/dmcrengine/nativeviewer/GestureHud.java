package com.dmcrengine.nativeviewer;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.os.SystemClock;

import java.util.Locale;

/**
 * Camera read-outs drawn over the render view while a camera gesture runs:
 * the pinch shows zoom and the equivalent lens, the dolly (one finger holds,
 * the other slides up / down) shows the distance to the target, or how far the
 * camera has moved when nothing is bound.
 *
 * The fly camera's two joysticks use the same rings (base where the finger
 * came down, a knob under the finger); the turn gesture (one finger holds, two
 * others twist round it) shows the ring under the holding finger and the angle.
 *
 * Numbers are light white digits with a soft dark outline, captions small
 * letter-spaced grey; everything is a little transparent and fades in and out.
 */
final class GestureHud {
    /** DMC3 model units are read as centimetres (a character is ~175 tall). */
    private static final float UNITS_PER_METER = 100.0f;
    /** 35 mm full frame: the diagonal is 43.27 mm. */
    private static final float FULL_FRAME_HALF_DIAGONAL_MM = 21.635f;
    /** Half field of view (radians) of the short screen side at zoom 1, as in the renderer. */
    private static final float BASE_HALF_FOV = 0.5f;
    static final float ZOOM_MIN = 0.15f, ZOOM_MAX = 8.0f;

    private static final long FADE_IN_MS = 130, FADE_OUT_MS = 420, HOLD_MS = 380;

    private enum Mode { NONE, ZOOM, DOLLY, TURN }

    private final float density;
    private final float scaledDensity;
    private final Paint digits = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Paint digitsOutline = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Paint unit = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Paint unitOutline = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Paint caption = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Paint captionOutline = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint lineShadow = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();

    private Mode mode = Mode.NONE;
    private boolean active;
    private long changedMs;
    private long lastFrameMs;
    private float alpha;

    // ZOOM
    private float zoom = 1.0f;
    private float zoomX, zoomY;
    private int viewWidth = 1, viewHeight = 1;

    // DOLLY
    private float holdX, holdY, dragX, dragY;
    private float distanceCm;      // to the target (bound) or the camera shift (unbound)
    private boolean bound;
    private float dollyFraction;   // ruler phase
    private float rulerTravel;     // accumulated ruler scroll in px

    // TURN (one finger holds, two twist): ring at the holding finger, degrees
    private float turnDegrees;

    // Joysticks of the fly camera: 0 left (move), 1 right (look).
    private static final class Stick {
        boolean active;
        float baseX, baseY, knobX, knobY;
        float alpha;
    }
    private final Stick[] sticks = {new Stick(), new Stick()};
    private long sticksFrameMs;

    /** Travel of a joystick knob: the outer ring of the dolly's rings. */
    static float stickRadius(float density) {
        return 49f * density;
    }

    GestureHud(float density, float scaledDensity) {
        this.density = density;
        this.scaledDensity = scaledDensity;
        final Typeface light = Typeface.create("sans-serif-light", Typeface.NORMAL);
        final Typeface medium = Typeface.create("sans-serif-medium", Typeface.NORMAL);
        final Typeface regular = Typeface.create("sans-serif", Typeface.NORMAL);

        style(digits, light, 44f, false);
        style(digitsOutline, light, 44f, true);
        style(unit, regular, 19f, false);
        style(unitOutline, regular, 19f, true);
        style(caption, medium, 10.5f, false);
        style(captionOutline, medium, 10.5f, true);
        for (Paint p : new Paint[]{digits, digitsOutline, unit, unitOutline}) {
            p.setFontFeatureSettings("tnum");  // digits keep their width while they change
        }
        for (Paint p : new Paint[]{caption, captionOutline}) p.setLetterSpacing(0.18f);
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeCap(Paint.Cap.ROUND);
        lineShadow.setStyle(Paint.Style.STROKE);
        lineShadow.setStrokeCap(Paint.Cap.ROUND);
        fill.setStyle(Paint.Style.FILL);
    }

    private void style(Paint paint, Typeface face, float sp, boolean outline) {
        paint.setTypeface(face);
        paint.setTextSize(sp * scaledDensity);
        paint.setTextAlign(Paint.Align.CENTER);
        if (outline) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeJoin(Paint.Join.ROUND);
            paint.setStrokeWidth(sp * scaledDensity * (sp > 30f ? 0.085f : 0.15f));
        }
    }

    // ---- state

    void showZoom(float zoomValue, float focusX, float focusY, int width, int height) {
        begin(Mode.ZOOM);
        zoom = zoomValue;
        zoomX = focusX;
        zoomY = focusY;
        viewWidth = Math.max(1, width);
        viewHeight = Math.max(1, height);
    }

    /**
     * @param distanceUnits distance to the target when `bound`, else the signed
     *                      camera shift, in model units
     */
    void showDolly(float distanceUnits, boolean isBound, float dolly,
                   float holdPx, float holdPy, float dragPx, float dragPy, int width, int height) {
        begin(Mode.DOLLY);
        distanceCm = distanceUnits;
        bound = isBound;
        rulerTravel += (dolly - dollyFraction) * 420f * density;
        dollyFraction = dolly;
        holdX = holdPx;
        holdY = holdPy;
        dragX = dragPx;
        dragY = dragPy;
        viewWidth = Math.max(1, width);
        viewHeight = Math.max(1, height);
    }

    void showTurn(float degrees, float holdPx, float holdPy, int width, int height) {
        begin(Mode.TURN);
        turnDegrees = degrees;
        holdX = holdPx;
        holdY = holdPy;
        viewWidth = Math.max(1, width);
        viewHeight = Math.max(1, height);
    }

    /** Joystick `index` (0 left, 1 right): base ring at (bx, by), knob at (kx, ky). */
    void showStick(int index, float bx, float by, float kx, float ky) {
        final Stick stick = sticks[index];
        if (!stick.active && stick.alpha <= 0f) sticksFrameMs = SystemClock.uptimeMillis();
        stick.active = true;
        stick.baseX = bx;
        stick.baseY = by;
        stick.knobX = kx;
        stick.knobY = ky;
    }

    /** The finger left joystick `index`: it fades out. */
    void releaseStick(int index) {
        sticks[index].active = false;
    }

    /** Fades the read-out out (after the fingers lift). */
    void release() {
        if (!active) return;
        active = false;
        changedMs = SystemClock.uptimeMillis();
    }

    void reset() {
        mode = Mode.NONE;
        active = false;
        alpha = 0f;
    }

    boolean needsFrame() {
        return mode != Mode.NONE || sticks[0].active || sticks[1].active
                || sticks[0].alpha > 0f || sticks[1].alpha > 0f;
    }

    private void begin(Mode next) {
        if (mode != next) {
            mode = next;
            alpha = 0f;
            rulerTravel = 0f;
        }
        if (!active) {
            changedMs = SystemClock.uptimeMillis();
            lastFrameMs = changedMs;
        }
        active = true;
    }

    // ---- lens

    /** 35 mm equivalent focal length (diagonal) of the renderer's camera at `zoom`. */
    static float lensMillimetres(float zoom, int width, int height) {
        final float shortSide = Math.max(1, Math.min(width, height));
        final float diagonal = (float) Math.hypot(width, height);
        // tan(half diagonal FOV) = (diagonal / shortSide) * tan(0.5) / zoom.
        final float tanHalf = (diagonal / shortSide) * (float) Math.tan(BASE_HALF_FOV) / zoom;
        return FULL_FRAME_HALF_DIAGONAL_MM / tanHalf;
    }

    // ---- drawing

    void draw(Canvas canvas) {
        drawSticks(canvas);
        if (mode == Mode.NONE) return;
        final long now = SystemClock.uptimeMillis();
        final float dt = Math.min(64L, Math.max(0L, now - lastFrameMs)) ;
        lastFrameMs = now;
        if (active) {
            alpha = Math.min(1f, alpha + dt / FADE_IN_MS);
        } else if (now - changedMs > HOLD_MS) {
            alpha = Math.max(0f, alpha - dt / FADE_OUT_MS);
            if (alpha <= 0f) {
                mode = Mode.NONE;
                return;
            }
        }
        if (alpha <= 0.002f) return;
        final float ease = alpha * alpha * (3f - 2f * alpha);
        if (mode == Mode.ZOOM) {
            drawZoom(canvas, ease);
        } else if (mode == Mode.TURN) {
            drawTurn(canvas, ease);
        } else {
            drawDolly(canvas, ease);
        }
    }

    private static int withAlpha(int argb, float a) {
        final int base = (argb >>> 24) & 0xff;
        return ((int) Math.max(0, Math.min(255, base * a)) << 24) | (argb & 0x00ffffff);
    }

    private static final int WHITE = 0xe8ffffff;      // slightly see-through
    private static final int GREY = 0xd2c3c7d1;
    private static final int GREY_DIM = 0x9ec3c7d1;
    private static final int OUTLINE = 0x8c05070c;    // dark, translucent

    private void text(Canvas canvas, String value, float x, float y, Paint fillPaint,
                      Paint outlinePaint, int color, float ease) {
        outlinePaint.setColor(withAlpha(OUTLINE, ease));
        canvas.drawText(value, x, y, outlinePaint);
        fillPaint.setColor(withAlpha(color, ease));
        canvas.drawText(value, x, y, fillPaint);
    }

    /** "ZOOM" caption over a big number with a small unit after it, centred on cx. */
    private void readout(Canvas canvas, float cx, float baseline, String captionText,
                         String number, String unitText, float ease) {
        final float gap = 4f * density;
        final float numberWidth = digits.measureText(number);
        final float unitWidth = unitText.isEmpty() ? 0f : unit.measureText(unitText);
        final float total = numberWidth + (unitText.isEmpty() ? 0f : gap + unitWidth);
        final float left = cx - total * 0.5f;
        digits.setTextAlign(Paint.Align.LEFT);
        digitsOutline.setTextAlign(Paint.Align.LEFT);
        unit.setTextAlign(Paint.Align.LEFT);
        unitOutline.setTextAlign(Paint.Align.LEFT);
        text(canvas, number, left, baseline, digits, digitsOutline, WHITE, ease);
        if (!unitText.isEmpty()) {
            text(canvas, unitText, left + numberWidth + gap, baseline, unit, unitOutline, GREY, ease);
        }
        digits.setTextAlign(Paint.Align.CENTER);
        digitsOutline.setTextAlign(Paint.Align.CENTER);
        unit.setTextAlign(Paint.Align.CENTER);
        unitOutline.setTextAlign(Paint.Align.CENTER);
        final float capY = baseline - digits.getTextSize() * 0.86f - 7f * density;
        text(canvas, captionText, cx, capY, caption, captionOutline, GREY, ease);
    }

    private void drawZoom(Canvas canvas, float ease) {
        final float panelHalf = 76f * density;
        final float cx = Math.max(panelHalf, Math.min(viewWidth - panelHalf, zoomX));
        // Above the fingers; below them when they are near the top edge.
        float baseline = zoomY - 86f * density;
        final float minBaseline = 92f * density;
        if (baseline < minBaseline) baseline = Math.min(zoomY + 126f * density, viewHeight - 70f * density);

        final String zoomText = zoom >= 10f ? String.format(Locale.US, "%.1f", zoom)
                : String.format(Locale.US, "%.2f", zoom);
        readout(canvas, cx, baseline, "ZOOM", "×" + zoomText, "", ease);

        // Lens, in the same light type, smaller.
        final float mm = lensMillimetres(zoom, viewWidth, viewHeight);
        final String lens = mm >= 20f ? String.format(Locale.US, "%.0f", mm)
                : String.format(Locale.US, "%.1f", mm);
        final float lensBase = baseline + 27f * density;
        final Paint small = unit;
        final float oldSize = small.getTextSize();
        small.setTextSize(oldSize * 1.25f);
        unitOutline.setTextSize(oldSize * 1.25f);
        final String full = lens + " mm";
        text(canvas, full, cx, lensBase, small, unitOutline, GREY, ease);
        small.setTextSize(oldSize);
        unitOutline.setTextSize(oldSize);
        text(canvas, "35 MM EQUIVALENT", cx, lensBase + 15f * density, caption, captionOutline, GREY_DIM, ease);

        // Zoom range on a log track, with the 1x mark.
        final float trackHalf = 54f * density;
        final float y = lensBase + 32f * density;
        final float logMin = (float) Math.log(ZOOM_MIN), logMax = (float) Math.log(ZOOM_MAX);
        final float t = ((float) Math.log(Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, zoom))) - logMin) / (logMax - logMin);
        final float one = ((float) 0 - logMin) / (logMax - logMin);
        strokeSoft(canvas, cx - trackHalf, y, cx + trackHalf, y, 1.6f, 0x55ffffff, ease);
        strokeSoft(canvas, cx - trackHalf, y, cx - trackHalf + 2f * trackHalf * t, y, 1.6f, 0xd0ffffff, ease);
        strokeSoft(canvas, cx - trackHalf + 2f * trackHalf * one, y - 4f * density,
                cx - trackHalf + 2f * trackHalf * one, y + 4f * density, 1.2f, 0x88ffffff, ease);
        final float dotX = cx - trackHalf + 2f * trackHalf * t;
        fill.setColor(withAlpha(OUTLINE, ease));
        canvas.drawCircle(dotX, y, 5.6f * density, fill);
        fill.setColor(withAlpha(WHITE, ease));
        canvas.drawCircle(dotX, y, 4f * density, fill);
    }

    private void strokeSoft(Canvas canvas, float x0, float y0, float x1, float y1, float widthDp,
                            int color, float ease) {
        lineShadow.setStrokeWidth((widthDp + 2.2f) * density);
        lineShadow.setColor(withAlpha(OUTLINE, ease * 0.75f));
        canvas.drawLine(x0, y0, x1, y1, lineShadow);
        line.setStrokeWidth(widthDp * density);
        line.setColor(withAlpha(color, ease));
        canvas.drawLine(x0, y0, x1, y1, line);
    }

    private void drawDolly(Canvas canvas, float ease) {
        // Ring around the holding finger.
        final float r = 38f * density;
        holdRings(canvas, holdX, holdY, ease);

        // Read-out above the ring (below it near the top edge).
        final float meters = distanceCm / UNITS_PER_METER;
        final float abs = Math.abs(meters);
        final String number;
        final String unitText;
        if (abs >= 1f) {
            number = String.format(Locale.US, abs >= 100f ? "%.0f" : abs >= 10f ? "%.1f" : "%.2f", abs);
            unitText = "m";
        } else {
            number = String.format(Locale.US, "%.0f", abs * 100f);
            unitText = "cm";
        }
        final String signed = bound ? number : (meters < -0.0005f ? "−" : (meters > 0.0005f ? "+" : "")) + number;
        final float panelHalf = 84f * density;
        final float cx = Math.max(panelHalf, Math.min(viewWidth - panelHalf, holdX));
        float baseline = holdY - r - 34f * density;
        if (baseline < 96f * density) baseline = holdY + r + 78f * density;
        readout(canvas, cx, baseline, bound ? "DISTANCE TO TARGET" : "CAMERA MOVED", signed, unitText, ease);

        // Ruler beside the moving finger, on the side that faces the centre.
        final float side = dragX > viewWidth * 0.5f ? -1f : 1f;
        final float rx = dragX + side * 52f * density;
        final float half = 92f * density;
        final float cy = Math.max(half + 10f * density, Math.min(viewHeight - half - 10f * density, dragY));
        strokeSoft(canvas, rx, cy - half, rx, cy + half, 1.2f, 0x50ffffff, ease);
        final float step = 13f * density;
        final float phase = ((rulerTravel % step) + step) % step;
        for (float y = cy - half + phase; y <= cy + half; y += step) {
            final float fade = 1f - Math.abs(y - cy) / half;
            final long index = Math.round((y - (cy - half) - phase) / step);
            final boolean major = ((index + (long) Math.floor(rulerTravel / step)) % 5 + 5) % 5 == 0;
            final float len = (major ? 10f : 5.5f) * density;
            strokeSoft(canvas, rx, y, rx + side * len, y, major ? 1.3f : 1f,
                    withAlpha(0xffffffff, 0.15f + 0.65f * fade), ease);
        }
        // Centre marker pointing at the ruler.
        path.reset();
        final float m = 5.5f * density;
        final float tip = rx - side * 11f * density;
        path.moveTo(tip, cy);
        path.lineTo(tip - side * m, cy - m * 0.75f);
        path.lineTo(tip - side * m, cy + m * 0.75f);
        path.close();
        lineShadow.setStrokeWidth(3.2f * density);
        lineShadow.setColor(withAlpha(OUTLINE, ease * 0.75f));
        lineShadow.setStrokeJoin(Paint.Join.ROUND);
        canvas.drawPath(path, lineShadow);
        fill.setColor(withAlpha(WHITE, ease));
        canvas.drawPath(path, fill);
    }

    /** The rings under a holding finger (dolly, turn, joystick base). */
    private void holdRings(Canvas canvas, float x, float y, float ease) {
        final float r = 38f * density;
        fill.setColor(withAlpha(0x14ffffff, ease));
        canvas.drawCircle(x, y, r, fill);
        ring(canvas, x, y, r, 1.5f, 0xa6ffffff, ease);
        ring(canvas, x, y, r + 11f * density, 1.0f, 0x40ffffff, ease);
    }

    private void drawTurn(Canvas canvas, float ease) {
        holdRings(canvas, holdX, holdY, ease);
        final float r = 38f * density;
        final String number = String.format(Locale.US, "%s%.0f", turnDegrees > 0.5f ? "+"
                : (turnDegrees < -0.5f ? "−" : ""), Math.abs(turnDegrees));
        final float panelHalf = 84f * density;
        final float cx = Math.max(panelHalf, Math.min(viewWidth - panelHalf, holdX));
        float baseline = holdY - r - 34f * density;
        if (baseline < 96f * density) baseline = holdY + r + 78f * density;
        readout(canvas, cx, baseline, "CAMERA TURN", number, "°", ease);
    }

    private void drawSticks(Canvas canvas) {
        final long now = SystemClock.uptimeMillis();
        final float dt = Math.min(64L, Math.max(0L, now - sticksFrameMs));
        sticksFrameMs = now;
        for (Stick stick : sticks) {
            stick.alpha = stick.active ? Math.min(1f, stick.alpha + dt / FADE_IN_MS)
                    : Math.max(0f, stick.alpha - dt / FADE_OUT_MS);
            if (stick.alpha <= 0.002f) continue;
            final float ease = stick.alpha * stick.alpha * (3f - 2f * stick.alpha);
            holdRings(canvas, stick.baseX, stick.baseY, ease);
            // Knob: a soft disc with a bright rim under the finger.
            final float k = 14f * density;
            fill.setColor(withAlpha(OUTLINE, ease * 0.8f));
            canvas.drawCircle(stick.knobX, stick.knobY, k + 1.5f * density, fill);
            fill.setColor(withAlpha(0x55ffffff, ease));
            canvas.drawCircle(stick.knobX, stick.knobY, k, fill);
            ring(canvas, stick.knobX, stick.knobY, k, 1.5f, 0xd0ffffff, ease);
        }
    }

    private void ring(Canvas canvas, float x, float y, float radius, float widthDp, int color, float ease) {
        lineShadow.setStrokeWidth((widthDp + 2f) * density);
        lineShadow.setColor(withAlpha(OUTLINE, ease * 0.7f));
        canvas.drawCircle(x, y, radius, lineShadow);
        line.setStrokeWidth(widthDp * density);
        line.setColor(withAlpha(color, ease));
        canvas.drawCircle(x, y, radius, line);
    }
}
