package com.dmcrengine.nativeviewer;

public final class NativeBridge {
    static {
        System.loadLibrary("dmcviewer");
    }

    private NativeBridge() {}

    public static native long open(int fd, String filename);
    public static native void close(long handle);
    public static native String info(long handle);

    /** Decoded text for a text-family resource, or null for geometry formats. */
    public static native String text(long handle);
    public static native int[] render(long handle, int width, int height,
                                      float yaw, float pitch, float zoom,
                                      boolean wireframe);
}
