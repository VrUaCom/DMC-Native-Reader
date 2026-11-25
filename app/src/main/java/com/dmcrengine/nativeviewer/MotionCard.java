package com.dmcrengine.nativeviewer;

/**
 * One card of the animation strip, written by the core
 * (spider/motion_strip.h): a MOT of the session's library or a staged one,
 * the Script Play buttons it has and its text.
 */
public final class MotionCard {
    public final String name;
    /** >= 0: the session's motion library. */
    public final int libraryIndex;
    /** >= 0: the shell's staged MOT of that order. */
    public final int stagedIndex;
    /** Top-level nested PAC slot; -1 unknown, -2 staged. */
    public final int packSlot;
    public final boolean separatorBefore;
    public final String separatorDescription;
    public final String kind;
    public final String stem;
    public final String description;
    public final int[] scriptIndexes;
    public final String[] scriptLabels;
    public final String[] scriptDescriptions;

    public MotionCard(String name, int libraryIndex, int stagedIndex, int packSlot, boolean separatorBefore,
                      String separatorDescription, String kind, String stem, String description,
                      int[] scriptIndexes, String[] scriptLabels, String[] scriptDescriptions) {
        this.name = name;
        this.libraryIndex = libraryIndex;
        this.stagedIndex = stagedIndex;
        this.packSlot = packSlot;
        this.separatorBefore = separatorBefore;
        this.separatorDescription = separatorDescription;
        this.kind = kind;
        this.stem = stem;
        this.description = description;
        this.scriptIndexes = scriptIndexes == null ? new int[0] : scriptIndexes;
        this.scriptLabels = scriptLabels == null ? new String[0] : scriptLabels;
        this.scriptDescriptions = scriptDescriptions == null ? new String[0] : scriptDescriptions;
    }
}
