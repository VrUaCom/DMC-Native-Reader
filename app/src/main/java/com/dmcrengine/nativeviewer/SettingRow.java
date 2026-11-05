package com.dmcrengine.nativeviewer;

/**
 * One row of the Settings window, written by the core
 * (spider/viewer_settings.h). The shell draws it, keeps its value under
 * {@link #key} with the row's storage type and hands the values back.
 */
public final class SettingRow {
    static final int CONTROL_CHOICE = 0;
    static final int CONTROL_SLIDER = 1;
    /** A piece only the platform draws (benchmark, collision kinds); {@link #key} names it. */
    static final int CONTROL_SLOT = 2;

    static final int STORAGE_BOOL = 0;
    static final int STORAGE_INT = 1;
    static final int STORAGE_FLOAT = 2;

    final String key;
    final String section;
    final String label;
    final int control;
    final int storage;
    final String[] choices;
    final float[] values;
    final float fallback;
    final String hint;
    final boolean renderNow;
    final boolean roomOnly;

    SettingRow(String key, String section, String label, int control, int storage, String[] choices,
               float[] values, float fallback, String hint, boolean renderNow, boolean roomOnly) {
        this.key = key;
        this.section = section;
        this.label = label;
        this.control = control;
        this.storage = storage;
        this.choices = choices == null ? new String[0] : choices;
        this.values = values == null ? new float[0] : values;
        this.fallback = fallback;
        this.hint = hint == null ? "" : hint;
        this.renderNow = renderNow;
        this.roomOnly = roomOnly;
    }

    /** The stored value as the core reads it (Bool: 1 or 0). */
    float read(android.content.SharedPreferences prefs) {
        switch (storage) {
            case STORAGE_BOOL: return prefs.getBoolean(key, fallback != 0.0f) ? 1.0f : 0.0f;
            case STORAGE_FLOAT: return prefs.getFloat(key, fallback);
            default: return prefs.getInt(key, Math.round(fallback));
        }
    }

    void write(android.content.SharedPreferences prefs, float value) {
        final android.content.SharedPreferences.Editor edit = prefs.edit();
        switch (storage) {
            case STORAGE_BOOL: edit.putBoolean(key, value != 0.0f); break;
            case STORAGE_FLOAT: edit.putFloat(key, value); break;
            default: edit.putInt(key, Math.round(value)); break;
        }
        edit.apply();
    }
}
