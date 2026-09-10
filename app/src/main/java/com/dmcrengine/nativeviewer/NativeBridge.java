package com.dmcrengine.nativeviewer;

public final class NativeBridge {
    static {
        System.loadLibrary("dmcviewer");
    }

    private NativeBridge() {}

    public static native long open(int fd, String filename);
    public static native void close(long handle);
    public static native String info(long handle);

    // Spider Black Widow is the single Android application/UI-state contract.
    // Java must not reconstruct policy from raw capabilities or diagnostics.
    public static native long blackWidowState(long handle);

    public static native int imagePreviewWidth(long handle);
    public static native int imagePreviewHeight(long handle);
    public static native int[] imagePreview(long handle);
    public static native String inspection(long handle);

    // Companion-resource orchestration. Java only supplies a file descriptor;
    // native Spider/framing/DDS modules validate PTX and bind its decoded
    // texture slots to an already-open neutral RenderScene.
    public static native boolean attachPtx(long handle, int fd, String filename);
    public static native String textureAttachmentInfo(long handle);

    // Generic nested-resource browser contract. Parent modules publish typed
    // children; Android does not know whether the parent is PTX/PAC/PNST/etc.
    public static native int childResourceCount(long handle);
    public static native String childResourceTitle(long handle, int index);
    public static native int childResourcePreviewWidth(long handle, int index);
    public static native int childResourcePreviewHeight(long handle, int index);
    public static native int[] childResourcePreview(long handle, int index);
    public static native long openUvGallery(long handle);
    public static native long openChild(long handle, int index);

    public static native int[] render(long handle, int width, int height,
                                      float yaw, float pitch, float zoom,
                                      int renderFlags);
}
