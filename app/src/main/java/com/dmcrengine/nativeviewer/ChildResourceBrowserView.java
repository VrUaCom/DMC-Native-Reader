package com.dmcrengine.nativeviewer;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Format-agnostic gallery for ResourceCapability.ChildResources.
 * Native child projections provide titles and optional image previews; the UI
 * does not branch on PTX/DDS/PAC/PNST/etc.
 */
public final class ChildResourceBrowserView extends ScrollView {
    public interface Listener {
        void onChildSelected(int index, String title);
    }

    private static final int COLUMNS = 2;
    private final LinearLayout content;
    private Listener listener;
    private long session;

    public ChildResourceBrowserView(Context context) {
        super(context);
        setFillViewport(true);
        setBackgroundColor(0xff121216);

        content = new LinearLayout(context);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(10), dp(10), dp(10), dp(16));
        addView(content, new ScrollView.LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
    }

    public void setListener(Listener listener) {
        this.listener = listener;
    }

    public void setSession(long newSession) {
        if (session == newSession) return;
        session = newSession;
        rebuild();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void rebuild() {
        content.removeAllViews();
        if (session == 0) return;

        final int count = Math.max(0, NativeBridge.childResourceCount(session));
        TextView summary = new TextView(getContext());
        summary.setText(count + (count == 1 ? " resource" : " resources"));
        summary.setTextColor(0xffd7d7dc);
        summary.setTextSize(14f);
        summary.setPadding(dp(4), dp(2), dp(4), dp(10));
        content.addView(summary, new LinearLayout.LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));

        for (int rowStart = 0; rowStart < count; rowStart += COLUMNS) {
            LinearLayout row = new LinearLayout(getContext());
            row.setOrientation(LinearLayout.HORIZONTAL);
            content.addView(row, new LinearLayout.LayoutParams(
                    LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));

            for (int column = 0; column < COLUMNS; ++column) {
                final int index = rowStart + column;
                if (index >= count) {
                    View spacer = new View(getContext());
                    row.addView(spacer, tileLayoutParams());
                    continue;
                }
                row.addView(makeTile(index), tileLayoutParams());
            }
        }
    }

    private LinearLayout.LayoutParams tileLayoutParams() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                0, dp(164), 1f);
        params.setMargins(dp(4), dp(4), dp(4), dp(4));
        return params;
    }

    private View makeTile(int index) {
        String title = NativeBridge.childResourceTitle(session, index);
        if (title == null || title.isEmpty()) title = "Resource " + index;
        final String selectedTitle = title;

        FrameLayout tile = new FrameLayout(getContext());
        tile.setBackgroundColor(0xff24242b);
        tile.setClickable(true);
        tile.setFocusable(true);
        tile.setContentDescription(selectedTitle);
        tile.setOnClickListener(v -> {
            if (listener != null) listener.onChildSelected(index, selectedTitle);
        });

        // Preview is the primary representation. The child title is rendered
        // only as a visible fallback when no safe bitmap can be materialized.
        if (!tryAddPreview(tile, index)) {
            TextView fallback = new TextView(getContext());
            fallback.setText(selectedTitle);
            fallback.setTextColor(Color.WHITE);
            fallback.setTextSize(17f);
            fallback.setGravity(Gravity.CENTER);
            fallback.setPadding(dp(8), dp(8), dp(8), dp(8));
            tile.addView(fallback, new FrameLayout.LayoutParams(
                    LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
        }
        return tile;
    }

    private boolean tryAddPreview(FrameLayout tile, int index) {
        if (!NativeBridge.childResourcePreviewAvailable(session, index)) return false;
        final int width = NativeBridge.childResourcePreviewWidth(session, index);
        final int height = NativeBridge.childResourcePreviewHeight(session, index);
        if (width <= 0 || height <= 0) return false;
        final long expected = (long) width * (long) height;
        if (expected <= 0L || expected > Integer.MAX_VALUE) return false;

        try {
            final int[] pixels = NativeBridge.childResourcePreview(session, index);
            if (pixels == null || pixels.length != (int) expected) return false;
            Bitmap bitmap = Bitmap.createBitmap(
                    pixels, width, height, Bitmap.Config.ARGB_8888);
            ImageView image = new ImageView(getContext());
            image.setImageBitmap(bitmap);
            image.setScaleType(ImageView.ScaleType.FIT_CENTER);
            image.setBackgroundColor(0xff101014);
            tile.addView(image, new FrameLayout.LayoutParams(
                    LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
            return true;
        } catch (OutOfMemoryError | RuntimeException ignored) {
            return false;
        }
    }
}
