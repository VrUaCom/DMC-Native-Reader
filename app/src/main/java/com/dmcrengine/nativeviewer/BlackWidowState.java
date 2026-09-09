package com.dmcrengine.nativeviewer;

/**
 * Passive Android view of Spider Black Widow state.
 *
 * Business decisions are evaluated in native C++ Black Widow. This class only
 * decodes a stable bitmask for Android widget presentation; it must not infer
 * state from diagnostics, file formats, or combinations of ResourceCapabilities.
 */
public final class BlackWidowState {
    private static final long TEXTURE_COMPANION_ATTACHABLE = 1L << 0;
    private static final long TEXTURE_COMPANION_ATTACHED = 1L << 1;

    public final long bits;
    public final boolean canAttachTextureCompanion;
    public final boolean textureCompanionAttached;

    private BlackWidowState(long bits) {
        this.bits = bits;
        canAttachTextureCompanion = has(bits, TEXTURE_COMPANION_ATTACHABLE);
        textureCompanionAttached = has(bits, TEXTURE_COMPANION_ATTACHED);
    }

    public static BlackWidowState fromNative(long bits) {
        return new BlackWidowState(bits);
    }

    public static BlackWidowState empty() {
        return new BlackWidowState(0L);
    }

    private static boolean has(long bits, long flag) {
        return (bits & flag) != 0L;
    }
}
