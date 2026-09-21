package com.dmcrengine.nativeviewer;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextUtils;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.OutputStream;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int REQUEST_OPEN = 1001;
    private static final int REQUEST_ATTACH_PTX = 1002;
    private static final int REQUEST_EXPORT_SINGLE_PNG = 1003;
    private static final int REQUEST_EXPORT_GALLERY_TREE = 1004;
    private static final int REQUEST_ADD_MOD_PARTS = 1005;
    private static final int REQUEST_STAGE_MOTION = 1006;
    private static final int REQUEST_STAGE_TEXTURE = 1007;
    private static final int REQUEST_STAGE_PHYSICS = 1008;
    private static final int REQUEST_STAGE_CLOTH = 1009;
    private static final int REQUEST_STAGE_OTHER = 1010;

    private static final int MENU_OPEN = 1;
    private static final int MENU_ADD_MOD = 2;
    private static final int MENU_ATTACH_PTX = 3;
    private static final int MENU_ADD_MOTION = 4;
    private static final int MENU_ADD_TEXTURE = 5;
    private static final int MENU_ADD_PHYSICS = 6;
    private static final int MENU_ADD_CLOTH = 7;
    private static final int MENU_ADD_OTHER = 8;
    private static final int MENU_PLACE_MOD = 9;
    private static final int MENU_RESET_MOD_PLACEMENT = 10;

    private static final String ROLE_MOTION = "motion";
    private static final String ROLE_TEXTURE = "texture";
    private static final String ROLE_PHYSICS = "physics";
    private static final String ROLE_CLOTH = "cloth";
    private static final String ROLE_OTHER = "other";

    private static final int TOOL_SIZE_DP = 48;
    private static final int TOOL_GAP_DP = 4;
    private static final int UV_EXPORT_SIZE = 1024;

    private static final class NavigationEntry {
        final long session;
        final String title;

        NavigationEntry(long session, String title) {
            this.session = session;
            this.title = title;
        }
    }

    private static final class StagedAsset {
        final Uri uri;
        final String name;
        final String role;

        StagedAsset(Uri uri, String name, String role) {
            this.uri = uri;
            this.name = name;
            this.role = role;
        }
    }

    private DmcRenderView renderView;
    private ChildResourceBrowserView childBrowser;
    private TextView titleView;
    private Button parentButton;
    private Button moreButton;
    private Button resetButton;
    private Button wireButton;
    private Button hierarchyButton;
    private Button uvButton;
    private Button infoButton;
    private HorizontalScrollView motionScroll;
    private LinearLayout motionBar;

    private long session;
    private long pendingExportSession;
    private int pendingPtxPart = -1;
    private int selectedMotionIndex = -1;

    private final ArrayDeque<NavigationEntry> navigation = new ArrayDeque<>();
    private final ArrayList<Uri> modelPartUris = new ArrayList<>();
    private final ArrayList<Uri> modelPartPtxUris = new ArrayList<>();
    private final ArrayList<StagedAsset> stagedAssets = new ArrayList<>();
    private Uri sharedModelPtxUri;

    private BlackWidowState blackWidowState = BlackWidowState.empty();
    private String infoText = "";

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        handleIncomingIntent(getIntent());
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIncomingIntent(intent);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private Button makeSquareButton(String text, String description, float textSize) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextSize(textSize);
        button.setAllCaps(false);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setMinHeight(0);
        button.setMinimumHeight(0);
        button.setPadding(0, 0, 0, 0);
        button.setGravity(Gravity.CENTER);
        button.setContentDescription(description);
        return button;
    }

    private void addToolButton(LinearLayout bar, Button button) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP));
        params.setMarginStart(dp(TOOL_GAP_DP));
        params.setMarginEnd(dp(TOOL_GAP_DP));
        bar.addView(button, params);
    }

    private void setToolAvailable(Button button, boolean available) {
        button.setEnabled(available);
        button.setAlpha(available ? 1.0f : 0.35f);
    }

    private void syncToggleButton(Button button, boolean available, boolean active) {
        button.setEnabled(available);
        button.setActivated(available && active);
        button.setAlpha(!available ? 0.35f : (active ? 1.0f : 0.78f));
    }

    private void refreshBlackWidowState() {
        blackWidowState = session == 0
                ? BlackWidowState.empty()
                : BlackWidowState.fromNative(NativeBridge.blackWidowState(session));
    }

    private boolean canAttachPtx() {
        return session != 0 && blackWidowState.canAttachTextureCompanion;
    }

    private boolean hasAttachedPtx() {
        return canAttachPtx() && blackWidowState.textureCompanionAttached;
    }

    private boolean isRootScene() {
        return session != 0 && navigation.isEmpty();
    }

    private boolean hasModCompositionContext() {
        return isRootScene() && blackWidowState.canAddModelPart;
    }

    private boolean hasCompositePlacementContext() {
        return isRootScene() && NativeBridge.compositePartCount(session) > 1;
    }

    private void applyPrimaryPresentation() {
        final boolean childBrowserMode = session != 0 && blackWidowState.childBrowserMode;
        if (childBrowserMode) {
            renderView.setVisibility(View.GONE);
            childBrowser.setVisibility(View.VISIBLE);
            childBrowser.setSession(session);
        } else {
            childBrowser.setSession(0);
            childBrowser.setVisibility(View.GONE);
            renderView.setVisibility(View.VISIBLE);
        }
    }

    private void applyResourceUiState() {
        final boolean hasSession = session != 0;
        applyPrimaryPresentation();

        parentButton.setVisibility(View.VISIBLE);
        setToolAvailable(moreButton, true);

        if (hasSession && blackWidowState.canExportPng) {
            resetButton.setText("↓");
            resetButton.setContentDescription(
                    NativeBridge.childResourceCount(session) > 0
                            ? "Export all images as PNG"
                            : "Export image as PNG");
            setToolAvailable(resetButton, true);
        } else {
            resetButton.setText("🔄");
            resetButton.setContentDescription("Reset view");
            setToolAvailable(resetButton, hasSession && blackWidowState.canRender);
        }

        syncToggleButton(wireButton,
                hasSession && (blackWidowState.canWireframe || blackWidowState.canInspectMeshes),
                renderView.isWireframe());

        final boolean hierarchyAvailable = hasSession
                && blackWidowState.canShowHierarchy
                && !renderView.isUvLayoutVisible();
        renderView.setHierarchyAvailable(hierarchyAvailable);
        syncToggleButton(hierarchyButton,
                hierarchyAvailable || blackWidowState.canInspectHierarchy,
                renderView.isHierarchyVisible());

        syncToggleButton(uvButton,
                hasSession && (blackWidowState.canShowUv || blackWidowState.canInspectUv),
                renderView.isUvLayoutVisible());

        setToolAvailable(infoButton,
                hasSession ? blackWidowState.canInspect : !infoText.isEmpty());
        refreshMotionStrip();
    }

    private void applySystemBarInsets(LinearLayout root) {
        root.setOnApplyWindowInsetsListener((view, windowInsets) -> {
            int left;
            int top;
            int right;
            int bottom;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                Insets safe = windowInsets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                left = safe.left;
                top = safe.top;
                right = safe.right;
                bottom = safe.bottom;
            } else {
                left = windowInsets.getSystemWindowInsetLeft();
                top = windowInsets.getSystemWindowInsetTop();
                right = windowInsets.getSystemWindowInsetRight();
                bottom = windowInsets.getSystemWindowInsetBottom();
            }
            view.setPadding(left, top, right, bottom);
            return windowInsets;
        });
        root.requestApplyInsets();
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff0b0b0e);
        applySystemBarInsets(root);

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);

        parentButton = makeSquareButton("←", "Back", 28f);
        parentButton.setOnClickListener(v -> navigateBack());
        header.addView(parentButton, new LinearLayout.LayoutParams(
                dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP)));

        titleView = new TextView(this);
        titleView.setTextColor(Color.WHITE);
        titleView.setTextSize(15f);
        titleView.setSingleLine(true);
        titleView.setEllipsize(TextUtils.TruncateAt.END);
        titleView.setPadding(dp(12), dp(8), dp(10), dp(6));
        titleView.setText("DMC Native Reader");
        header.addView(titleView, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        moreButton = makeSquareButton("⋮", "Add or attach DMC resource", 28f);
        moreButton.setOnClickListener(this::showCompanionMenu);
        header.addView(moreButton, new LinearLayout.LayoutParams(
                dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP)));

        root.addView(header, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        FrameLayout viewport = new FrameLayout(this);
        renderView = new DmcRenderView(this);
        viewport.addView(renderView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        childBrowser = new ChildResourceBrowserView(this);
        childBrowser.setVisibility(View.GONE);
        childBrowser.setListener(this::openChildResource);
        viewport.addView(childBrowser, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        root.addView(viewport, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        motionScroll = new HorizontalScrollView(this);
        motionScroll.setHorizontalScrollBarEnabled(false);
        motionScroll.setFillViewport(false);
        motionScroll.setVisibility(View.GONE);
        motionBar = new LinearLayout(this);
        motionBar.setOrientation(LinearLayout.HORIZONTAL);
        motionBar.setGravity(Gravity.CENTER_VERTICAL);
        motionBar.setPadding(dp(8), dp(2), dp(8), dp(2));
        motionScroll.addView(motionBar, new HorizontalScrollView.LayoutParams(
                HorizontalScrollView.LayoutParams.WRAP_CONTENT,
                HorizontalScrollView.LayoutParams.WRAP_CONTENT));
        root.addView(motionScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                dp(TOOL_SIZE_DP + 8)));

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER);
        bar.setPadding(dp(8), dp(6), dp(8), dp(8));

        Button open = makeSquareButton("↑", "Open resource or combine multiple MOD files", 28f);
        open.setOnClickListener(v -> chooseFile());
        addToolButton(bar, open);

        resetButton = makeSquareButton("🔄", "Reset view", 20f);
        resetButton.setOnClickListener(v -> handleResetOrExport());
        addToolButton(bar, resetButton);

        wireButton = makeSquareButton("W", "Wireframe", 18f);
        wireButton.setOnClickListener(v -> {
            if (!blackWidowState.canWireframe) return;
            renderView.toggleWireframe();
            applyResourceUiState();
        });
        addToolButton(bar, wireButton);

        hierarchyButton = makeSquareButton("🦴", "Bones / hierarchy", 20f);
        hierarchyButton.setOnClickListener(v -> {
            if (!blackWidowState.canShowHierarchy) return;
            renderView.toggleHierarchy();
            applyResourceUiState();
        });
        addToolButton(bar, hierarchyButton);

        uvButton = makeSquareButton("UV", "UV layout", 14f);
        uvButton.setOnClickListener(v -> {
            if (!blackWidowState.canShowUv) return;
            final long gallery = NativeBridge.openUvGallery(session);
            if (gallery == 0) {
                Toast.makeText(this, "UV maps unavailable: incomplete bindings", Toast.LENGTH_LONG).show();
                return;
            }
            navigateToSession(gallery, titleView.getText() + " · UV");
        });
        addToolButton(bar, uvButton);
        bindInspectionHold(uvButton, NativeBridge.INSPECT_UV);
        bindInspectionHold(wireButton, NativeBridge.INSPECT_MESHES);
        bindInspectionHold(hierarchyButton, NativeBridge.INSPECT_HIERARCHY);

        infoButton = makeSquareButton("\u2139", "Resource information", 22f);
        infoButton.setOnClickListener(v -> showInfoDialog());
        addToolButton(bar, infoButton);

        root.addView(bar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        setContentView(root);
        showIdleStatus();
    }

    private void showCompanionMenu(View anchor) {
        PopupMenu menu = new PopupMenu(this, anchor);
        menu.getMenu().add(0, MENU_OPEN, 0, "Open / replace resource");
        if (hasModCompositionContext()) {
            menu.getMenu().add(0, MENU_ADD_MOD, 1, "Add .MOD part(s)");
        }
        if (hasCompositePlacementContext()) {
            menu.getMenu().add(0, MENU_PLACE_MOD, 2, "Place MOD part on host joint…");
            menu.getMenu().add(0, MENU_RESET_MOD_PLACEMENT, 3, "Reset MOD part placement…");
        }
        if (isRootScene() && canAttachPtx()) {
            menu.getMenu().add(0, MENU_ATTACH_PTX, 4, "Attach .PTX texture");
        }
        if (isRootScene() && blackWidowState.canStageCompanion) {
            menu.getMenu().add(0, MENU_ADD_MOTION, 5, "Add animation / motion…");
            menu.getMenu().add(0, MENU_ADD_TEXTURE, 6, "Add texture asset (.TM2 / .DDS / …)");
            menu.getMenu().add(0, MENU_ADD_PHYSICS, 7, "Add physics resource…");
            menu.getMenu().add(0, MENU_ADD_CLOTH, 8, "Add cloth resource…");
            menu.getMenu().add(0, MENU_ADD_OTHER, 9, "Add other companion…");
        }
        menu.setOnMenuItemClickListener(item -> {
            switch (item.getItemId()) {
                case MENU_OPEN:
                    chooseFile();
                    return true;
                case MENU_ADD_MOD:
                    chooseAdditionalMods();
                    return true;
                case MENU_PLACE_MOD:
                    showPlaceModPartDialog();
                    return true;
                case MENU_RESET_MOD_PLACEMENT:
                    showResetModPlacementDialog();
                    return true;
                case MENU_ATTACH_PTX:
                    choosePtxForCurrentSession();
                    return true;
                case MENU_ADD_MOTION:
                    chooseStagedAssets(REQUEST_STAGE_MOTION, true);
                    return true;
                case MENU_ADD_TEXTURE:
                    chooseStagedAssets(REQUEST_STAGE_TEXTURE, true);
                    return true;
                case MENU_ADD_PHYSICS:
                    chooseStagedAssets(REQUEST_STAGE_PHYSICS, true);
                    return true;
                case MENU_ADD_CLOTH:
                    chooseStagedAssets(REQUEST_STAGE_CLOTH, true);
                    return true;
                case MENU_ADD_OTHER:
                    chooseStagedAssets(REQUEST_STAGE_OTHER, true);
                    return true;
                default:
                    return false;
            }
        });
        menu.show();
    }

    private void refreshMotionStrip() {
        if (motionBar == null || motionScroll == null) return;
        motionBar.removeAllViews();
        ArrayList<StagedAsset> motions = motionAssets();
        if (motions.isEmpty() || !isRootScene()) {
            motionScroll.setVisibility(View.GONE);
            if (motions.isEmpty()) selectedMotionIndex = -1;
            return;
        }
        if (selectedMotionIndex < 0 || selectedMotionIndex >= motions.size()) {
            selectedMotionIndex = 0;
        }
        for (int index = 0; index < motions.size(); ++index) {
            final int motionIndex = index;
            StagedAsset asset = motions.get(index);
            Button button = makeSquareButton("", "Select animation " + asset.name, 11f);
            button.setText(motionCardLabel(asset.name));
            button.setActivated(index == selectedMotionIndex);
            button.setAlpha(index == selectedMotionIndex ? 1.0f : 0.72f);
            button.setOnClickListener(v -> selectMotion(motionIndex));
            addToolButton(motionBar, button);
        }
        motionScroll.setVisibility(View.VISIBLE);
    }

    private CharSequence motionCardLabel(String name) {
        String stem = withoutExtension(name).toUpperCase(Locale.ROOT);
        if (stem.length() > 7) stem = stem.substring(0, 7);
        String extension = "MOT";
        if (name != null) {
            int dot = name.lastIndexOf('.');
            if (dot >= 0 && dot + 1 < name.length()) {
                extension = name.substring(dot + 1).toUpperCase(Locale.ROOT);
                if (extension.length() > 4) extension = extension.substring(0, 4);
            }
        }
        SpannableStringBuilder text = new SpannableStringBuilder(extension + "\n" + stem);
        int split = extension.length() + 1;
        text.setSpan(new StyleSpan(Typeface.BOLD), 0, extension.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        text.setSpan(new RelativeSizeSpan(0.70f), split, text.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        return text;
    }

    private ArrayList<StagedAsset> motionAssets() {
        ArrayList<StagedAsset> result = new ArrayList<>();
        for (StagedAsset asset : stagedAssets) {
            if (ROLE_MOTION.equals(asset.role)) result.add(asset);
        }
        return result;
    }

    private void selectMotion(int index) {
        ArrayList<StagedAsset> motions = motionAssets();
        if (index < 0 || index >= motions.size()) return;
        selectedMotionIndex = index;
        refreshMotionStrip();
        Toast.makeText(this,
                motions.get(index).name + " selected · playback runtime is not promoted yet",
                Toast.LENGTH_LONG).show();
    }

    private void setInfo(String text) {
        infoText = text == null ? "" : text;
    }

    private void bindInspectionHold(Button button, int topic) {
        button.setOnLongClickListener(v -> {
            if (session == 0) return false;
            showInfoDialog(NativeBridge.inspectionTopic(session, topic));
            return true;
        });
    }

    private void showInfoDialog() {
        showInfoDialog(infoText);
    }

    private void showInfoDialog(String text) {
        TextView details = new TextView(this);
        details.setText(text == null || text.isEmpty() ? "No resource information available." : text);
        details.setTextColor(Color.WHITE);
        details.setTextSize(13f);
        details.setTypeface(Typeface.MONOSPACE);
        details.setTextIsSelectable(true);
        details.setPadding(dp(16), dp(12), dp(16), dp(20));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(0xff141418);
        scroll.addView(details, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        new AlertDialog.Builder(this)
                .setTitle(titleView.getText())
                .setView(scroll)
                .setPositiveButton("Close", null)
                .show();
    }

    private void chooseFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/vnd.dmc.scm",
                "application/vnd.dmc.mod",
                "application/vnd.dmc.ptx",
                "image/vnd-ms.dds",
                "application/octet-stream",
                "*/*"
        });
        startActivityForResult(intent, REQUEST_OPEN);
    }

    private void chooseAdditionalMods() {
        if (!hasModCompositionContext()) {
            Toast.makeText(this, "Open a MOD scene first", Toast.LENGTH_LONG).show();
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/vnd.dmc.mod",
                "application/octet-stream",
                "*/*"
        });
        startActivityForResult(intent, REQUEST_ADD_MOD_PARTS);
    }

    private String compositePartLabel(int partIndex) {
        String name = NativeBridge.compositePartName(session, partIndex);
        return name == null || name.isEmpty()
                ? "MOD part " + (partIndex + 1)
                : name;
    }

    private void showPlaceModPartDialog() {
        final int partCount = NativeBridge.compositePartCount(session);
        if (!isRootScene() || partCount <= 1) return;

        String[] children = new String[partCount - 1];
        for (int index = 1; index < partCount; ++index) {
            children[index - 1] = compositePartLabel(index);
        }

        new AlertDialog.Builder(this)
                .setTitle("Choose MOD part to place")
                .setItems(children, (dialog, which) ->
                        showHostJointDialog(which + 1))
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void showHostJointDialog(int childPartIndex) {
        final int hostPartIndex = 0;
        final int nodeCount =
                NativeBridge.compositePartNodeCount(session, hostPartIndex);
        if (nodeCount <= 0) {
            Toast.makeText(this,
                    "Primary host exposes no placement joints",
                    Toast.LENGTH_LONG).show();
            return;
        }

        String[] joints = new String[nodeCount];
        for (int index = 0; index < nodeCount; ++index) {
            String name = NativeBridge.compositePartNodeName(
                    session, hostPartIndex, index);
            joints[index] = index + " · "
                    + (name == null || name.isEmpty() ? "unnamed node" : name);
        }

        final int defaultSelector =
                NativeBridge.compositePartDefaultAttachmentSelector(
                        session, hostPartIndex);
        final int[] selected = {
                defaultSelector >= 0 && defaultSelector < nodeCount
                        ? defaultSelector : -1
        };
        final String hostName = compositePartLabel(hostPartIndex);
        final String childName = compositePartLabel(childPartIndex);

        new AlertDialog.Builder(this)
                .setTitle("Place " + childName + " on " + hostName)
                .setSingleChoiceItems(joints, selected[0],
                        (dialog, which) -> selected[0] = which)
                .setPositiveButton("Attach", (dialog, which) -> {
                    if (selected[0] < 0) {
                        Toast.makeText(this,
                                "Choose a host joint first",
                                Toast.LENGTH_LONG).show();
                        return;
                    }
                    String status = NativeBridge.attachModPartToHostJoint(
                            session, hostPartIndex, childPartIndex, selected[0]);
                    if ("applied".equals(status)) {
                        renderView.renderNow();
                        rebuildInfo(titleView.getText().toString());
                        applyResourceUiState();
                        Toast.makeText(this,
                                "Placement applied: joint " + selected[0],
                                Toast.LENGTH_LONG).show();
                    } else {
                        Toast.makeText(this,
                                "Placement rejected: " + status,
                                Toast.LENGTH_LONG).show();
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void showResetModPlacementDialog() {
        final int partCount = NativeBridge.compositePartCount(session);
        if (!isRootScene() || partCount <= 1) return;

        String[] children = new String[partCount - 1];
        for (int index = 1; index < partCount; ++index) {
            children[index - 1] = compositePartLabel(index);
        }

        new AlertDialog.Builder(this)
                .setTitle("Reset MOD part placement")
                .setItems(children, (dialog, which) -> {
                    final int childPartIndex = which + 1;
                    String status = NativeBridge.resetModPartPlacement(
                            session, childPartIndex);
                    if ("reset".equals(status)) {
                        renderView.renderNow();
                        rebuildInfo(titleView.getText().toString());
                        applyResourceUiState();
                        Toast.makeText(this,
                                "Part restored to source coordinates",
                                Toast.LENGTH_LONG).show();
                    } else {
                        Toast.makeText(this,
                                "Reset rejected: " + status,
                                Toast.LENGTH_LONG).show();
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void chooseStagedAssets(int requestCode, boolean allowMultiple) {
        if (!isRootScene() || !blackWidowState.canStageCompanion) return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, allowMultiple);
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/octet-stream",
                "*/*"
        });
        startActivityForResult(intent, requestCode);
    }

    private void choosePtxForCurrentSession() {
        if (!canAttachPtx()) return;
        final int partCount = NativeBridge.compositePartCount(session);
        if (partCount <= 0) {
            choosePtxCompanion(-1);
            return;
        }

        String[] names = new String[partCount + 1];
        names[0] = "Shared PTX · all MOD parts";
        for (int index = 0; index < partCount; ++index) {
            String name = NativeBridge.compositePartName(session, index);
            names[index + 1] = name == null || name.isEmpty()
                    ? "MOD part " + (index + 1)
                    : name;
        }
        new AlertDialog.Builder(this)
                .setTitle("Attach PTX texture")
                .setItems(names, (dialog, which) ->
                        choosePtxCompanion(which == 0 ? -1 : which - 1))
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void choosePtxCompanion(int partIndex) {
        if (!canAttachPtx()) return;
        pendingPtxPart = partIndex;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/vnd.dmc.ptx",
                "application/octet-stream",
                "*/*"
        });
        startActivityForResult(intent, REQUEST_ATTACH_PTX);
    }

    private void handleResetOrExport() {
        if (session == 0) return;
        if (blackWidowState.canExportPng) {
            choosePngExportDestination();
        } else if (blackWidowState.canRender) {
            renderView.resetView();
        }
    }

    private void choosePngExportDestination() {
        if (session == 0 || !blackWidowState.canExportPng) return;
        pendingExportSession = session;
        if (NativeBridge.childResourceCount(session) > 0) {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_EXPORT_GALLERY_TREE);
        } else {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("image/png");
            intent.putExtra(Intent.EXTRA_TITLE, singleExportFileName());
            startActivityForResult(intent, REQUEST_EXPORT_SINGLE_PNG);
        }
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null) {
            if (requestCode == REQUEST_ATTACH_PTX) pendingPtxPart = -1;
            if (requestCode == REQUEST_EXPORT_SINGLE_PNG
                    || requestCode == REQUEST_EXPORT_GALLERY_TREE) {
                pendingExportSession = 0;
            }
            return;
        }

        if (requestCode == REQUEST_EXPORT_SINGLE_PNG) {
            Uri target = data.getData();
            if (target != null) exportCurrentToUri(target);
            pendingExportSession = 0;
            return;
        }

        if (requestCode == REQUEST_EXPORT_GALLERY_TREE) {
            Uri tree = data.getData();
            if (tree != null) {
                persistUriPermission(tree, data.getFlags(), true);
                exportGalleryToTree(tree);
            }
            pendingExportSession = 0;
            return;
        }

        if (requestCode == REQUEST_ATTACH_PTX) {
            Uri uri = data.getData();
            final int targetPart = pendingPtxPart;
            pendingPtxPart = -1;
            if (uri == null) return;
            persistUriPermission(uri, data.getFlags(), false);
            attachPtxUri(uri, targetPart, true);
            return;
        }

        if (requestCode == REQUEST_ADD_MOD_PARTS) {
            ArrayList<Uri> added = selectedUris(data);
            if (added.isEmpty()) return;
            for (Uri uri : added) persistUriPermission(uri, data.getFlags(), false);

            // The first transition from one live MOD to a composite must not
            // reopen the already-loaded base through an old content URI. Keep
            // the native base Session authoritative and open only the new parts.
            // This is especially important for ACTION_VIEW/file-manager grants,
            // which may be temporary even though the MOD itself is still live.
            if (session != 0 && NativeBridge.compositePartCount(session) == 0) {
                composeCurrentWithAdditionalMods(added);
            } else {
                ArrayList<Uri> combined = new ArrayList<>(modelPartUris);
                for (Uri uri : added) if (!combined.contains(uri)) combined.add(uri);
                openCompositeUris(combined, true);
            }
            return;
        }

        if (isStagedAssetRequest(requestCode)) {
            ArrayList<Uri> uris = selectedUris(data);
            if (uris.isEmpty()) return;
            for (Uri uri : uris) persistUriPermission(uri, data.getFlags(), false);
            stageAssets(uris, stagedRoleForRequest(requestCode));
            return;
        }

        if (requestCode != REQUEST_OPEN) return;
        ArrayList<Uri> uris = selectedUris(data);
        if (uris.isEmpty()) return;
        for (Uri uri : uris) persistUriPermission(uri, data.getFlags(), false);
        if (uris.size() == 1) {
            openUri(uris.get(0));
        } else {
            openCompositeUris(uris, false);
        }
    }

    private boolean isStagedAssetRequest(int requestCode) {
        return requestCode == REQUEST_STAGE_MOTION
                || requestCode == REQUEST_STAGE_TEXTURE
                || requestCode == REQUEST_STAGE_PHYSICS
                || requestCode == REQUEST_STAGE_CLOTH
                || requestCode == REQUEST_STAGE_OTHER;
    }

    private String stagedRoleForRequest(int requestCode) {
        switch (requestCode) {
            case REQUEST_STAGE_MOTION: return ROLE_MOTION;
            case REQUEST_STAGE_TEXTURE: return ROLE_TEXTURE;
            case REQUEST_STAGE_PHYSICS: return ROLE_PHYSICS;
            case REQUEST_STAGE_CLOTH: return ROLE_CLOTH;
            default: return ROLE_OTHER;
        }
    }

    private void stageAssets(ArrayList<Uri> uris, String role) {
        int added = 0;
        for (Uri uri : uris) {
            boolean duplicate = false;
            for (StagedAsset asset : stagedAssets) {
                if (asset.uri.equals(uri) && asset.role.equals(role)) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            stagedAssets.add(new StagedAsset(uri, displayName(uri), role));
            ++added;
        }
        if (ROLE_MOTION.equals(role) && selectedMotionIndex < 0 && !motionAssets().isEmpty()) {
            selectedMotionIndex = 0;
        }
        rebuildInfo(titleView.getText().toString());
        applyResourceUiState();
        Toast.makeText(this,
                added + " " + role + " resource(s) staged",
                Toast.LENGTH_LONG).show();
    }

    private ArrayList<Uri> selectedUris(Intent data) {
        ArrayList<Uri> result = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int index = 0; index < clip.getItemCount(); ++index) {
                Uri uri = clip.getItemAt(index).getUri();
                if (uri != null && !result.contains(uri)) result.add(uri);
            }
        }
        Uri single = data.getData();
        if (single != null && !result.contains(single)) result.add(single);
        return result;
    }

    private void persistUriPermission(Uri uri, int dataFlags, boolean write) {
        int wanted = Intent.FLAG_GRANT_READ_URI_PERMISSION;
        if (write) wanted |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        final int flags = dataFlags & wanted;
        if (flags == 0) return;
        try {
            getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (SecurityException ignored) {}
    }

    private void handleIncomingIntent(Intent intent) {
        if (intent == null) {
            showIdleStatus();
            return;
        }

        ClipData clip = intent.getClipData();
        if (clip != null && clip.getItemCount() > 1) {
            ArrayList<Uri> uris = new ArrayList<>();
            for (int index = 0; index < clip.getItemCount(); ++index) {
                Uri item = clip.getItemAt(index).getUri();
                if (item != null && !uris.contains(item)) uris.add(item);
            }
            if (uris.size() > 1) {
                for (Uri item : uris) persistUriPermission(item, intent.getFlags(), false);
                openCompositeUris(uris, false);
                return;
            }
        }

        Uri uri = intent.getData();
        if (uri == null && Intent.ACTION_SEND.equals(intent.getAction())) {
            try {
                Object value = intent.getParcelableExtra(Intent.EXTRA_STREAM);
                if (value instanceof Uri) uri = (Uri) value;
            } catch (RuntimeException ignored) {}
        }
        if (uri == null && clip != null && clip.getItemCount() > 0) {
            uri = clip.getItemAt(0).getUri();
        }

        if (uri == null) {
            showIdleStatus();
        } else {
            persistUriPermission(uri, intent.getFlags(), false);
            openUri(uri);
        }
    }

    private void resetCompositionState() {
        modelPartUris.clear();
        modelPartPtxUris.clear();
        sharedModelPtxUri = null;
        stagedAssets.clear();
        selectedMotionIndex = -1;
    }

    private void showIdleStatus() {
        titleView.setText("DMC Native Reader");
        blackWidowState = BlackWidowState.empty();
        pendingPtxPart = -1;
        pendingExportSession = 0;
        resetCompositionState();
        setInfo("DMC Native Reader " + BuildConfig.VERSION_NAME + "\n"
                + "Architecture v2 core: MOD / SCM / DDS / PTX.\n"
                + "Unpromoted DMC families are intentionally excluded from main.\n\n"
                + "Open a supported resource from My Files or use ↑.\n"
                + "Select multiple canonical MOD files to compose them in one scene.\n"
                + "Use ⋮ for model parts, PTX, animations and staged future companions.");
        applyResourceUiState();
    }

    private String displayName(Uri uri) {
        if ("content".equals(uri.getScheme())) {
            try (Cursor cursor = getContentResolver().query(uri,
                    new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (index >= 0) return cursor.getString(index);
                }
            } catch (RuntimeException ignored) {}
        }
        String path = uri.getPath();
        return path == null ? "resource.bin" : new File(path).getName();
    }

    private ParcelFileDescriptor openReadOnlyDescriptor(Uri uri) throws FileNotFoundException {
        if ("file".equals(uri.getScheme()) && uri.getPath() != null) {
            return ParcelFileDescriptor.open(
                    new File(uri.getPath()), ParcelFileDescriptor.MODE_READ_ONLY);
        }
        return getContentResolver().openFileDescriptor(uri, "r");
    }

    private void activateSession(long handle, String name) {
        session = handle;
        titleView.setText(name);
        renderView.setSession(session);
        refreshBlackWidowState();
        rebuildInfo(name);
        applyResourceUiState();
    }

    private void rebuildInfo(String name) {
        final String inspection = session == 0 ? "" : NativeBridge.inspection(session);
        final String nativeInfo = session == 0 ? "" : NativeBridge.info(session);

        StringBuilder details = new StringBuilder();
        details.append(name).append("\n\nSTRUCTURE\n");
        details.append(inspection == null || inspection.isEmpty()
                ? "No typed inspection document.\n"
                : inspection);
        details.append("\nSESSION / EVIDENCE\n").append(nativeInfo).append("\n");
        if (!navigation.isEmpty()) {
            details.append("\nNAVIGATION\n")
                    .append("Depth: ").append(navigation.size()).append("\n")
                    .append("Back returns to: ").append(navigation.peek().title).append("\n");
        }
        if (!stagedAssets.isEmpty()) {
            details.append("\nSTAGED COMPANIONS\n");
            for (StagedAsset asset : stagedAssets) {
                details.append("- ").append(asset.role).append(": ")
                        .append(asset.name).append("\n");
            }
            details.append("Playback / physics application remains disabled until the corresponding native runtime is promoted.\n");
        }
        setInfo(details.toString());
    }

    private void openUri(Uri uri) {
        closeAllSessions();
        resetCompositionState();
        String name = displayName(uri);
        titleView.setText(name);

        long opened;
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
            if (pfd == null) throw new FileNotFoundException("No file descriptor");
            opened = NativeBridge.open(pfd.getFd(), name);
        } catch (Exception error) {
            setInfo(name + "\nOpen failed: " + error);
            applyResourceUiState();
            Toast.makeText(this, "Could not read file", Toast.LENGTH_LONG).show();
            return;
        }

        if (opened == 0) {
            setInfo(name + "\nRejected: supported route failed structural validation or format is outside MOD / SCM / DDS / PTX.");
            applyResourceUiState();
            Toast.makeText(this, "Unsupported or malformed DMC resource", Toast.LENGTH_LONG).show();
            return;
        }

        activateSession(opened, name);
        if (blackWidowState.canAddModelPart) {
            modelPartUris.add(uri);
            modelPartPtxUris.add(null);
        }
    }

    private void composeCurrentWithAdditionalMods(ArrayList<Uri> requested) {
        if (session == 0 || requested == null || requested.isEmpty()) return;
        if (NativeBridge.compositePartCount(session) != 0) {
            ArrayList<Uri> combined = new ArrayList<>(modelPartUris);
            for (Uri uri : requested) if (!combined.contains(uri)) combined.add(uri);
            openCompositeUris(combined, true);
            return;
        }

        ArrayList<Uri> additions = new ArrayList<>();
        for (Uri uri : requested) {
            if (uri != null && !modelPartUris.contains(uri) && !additions.contains(uri)) {
                additions.add(uri);
            }
        }
        if (additions.isEmpty()) {
            Toast.makeText(this, "No new MOD parts selected", Toast.LENGTH_LONG).show();
            return;
        }

        final ArrayList<Uri> previousPtx = new ArrayList<>(modelPartPtxUris);
        final long baseSession = session;
        final int totalParts = additions.size() + 1;
        long[] handles = new long[totalParts];
        String[] names = new String[totalParts];
        handles[0] = baseSession;
        names[0] = !modelPartUris.isEmpty()
                ? displayName(modelPartUris.get(0))
                : titleView.getText().toString();

        long composite = 0;
        String failure = null;
        try {
            for (int index = 0; index < additions.size(); ++index) {
                Uri uri = additions.get(index);
                final int target = index + 1;
                names[target] = displayName(uri);
                try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
                    if (pfd == null) throw new FileNotFoundException("No file descriptor");
                    handles[target] = NativeBridge.open(pfd.getFd(), names[target]);
                }
                if (handles[target] == 0) {
                    failure = "Could not open " + names[target] + " as a canonical DMC resource";
                    break;
                }
            }
            if (failure == null) {
                composite = NativeBridge.composeMods(handles, names);
                if (composite == 0) {
                    failure = "Selected files could not be composed with the live MOD session";
                }
            }
        } catch (Exception error) {
            failure = "Could not read selected MOD files: " + error;
        } finally {
            // handles[0] is the currently displayed Session and remains owned by
            // MainActivity until the replacement composite is known-good.
            for (int index = 1; index < handles.length; ++index) {
                if (handles[index] != 0) NativeBridge.close(handles[index]);
            }
        }

        if (composite == 0) {
            Toast.makeText(this,
                    failure == null ? "MOD composition failed" : failure,
                    Toast.LENGTH_LONG).show();
            return;
        }

        ArrayList<Uri> combinedUris = new ArrayList<>(modelPartUris);
        for (Uri uri : additions) if (!combinedUris.contains(uri)) combinedUris.add(uri);

        closeAllSessions();
        modelPartUris.clear();
        modelPartUris.addAll(combinedUris);
        modelPartPtxUris.clear();
        for (int index = 0; index < combinedUris.size(); ++index) {
            modelPartPtxUris.add(index < previousPtx.size() ? previousPtx.get(index) : null);
        }

        activateSession(composite, "MOD scene · " + combinedUris.size() + " parts");
        reattachSavedPtxToComposite();
        Toast.makeText(this,
                combinedUris.size()
                        + " MOD parts composed. Use ⋮ to place a part on a host joint.",
                Toast.LENGTH_LONG).show();
    }

    private void openCompositeUris(ArrayList<Uri> uris, boolean preserveAssets) {
        if (uris.size() < 2) {
            if (!uris.isEmpty() && !preserveAssets) openUri(uris.get(0));
            return;
        }

        ArrayList<Uri> previousPtx = preserveAssets
                ? new ArrayList<>(modelPartPtxUris)
                : new ArrayList<>();
        long[] handles = new long[uris.size()];
        String[] names = new String[uris.size()];
        long composite = 0;
        String failure = null;

        try {
            for (int index = 0; index < uris.size(); ++index) {
                Uri uri = uris.get(index);
                names[index] = displayName(uri);
                try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
                    if (pfd == null) throw new FileNotFoundException("No file descriptor");
                    handles[index] = NativeBridge.open(pfd.getFd(), names[index]);
                }
                if (handles[index] == 0) {
                    failure = "Could not open " + names[index] + " as a canonical DMC resource";
                    break;
                }
            }
            if (failure == null) {
                composite = NativeBridge.composeMods(handles, names);
                if (composite == 0) {
                    failure = "Multi-select can combine canonical MOD files only";
                }
            }
        } catch (Exception error) {
            failure = "Could not read selected MOD files: " + error;
        } finally {
            for (long handle : handles) {
                if (handle != 0) NativeBridge.close(handle);
            }
        }

        if (composite == 0) {
            Toast.makeText(this,
                    failure == null ? "MOD composition failed" : failure,
                    Toast.LENGTH_LONG).show();
            return;
        }

        closeAllSessions();
        if (!preserveAssets) resetCompositionState();
        modelPartUris.clear();
        modelPartUris.addAll(uris);
        modelPartPtxUris.clear();
        for (int index = 0; index < uris.size(); ++index) {
            modelPartPtxUris.add(index < previousPtx.size() ? previousPtx.get(index) : null);
        }

        activateSession(composite, "MOD scene · " + uris.size() + " parts");
        reattachSavedPtxToComposite();
        Toast.makeText(this,
                uris.size()
                        + " MOD parts composed in source coordinates. Use ⋮ for placement.",
                Toast.LENGTH_LONG).show();
    }

    private void reattachSavedPtxToComposite() {
        if (session == 0 || NativeBridge.compositePartCount(session) <= 0) return;
        int restored = 0;

        if (sharedModelPtxUri != null) {
            final String sharedName = displayName(sharedModelPtxUri);
            try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(sharedModelPtxUri)) {
                if (pfd != null && NativeBridge.attachPtx(
                        session, pfd.getFd(), sharedName)) {
                    ++restored;
                }
            } catch (Exception ignored) {}
        }

        for (int index = 0; index < modelPartPtxUris.size(); ++index) {
            Uri uri = modelPartPtxUris.get(index);
            if (uri == null) continue;
            final String ptxName = displayName(uri);
            try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
                if (pfd != null && NativeBridge.attachPtxToPart(
                        session, index, pfd.getFd(), ptxName)) {
                    ++restored;
                }
            } catch (Exception ignored) {}
        }
        if (restored > 0) {
            refreshBlackWidowState();
            renderView.renderNow();
            rebuildInfo(titleView.getText().toString());
            applyResourceUiState();
        }
    }

    private void rememberPtxForPart(Uri uri, int partIndex) {
        if (modelPartUris.isEmpty()) return;
        while (modelPartPtxUris.size() < modelPartUris.size()) modelPartPtxUris.add(null);

        if (partIndex < 0) {
            sharedModelPtxUri = uri;
            for (int index = 0; index < modelPartPtxUris.size(); ++index) {
                modelPartPtxUris.set(index, null);
            }
            return;
        }

        if (partIndex >= modelPartUris.size()) return;
        modelPartPtxUris.set(partIndex, uri);
    }

    private void attachPtxUri(Uri uri, int partIndex, boolean remember) {
        if (!canAttachPtx()) return;
        final String ptxName = displayName(uri);
        final boolean attached;
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
            if (pfd == null) throw new FileNotFoundException("No file descriptor");
            attached = partIndex >= 0
                    ? NativeBridge.attachPtxToPart(session, partIndex, pfd.getFd(), ptxName)
                    : NativeBridge.attachPtx(session, pfd.getFd(), ptxName);
        } catch (Exception error) {
            Toast.makeText(this, "Could not read PTX", Toast.LENGTH_LONG).show();
            return;
        }

        refreshBlackWidowState();
        final String diagnostic = NativeBridge.textureAttachmentInfo(session);
        if (attached) {
            if (remember) rememberPtxForPart(uri, partIndex);
            renderView.renderNow();
            rebuildInfo(titleView.getText().toString());
            applyResourceUiState();
            Toast.makeText(this,
                    diagnostic == null || diagnostic.isEmpty()
                            ? "PTX textures attached"
                            : diagnostic,
                    Toast.LENGTH_LONG).show();
        } else {
            rebuildInfo(titleView.getText().toString());
            applyResourceUiState();
            Toast.makeText(this,
                    diagnostic == null || diagnostic.isEmpty()
                            ? "PTX could not be matched to this model"
                            : diagnostic,
                    Toast.LENGTH_LONG).show();
        }
    }

    private String exportRootTitle() {
        NavigationEntry root = navigation.peekLast();
        return root == null ? titleView.getText().toString() : root.title;
    }

    private String withoutExtension(String value) {
        if (value == null || value.isEmpty()) return "resource";
        int dot = value.lastIndexOf('.');
        if (dot > 0) return value.substring(0, dot);
        return value;
    }

    private String sanitizeFileComponent(String value) {
        if (value == null || value.isEmpty()) return "image";
        StringBuilder out = new StringBuilder(value.length());
        boolean lastUnderscore = false;
        for (int index = 0; index < value.length(); ++index) {
            char c = value.charAt(index);
            boolean forbidden = c == '\\' || c == '/' || c == ':' || c == '*'
                    || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
            boolean separator = forbidden || Character.isWhitespace(c) || c == '·';
            if (separator) {
                if (!lastUnderscore && out.length() > 0) out.append('_');
                lastUnderscore = true;
            } else {
                out.append(c);
                lastUnderscore = false;
            }
        }
        while (out.length() > 0 && out.charAt(out.length() - 1) == '_') {
            out.deleteCharAt(out.length() - 1);
        }
        return out.length() == 0 ? "image" : out.toString();
    }

    private String singleExportFileName() {
        String rootTitle = exportRootTitle();
        String root = sanitizeFileComponent(withoutExtension(rootTitle));
        String current = titleView.getText().toString();
        if (!current.equals(rootTitle)) {
            return root + "__" + sanitizeFileComponent(current) + ".png";
        }
        return root + ".png";
    }

    private String childExportFileName(String childTitle) {
        String root = sanitizeFileComponent(withoutExtension(exportRootTitle()));
        return root + "__" + sanitizeFileComponent(childTitle) + ".png";
    }

    private Bitmap bitmapForSession(long handle) {
        if (handle == 0) return null;
        BlackWidowState state = BlackWidowState.fromNative(NativeBridge.blackWidowState(handle));
        final int width;
        final int height;
        if (state.uvMapView) {
            width = UV_EXPORT_SIZE;
            height = UV_EXPORT_SIZE;
        } else {
            width = NativeBridge.imagePreviewWidth(handle);
            height = NativeBridge.imagePreviewHeight(handle);
        }

        final long expected = (long) width * (long) height;
        if (width <= 0 || height <= 0 || expected <= 0L || expected > Integer.MAX_VALUE) {
            return null;
        }

        final Bitmap bitmap;
        try {
            bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        } catch (IllegalArgumentException | OutOfMemoryError error) {
            return null;
        }

        final boolean filled = state.uvMapView
                ? NativeBridge.render(handle, width, height, 0.0f, 0.0f, 1.0f, 0, bitmap)
                : NativeBridge.imagePreview(handle, bitmap);
        if (!filled) {
            bitmap.recycle();
            return null;
        }
        return bitmap;
    }

    private boolean saveBitmapToUri(Bitmap bitmap, Uri target) {
        if (bitmap == null || target == null) return false;
        try (OutputStream output = getContentResolver().openOutputStream(target, "w")) {
            return output != null && bitmap.compress(Bitmap.CompressFormat.PNG, 100, output);
        } catch (Exception error) {
            return false;
        }
    }

    private void exportCurrentToUri(Uri target) {
        final long handle = pendingExportSession != 0 ? pendingExportSession : session;
        Bitmap bitmap = bitmapForSession(handle);
        boolean saved = saveBitmapToUri(bitmap, target);
        if (bitmap != null) bitmap.recycle();
        Toast.makeText(this,
                saved ? "PNG saved" : "Could not export PNG",
                Toast.LENGTH_LONG).show();
    }

    private void exportGalleryToTree(Uri treeUri) {
        final long handle = pendingExportSession != 0 ? pendingExportSession : session;
        if (handle == 0) return;
        final int count = NativeBridge.childResourceCount(handle);
        if (count <= 0) {
            Toast.makeText(this, "No gallery images to export", Toast.LENGTH_LONG).show();
            return;
        }

        final Uri parent;
        try {
            String documentId = DocumentsContract.getTreeDocumentId(treeUri);
            parent = DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId);
        } catch (RuntimeException error) {
            Toast.makeText(this, "Selected folder is not writable", Toast.LENGTH_LONG).show();
            return;
        }

        int saved = 0;
        for (int index = 0; index < count; ++index) {
            long child = 0;
            Bitmap bitmap = null;
            try {
                String childTitle = NativeBridge.childResourceTitle(handle, index);
                child = NativeBridge.openChild(handle, index);
                if (child == 0) continue;
                bitmap = bitmapForSession(child);
                if (bitmap == null) continue;
                Uri target = DocumentsContract.createDocument(
                        getContentResolver(), parent, "image/png",
                        childExportFileName(childTitle));
                if (target != null && saveBitmapToUri(bitmap, target)) ++saved;
            } catch (Exception ignored) {
            } finally {
                if (bitmap != null) bitmap.recycle();
                if (child != 0) NativeBridge.close(child);
            }
        }

        Toast.makeText(this,
                "PNG export: " + saved + "/" + count,
                Toast.LENGTH_LONG).show();
    }

    private void openChildResource(int index, String childTitle) {
        if (session == 0) return;
        final long child = NativeBridge.openChild(session, index);
        if (child == 0) {
            Toast.makeText(this, "Could not open child resource", Toast.LENGTH_LONG).show();
            return;
        }
        navigateToSession(child, childTitle);
    }

    private void navigateToSession(long handle, String title) {
        navigation.push(new NavigationEntry(session, titleView.getText().toString()));
        activateSession(handle, title);
    }

    private boolean navigateToParent() {
        if (navigation.isEmpty()) return false;

        final long child = session;
        renderView.setSession(0);
        childBrowser.setSession(0);
        if (child != 0) NativeBridge.close(child);

        NavigationEntry parent = navigation.pop();
        activateSession(parent.session, parent.title);
        return true;
    }

    private void navigateBack() {
        if (navigateToParent()) return;
        finish();
    }

    @Override public void onBackPressed() {
        navigateBack();
    }

    private void closeAllSessions() {
        renderView.setSession(0);
        childBrowser.setSession(0);
        blackWidowState = BlackWidowState.empty();
        pendingPtxPart = -1;
        pendingExportSession = 0;

        if (session != 0) {
            NativeBridge.close(session);
            session = 0;
        }
        while (!navigation.isEmpty()) {
            NavigationEntry entry = navigation.pop();
            if (entry.session != 0) NativeBridge.close(entry.session);
        }
        applyResourceUiState();
    }

    @Override protected void onDestroy() {
        closeAllSessions();
        super.onDestroy();
    }
}
