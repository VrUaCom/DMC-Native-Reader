package com.dmcrengine.nativeviewer;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Format-agnostic, recycled gallery. Native sessions own entries and previews. */
public final class ChildResourceBrowserView extends GridView {
    public interface Listener {
        void onChildSelected(int index, String title);
    }

    private final GalleryAdapter adapter = new GalleryAdapter();
    private Listener listener;
    private long session;

    public ChildResourceBrowserView(Context context) {
        super(context);
        setNumColumns(2);
        setStretchMode(STRETCH_COLUMN_WIDTH);
        setHorizontalSpacing(dp(8));
        setVerticalSpacing(dp(8));
        setPadding(dp(14), dp(12), dp(14), dp(16));
        setBackgroundColor(0xff121216);
        setAdapter(adapter);
        setOnItemClickListener((parent, view, index, id) -> {
            if (listener != null) listener.onChildSelected(index, title(index));
        });
    }

    public void setListener(Listener listener) { this.listener = listener; }

    public void setSession(long newSession) {
        if (session == newSession) return;
        session = newSession;
        adapter.notifyDataSetChanged();
        setSelection(0);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private String title(int index) {
        String value = NativeBridge.childResourceTitle(session, index);
        return value == null || value.isEmpty() ? "Resource " + index : value;
    }

    private void clearImage(ImageView image) {
        Drawable drawable = image.getDrawable();
        image.setImageDrawable(null);
        if (drawable instanceof BitmapDrawable) {
            Bitmap old = ((BitmapDrawable) drawable).getBitmap();
            if (old != null && !old.isRecycled()) old.recycle();
        }
    }

    private final class Tile extends LinearLayout {
        final ImageView image;
        final TextView caption;
        Tile() {
            super(ChildResourceBrowserView.this.getContext());
            setOrientation(VERTICAL);
            setBackgroundColor(0xff24242b);
            setLayoutParams(new GridView.LayoutParams(LayoutParams.MATCH_PARENT, dp(192)));
            image = new ImageView(getContext());
            image.setScaleType(ImageView.ScaleType.FIT_CENTER);
            image.setBackgroundColor(0xff101014);
            addView(image, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f));
            caption = new TextView(getContext());
            caption.setTextColor(Color.WHITE);
            caption.setTextSize(12f);
            caption.setGravity(Gravity.CENTER);
            caption.setMaxLines(2);
            caption.setPadding(dp(4), dp(4), dp(4), dp(4));
            addView(caption, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, dp(40)));
        }
    }

    private final class GalleryAdapter extends BaseAdapter {
        @Override public int getCount() {
            return session == 0 ? 0 : Math.max(0, NativeBridge.childResourceCount(session));
        }
        @Override public Object getItem(int index) { return index; }
        @Override public long getItemId(int index) { return index; }
        @Override public View getView(int index, View recycled, ViewGroup parent) {
            Tile tile = recycled instanceof Tile ? (Tile) recycled : new Tile();
            clearImage(tile.image);
            final String label = title(index);
            tile.caption.setText(label);
            tile.setContentDescription(label);
            try {
                final int w = NativeBridge.childResourcePreviewWidth(session, index);
                final int h = NativeBridge.childResourcePreviewHeight(session, index);
                final long expected = (long) w * h;
                if (w > 0 && h > 0 && expected > 0L && expected <= Integer.MAX_VALUE) {
                    Bitmap preview = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
                    if (NativeBridge.childResourcePreview(session, index, preview)) {
                        tile.image.setImageBitmap(preview);
                    } else {
                        preview.recycle();
                    }
                }
            } catch (OutOfMemoryError | RuntimeException ignored) {
                // The caption remains usable if a preview cannot be allocated.
            }
            return tile;
        }
    }
}
