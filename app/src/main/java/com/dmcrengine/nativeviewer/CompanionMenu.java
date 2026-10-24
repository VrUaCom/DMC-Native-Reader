package com.dmcrengine.nativeviewer;

/**
 * Passive ⋮ menu decided by the core (spider/companion_menu.h): shortcuts and
 * rows with their labels and glyphs, and the ids the shell sends back. The
 * ids mirror the core's Companion enum, as BlackWidowState mirrors its bits.
 */
public final class CompanionMenu {
    public static final int SETTINGS = 100;
    public static final int GESTURES = 101;
    public static final int ROOM_TOGGLE = 102;
    public static final int NEXT_ROOM_SPOT = 103;
    public static final int OPEN_FILE = 1;
    public static final int TEXTURE_FORMAT = 2;
    public static final int SAVE_FILE = 3;
    public static final int ADD_PAC = 4;
    public static final int BROWSE_ASSEMBLED_PAC = 5;
    public static final int BROWSE_STAGE_PAC = 6;
    public static final int STAGE_EFFECTS = 7;
    public static final int ADD_MOD_PARTS = 8;
    public static final int COMPOSITE_PARTS = 9;
    public static final int PLACE_MOD_PART = 10;
    public static final int RESET_MOD_PLACEMENT = 11;
    public static final int ATTACH_PTX = 12;
    public static final int STAGE_MOTION = 13;
    public static final int STAGE_TEXTURE = 14;
    public static final int STAGE_PHYSICS = 15;
    public static final int STAGE_CLOTH = 16;
    public static final int STAGE_OTHER = 17;
    public static final int STAGE_CONFIG_LOAD = 18;
    public static final int STAGE_CONFIG_TOGGLE = 19;
    public static final int STAGE_CONFIG_REMOVE = 20;
    public static final int DIAGNOSTICS = 21;
    public static final int CUTSCENE_CAMERA = 22;
    public static final int CUTSCENE_SOUND = 23;
    public static final int PHOTO = 26;
    public static final int VIDEO = 27;

    public final NativeMenu shortcuts;
    public final NativeMenu rows;
    /** What can be done but does not fit what is on the screen: under a folded row. */
    public final NativeMenu more;
    public final int[] dimmed;

    public CompanionMenu(NativeMenu shortcuts, NativeMenu rows, NativeMenu more, int[] dimmed) {
        this.shortcuts = shortcuts;
        this.rows = rows;
        this.more = more;
        this.dimmed = dimmed == null ? new int[0] : dimmed;
    }

    public boolean isDimmed(int id) {
        for (int value : dimmed) if (value == id) return true;
        return false;
    }
}
