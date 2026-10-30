package com.dmcrengine.nativeviewer;

/** Passive card of a held stage-config marker: its rows and buttons, as the core wrote them. */
public final class MarkerCard {
    public final String title;
    public final String[] names;
    public final String[] values;
    public final NativeMenu actions;

    public MarkerCard(String title, String[] names, String[] values, NativeMenu actions) {
        this.title = title == null ? "" : title;
        this.names = names == null ? new String[0] : names;
        this.values = values == null ? new String[0] : values;
        this.actions = actions;
    }
}
