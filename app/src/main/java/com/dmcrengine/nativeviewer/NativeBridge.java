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

    // Native-authoritative read-only projection for Android presentation.
    // Java displays this state but does not reconstruct placement/binding semantics.
    public static native String compositePartState(long handle, int index);

    public static native int compositePartNodeCount(long handle, int partIndex);
    public static native String compositePartNodeName(long handle, int partIndex, int nodeIndex);
    public static native int compositePartDefaultAttachmentSelector(long handle, int partIndex);
    public static native String attachModPartToHostJoint(
            long handle, int hostPartIndex, int childPartIndex, int hostJointIndex);
    public static native String resetModPartPlacement(long handle, int childPartIndex);

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
}
