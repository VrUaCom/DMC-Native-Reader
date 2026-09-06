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
    public static native String inspection(long handle);

    public static native int[] render(long handle, int width, int height,
                                      float yaw, float pitch, float zoom,
                                      int renderFlags);
}
