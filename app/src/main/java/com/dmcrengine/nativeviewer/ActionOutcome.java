package com.dmcrengine.nativeviewer;

/**
 * Passive result of a native Spider action (spider/stage_actions.h).
 *
 * The core decides the step and writes the message; the shell carries out
 * the one platform step it names and shows the message. Nothing here is
 * inferred from text.
 */
public final class ActionOutcome {
    public static final int STEP_NONE = 0;
    public static final int STEP_PICK_STAGE_CONFIG = 1;
    public static final int STEP_SHOW_MARKER_CARD = 2;
    public static final int STEP_START_SCRUB = 3;
    public static final int STEP_OPEN_SESSION = 4;

    public final boolean ok;
    public final int step;
    public final String message;
    public final int marker;
    /** STEP_OPEN_SESSION: the title of the opened session. */
    public final String title;
    /** STEP_OPEN_SESSION: the native handle of the opened session. */
    public final long session;

    public ActionOutcome(boolean ok, int step, String message, int marker, String title, long session) {
        this.ok = ok;
        this.step = step;
        this.message = message == null ? "" : message;
        this.marker = marker;
        this.title = title == null ? "" : title;
        this.session = session;
    }
}
