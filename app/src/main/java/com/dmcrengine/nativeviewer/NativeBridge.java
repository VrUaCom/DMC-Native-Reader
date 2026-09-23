package com.dmcrengine.nativeviewer;

import android.graphics.Bitmap;

public final class NativeBridge {
    static {
        System.loadLibrary("dmcviewer");
    }

    private NativeBridge() {}

    public static native long open(int fd, String filename);
    public static native long composeMods(long[] handles, String[] names);
    public static native void close(long handle);
    public static native String info(long handle);

    // Spider Black Widow is the single Android application/UI-state contract.
    // Java must not reconstruct policy from raw capabilities or diagnostics.
    public static native long blackWidowState(long handle);

    public static native int compositePartCount(long handle);
    public static native String compositePartName(long handle, int index);

    public static native int imagePreviewWidth(long handle);
    public static native int imagePreviewHeight(long handle);
    public static native boolean imagePreview(long handle, Bitmap target);
    public static native String inspection(long handle);
    public static final int INSPECT_UV = 1;
    public static final int INSPECT_MESHES = 2;
    public static final int INSPECT_HIERARCHY = 3;
    public static native String inspectionTopic(long handle, int topic);

    // Companion-resource orchestration. Java only supplies a file descriptor;
    // native Spider/framing/DDS modules validate PTX and bind decoded texture
    // slots. Composite scenes may consume one shared PTX bank transactionally;
    // explicit per-part attachment remains the deterministic fallback.
    public static native boolean attachPtx(long handle, int fd, String filename);
    public static native boolean attachPtxToPart(long handle, int partIndex,
                                                  int fd, String filename);
    public static native String textureAttachmentInfo(long handle);
    // Newline-separated reasons the current view is shown but not read
    // canonically (orange warning). Empty when everything is canonical.
    public static native String nonCanonicalNotes(long handle);

    // Generic nested-resource browser contract. Parent modules publish typed
    // children; Android does not know whether the parent is PTX/PAC/PNST/etc.
    public static native int childResourceCount(long handle);
    public static native String childResourceTitle(long handle, int index);
    public static native int childResourcePreviewWidth(long handle, int index);
    public static native int childResourcePreviewHeight(long handle, int index);
    public static native boolean childResourcePreview(long handle, int index, Bitmap target);
    public static native long openUvGallery(long handle);
    public static native long openChild(long handle, int index);

    // Android allocates/reuses the destination Bitmap. JNI writes native RGBA
    // pixels directly into it, avoiding an intermediate Java int[] frame.
    public static native boolean render(long handle, int width, int height,
                                        float yaw, float pitch, float zoom,
                                        int renderFlags, Bitmap target);

    // Read-only PAC assembly: MODs in model space, slot-adjacent PTX, MOT library.
    public static native long assemblePac(long handle, String archiveName);
    // handles[0] = character PAC, the rest are added (plwp_*.pac weapons hang
    // from the body joint the game records for them).
    public static native long assemblePacs(long[] handles, String[] names);
    public static native int motionLibraryCount(long handle);
    public static native String motionLibraryName(long handle, int index);

    // MOT playback. Frames are MOT timeline units (60 per second in DMC3).
    // load* return a human-readable report; hasMotion tells whether it bound.
    public static native String loadLibraryMotion(long handle, int index);
    public static native String loadMotion(long handle, int fd, String filename);
    public static native boolean hasMotion(long handle);
    public static native float motionEndFrame(long handle);
    public static native float motionLoopStartFrame(long handle);
    public static native boolean setMotionFrame(long handle, float frame);
    public static native void clearMotion(long handle);
}
