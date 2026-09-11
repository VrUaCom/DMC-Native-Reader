package com.dmcrengine.nativeviewer;

/**
 * Passive Android view of Spider Black Widow state.
 *
 * Business decisions are evaluated in native C++ Black Widow. This class only
 * decodes a stable bitmask for Android widget presentation; it must not infer
 * state from diagnostics, file formats, or ResourceCapabilities combinations.
 */
public final class BlackWidowState {
    private static final long CAN_RENDER = 1L << 0;
    private static final long CAN_WIREFRAME = 1L << 1;
    private static final long CAN_INSPECT = 1L << 2;
    private static final long CAN_SHOW_HIERARCHY = 1L << 3;
    private static final long HAS_SKINNING = 1L << 4;
    private static final long HAS_SKIN_WEIGHTS = 1L << 5;
    private static final long HAS_TEXTURE_BINDINGS = 1L << 6;
    private static final long CAN_PREVIEW_IMAGE = 1L << 7;
    private static final long HAS_CHILD_RESOURCES = 1L << 8;
    private static final long IS_TEXT = 1L << 9;
    private static final long IS_CONTAINER = 1L << 10;
    private static final long HAS_COLLISION = 1L << 11;
    private static final long HAS_ADJACENCY = 1L << 12;
    private static final long HAS_TRANSFORM_SELECTORS = 1L << 13;
    private static final long CAN_SHOW_UV = 1L << 14;
    private static final long CHILD_BROWSER_MODE = 1L << 15;
    private static final long TEXTURE_COMPANION_ATTACHABLE = 1L << 16;
    private static final long TEXTURE_COMPANION_ATTACHED = 1L << 17;
    private static final long UV_MAP_VIEW = 1L << 18;
    private static final long CAN_INSPECT_UV = 1L << 19;
    private static final long CAN_INSPECT_MESHES = 1L << 20;
    private static final long CAN_INSPECT_HIERARCHY = 1L << 21;
    private static final long CAN_EXPORT_PNG = 1L << 22;
    private static final long CAN_ADD_MODEL_PART = 1L << 23;
    private static final long CAN_STAGE_COMPANION = 1L << 24;

    public final long bits;
    public final boolean canRender;
    public final boolean canWireframe;
    public final boolean canInspect;
    public final boolean canShowHierarchy;
    public final boolean hasSkinning;
    public final boolean hasSkinWeights;
    public final boolean hasTextureBindings;
    public final boolean canPreviewImage;
    public final boolean hasChildResources;
    public final boolean isText;
    public final boolean isContainer;
    public final boolean hasCollision;
    public final boolean hasAdjacency;
    public final boolean hasTransformSelectors;
    public final boolean canShowUv;
    public final boolean childBrowserMode;
    public final boolean canAttachTextureCompanion;
    public final boolean textureCompanionAttached;
    public final boolean uvMapView;
    public final boolean canInspectUv;
    public final boolean canInspectMeshes;
    public final boolean canInspectHierarchy;
    public final boolean canExportPng;
    public final boolean canAddModelPart;
    public final boolean canStageCompanion;

    private BlackWidowState(long bits) {
        this.bits = bits;
        canRender = has(bits, CAN_RENDER);
        canWireframe = has(bits, CAN_WIREFRAME);
        canInspect = has(bits, CAN_INSPECT);
        canShowHierarchy = has(bits, CAN_SHOW_HIERARCHY);
        hasSkinning = has(bits, HAS_SKINNING);
        hasSkinWeights = has(bits, HAS_SKIN_WEIGHTS);
        hasTextureBindings = has(bits, HAS_TEXTURE_BINDINGS);
        canPreviewImage = has(bits, CAN_PREVIEW_IMAGE);
        hasChildResources = has(bits, HAS_CHILD_RESOURCES);
        isText = has(bits, IS_TEXT);
        isContainer = has(bits, IS_CONTAINER);
        hasCollision = has(bits, HAS_COLLISION);
        hasAdjacency = has(bits, HAS_ADJACENCY);
        hasTransformSelectors = has(bits, HAS_TRANSFORM_SELECTORS);
        canShowUv = has(bits, CAN_SHOW_UV);
        childBrowserMode = has(bits, CHILD_BROWSER_MODE);
        canAttachTextureCompanion = has(bits, TEXTURE_COMPANION_ATTACHABLE);
        textureCompanionAttached = has(bits, TEXTURE_COMPANION_ATTACHED);
        uvMapView = has(bits, UV_MAP_VIEW);
        canInspectUv = has(bits, CAN_INSPECT_UV);
        canInspectMeshes = has(bits, CAN_INSPECT_MESHES);
        canInspectHierarchy = has(bits, CAN_INSPECT_HIERARCHY);
        canExportPng = has(bits, CAN_EXPORT_PNG);
        canAddModelPart = has(bits, CAN_ADD_MODEL_PART);
        canStageCompanion = has(bits, CAN_STAGE_COMPANION);
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
