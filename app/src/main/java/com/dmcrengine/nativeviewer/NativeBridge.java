package com.dmcrengine.nativeviewer;

public final class NativeBridge {
    static {
        System.loadLibrary("dmcviewer");
    }

    private NativeBridge() {}

    public static native long open(int fd, String filename);
    public static native void close(long handle);
    public static native String info(long handle);

    // Architecture v2 generic resource-session APIs. UI code consumes these
    // projections without reparsing format bytes in Java.
    public static native long capabilities(long handle);
    public static native boolean hierarchyAvailable(long handle);
    public static native boolean imagePreviewAvailable(long handle);
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
    public static native boolean childResourcePreviewAvailable(long handle, int index);
    public static native int childResourcePreviewWidth(long handle, int index);
    public static native int childResourcePreviewHeight(long handle, int index);
    public static native int[] childResourcePreview(long handle, int index);
    public static native long openChild(long handle, int index);

    public static native int[] render(long handle, int width, int height,
                                      float yaw, float pitch, float zoom,
                                      int renderFlags);
}
