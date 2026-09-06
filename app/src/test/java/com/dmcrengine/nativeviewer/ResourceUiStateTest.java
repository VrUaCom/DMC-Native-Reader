package com.dmcrengine.nativeviewer;

public final class ResourceUiStateTest {
    private static final long INSPECTION = 1L << 0;
    private static final long GEOMETRY = 1L << 1;
    private static final long WIREFRAME = 1L << 2;
    private static final long NODE_HIERARCHY = 1L << 3;
    private static final long SKELETAL_SKINNING = 1L << 4;
    private static final long SKIN_WEIGHTS = 1L << 5;
    private static final long TEXTURE_BINDING = 1L << 6;

    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        final long modCapabilities = INSPECTION | GEOMETRY | WIREFRAME |
                NODE_HIERARCHY | SKELETAL_SKINNING | SKIN_WEIGHTS;
        final ResourceUiState mod = ResourceUiState.fromCapabilities(modCapabilities);
        require(mod.canInspect, "MOD inspection");
        require(mod.canRender, "MOD geometry");
        require(mod.canWireframe, "MOD wireframe");
        require(mod.canShowHierarchy, "MOD hierarchy");
        require(mod.hasSkinning, "MOD skeletal skinning");
        require(mod.hasSkinWeights, "MOD skin weights");
        require(!mod.hasTextureBindings, "MOD texture binding must remain gated");

        final long scmCapabilities = INSPECTION | GEOMETRY | WIREFRAME |
                NODE_HIERARCHY | TEXTURE_BINDING;
        final ResourceUiState scm = ResourceUiState.fromCapabilities(scmCapabilities);
        require(scm.canInspect, "SCM inspection");
        require(scm.canRender, "SCM geometry");
        require(scm.canWireframe, "SCM wireframe");
        require(scm.canShowHierarchy, "SCM hierarchy");
        require(!scm.hasSkinning, "SCM must not advertise skeletal skinning");
        require(!scm.hasSkinWeights, "SCM must not advertise skin weights");
        require(scm.hasTextureBindings, "SCM texture binding");

        final ResourceUiState empty = ResourceUiState.empty();
        require(!empty.canInspect, "empty inspection");
        require(!empty.canRender, "empty geometry");
        require(!empty.canWireframe, "empty wireframe");
        require(!empty.canShowHierarchy, "empty hierarchy");
    }
}
