package com.dmcrengine.nativeviewer;

/** Passive menu written by the core: a title, a line, and items with ids to send back. */
public final class NativeMenu {
    public final String title;
    public final String message;
    public final int[] ids;
    public final String[] labels;
    /** Glyphs of square shortcuts; empty strings for rows. */
    public final String[] icons;

    public NativeMenu(String title, String message, int[] ids, String[] labels, String[] icons) {
        this.title = title == null ? "" : title;
        this.message = message == null ? "" : message;
        this.ids = ids == null ? new int[0] : ids;
        this.labels = labels == null ? new String[0] : labels;
        this.icons = icons == null ? new String[0] : icons;
    }
}
