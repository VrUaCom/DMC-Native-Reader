package com.dmcrengine.nativeviewer;

/**
 * Format-agnostic Android policy derived only from native ResourceCapabilities.
 * Binary-format knowledge stays in the native module registry and adapters.
 */
public final class ResourceUiState {
    private static final long CAP_INSPECTION = 1L << 0;
    private static final long CAP_GEOMETRY = 1L << 1;
    private static final long CAP_WIREFRAME = 1L << 2;
    private static final long CAP_NODE_HIERARCHY = 1L << 3;
    private static final long CAP_SKELETAL_SKINNING = 1L << 4;
    private static final long CAP_SKIN_WEIGHTS = 1L << 5;
    private static final long CAP_TEXTURE_BINDING = 1L << 6;
    private static final long CAP_IMAGE_PREVIEW = 1L << 7;
    private static final long CAP_CHILD_RESOURCES = 1L << 8;
    private static final long CAP_TEXT = 1L << 9;
    private static final long CAP_CONTAINER = 1L << 10;
    private static final long CAP_COLLISION = 1L << 11;
    private static final long CAP_ADJACENCY = 1L << 12;
    private static final long CAP_TRANSFORM_SELECTORS = 1L << 13;
    private static final long CAP_UV_COORDINATES = 1L << 14;

    public final long capabilities;
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
    public final boolean hasUvCoordinates;

    private ResourceUiState(long capabilities) {
        this.capabilities = capabilities;
        canRender = has(capabilities, CAP_GEOMETRY);
        canWireframe = canRender && has(capabilities, CAP_WIREFRAME);
        canInspect = has(capabilities, CAP_INSPECTION);
        canShowHierarchy = has(capabilities, CAP_NODE_HIERARCHY);
        hasSkinning = has(capabilities, CAP_SKELETAL_SKINNING);
        hasSkinWeights = has(capabilities, CAP_SKIN_WEIGHTS);
        hasTextureBindings = has(capabilities, CAP_TEXTURE_BINDING);
        canPreviewImage = has(capabilities, CAP_IMAGE_PREVIEW);
        hasChildResources = has(capabilities, CAP_CHILD_RESOURCES);
        isText = has(capabilities, CAP_TEXT);
        isContainer = has(capabilities, CAP_CONTAINER);
        hasCollision = has(capabilities, CAP_COLLISION);
        hasAdjacency = has(capabilities, CAP_ADJACENCY);
        hasTransformSelectors = has(capabilities, CAP_TRANSFORM_SELECTORS);
        hasUvCoordinates = has(capabilities, CAP_UV_COORDINATES);
    }

    public static ResourceUiState fromCapabilities(long capabilities) {
        return new ResourceUiState(capabilities);
    }

    public static ResourceUiState empty() {
        return new ResourceUiState(0L);
    }

    private static boolean has(long capabilities, long bit) {
        return (capabilities & bit) != 0L;
    }
}
