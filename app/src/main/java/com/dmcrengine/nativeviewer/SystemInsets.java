package com.dmcrengine.nativeviewer;

import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;

/**
 * Keeps content clear of the status bar, the navigation bar and any display
 * cutout.
 *
 * From Android 15 (targetSdk 35+) edge-to-edge is enforced: the window is laid
 * out behind the system bars whether the app asks for it or not, so an app that
 * ignores insets draws its top row under the clock and its bottom row under the
 * navigation buttons. Padding the root view by the inset amounts is the fix.
 */
final class SystemInsets {

    private SystemInsets() {}

    /** Pads {@code root} by the system bar and cutout insets as they change. */
    static void applyAsPadding(final View root) {
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override public WindowInsets onApplyWindowInsets(View view, WindowInsets insets) {
                final int left;
                final int top;
                final int right;
                final int bottom;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    final Insets bars = insets.getInsets(
                            WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                    left = bars.left;
                    top = bars.top;
                    right = bars.right;
                    bottom = bars.bottom;
                } else {
                    // Deprecated on newer releases but the only option on 26-29.
                    left = insets.getSystemWindowInsetLeft();
                    top = insets.getSystemWindowInsetTop();
                    right = insets.getSystemWindowInsetRight();
                    bottom = insets.getSystemWindowInsetBottom();
                }
                view.setPadding(left, top, right, bottom);
                return insets;
            }
        });
        root.requestApplyInsets();
    }
}
