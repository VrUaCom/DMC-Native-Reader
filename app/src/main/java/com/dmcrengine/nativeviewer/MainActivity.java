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
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int REQUEST_OPEN = 1001;
    private static final int REQUEST_ATTACH_PTX = 1002;
    private static final int REQUEST_EXPORT_SINGLE_PNG = 1003;
    private static final int REQUEST_EXPORT_GALLERY_TREE = 1004;
    private static final int REQUEST_ADD_MOD_PARTS = 1005;
    private static final int REQUEST_ADD_PAC = 1011;
    private static final int REQUEST_ROOM = 1012;
    private static final int REQUEST_ROOM_EFFECTS = 1014;
    private static final String PREF_EFFECTS_NAME = "room.effects.name";
    private static final String EFFECTS_FILE = "room_effects.bin";
    private static final int REQUEST_EXPORT_INFO = 1013;
    private static final int REQUEST_SAVE_SOURCE = 1015;
    private static final int REQUEST_STAGE_CONFIG = 1016;
    private static final int REQUEST_CUTSCENE_SOUND = 1017;
    private static final String PREFS = "viewer";
    private static final String PREF_ROOM_NAME = "room.name";
    private static final String PREF_ROOM_SHOWN = "room.shown";
    private static final String ROOM_FILE = "room.bin";
    private static final int REQUEST_STAGE_MOTION = 1006;
    private static final int REQUEST_STAGE_TEXTURE = 1007;
    private static final int REQUEST_STAGE_PHYSICS = 1008;
    private static final int REQUEST_STAGE_CLOTH = 1009;
    private static final int REQUEST_STAGE_OTHER = 1010;
    private static final int REQUEST_EXPORT_DIAGNOSTICS = 1014;

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
    private static final int MENU_COMPOSITE_PARTS = 11;
    private static final int MENU_DIAGNOSTICS = 12;

    private static final String ROLE_MOTION = "motion";
    private static final String ROLE_TEXTURE = "texture";
    private static final String ROLE_PHYSICS = "physics";
    private static final String ROLE_CLOTH = "cloth";
    private static final String ROLE_OTHER = "other";

    private static final int TOOL_SIZE_DP = 48;
    private static final int TOOL_GAP_DP = 4;
    private static final int DIAGNOSTICS_PREVIEW_BYTES = 64 * 1024;
    private static final DateTimeFormatter DIAGNOSTICS_TIME_FORMAT =
            DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss", Locale.ROOT);

    private static final class NavigationEntry {
        final long session;
        final String title;

        NavigationEntry(long session, String title) {
            this.session = session;
            this.title = title;
        }
    }

    private long pendingSaveSession;

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
    // Orange warning: something on screen was read through a non-canonical path.
    private Button nonCanonicalBadge;
    private static final int NON_CANONICAL_ORANGE = 0xffff9800;
    private Button resetButton;
    private Button wireButton;
    private Button hierarchyButton;
    private Button uvButton;
    private Button shadowButton;
    private Button openButton;
    private Button collisionButton;
    private Button stageCollisionButton;
    private Button soundButton;
    private android.media.AudioTrack soundTrack;
    private int soundIndex;
    private Button flyButton;
    private Button breakButton;
    // Position in the collision cycle: -1 all attacks, then each used id.
    private int collisionCursor = -2;
    private Button infoButton;
    private HorizontalScrollView effectModeScroll;
    private LinearLayout effectModeBar;
    private Button effectVisualButton;
    private Button effectInfoButton;
    private HorizontalScrollView motionScroll;
    private HorizontalScrollView toolScroll;
    // In-viewport notice (replaces Toasts so messages never cover the motion
    // strip or the tool bar).
    private TextView noticeView;
    private final Runnable hideNotice = () -> {
        if (noticeView != null) noticeView.animate().alpha(0f).setDuration(250)
                .withEndAction(() -> noticeView.setVisibility(View.GONE)).start();
    };
    private LinearLayout motionBar;
    // Visual separators between nested motion PACs are not motion entries.
    // Keep a direct index -> card map so gesture navigation never mistakes a
    // separator for an animation button.
    private final ArrayList<View> motionCards = new ArrayList<>();

    private long session;
    private long pendingExportSession;
    private int pendingPtxPart = -1;
    private int selectedMotionIndex = -1;
    // -1 = raw MOT playback; >=0 = MotionScript button/controller index.
    private int selectedScriptIndex = -1;
    private LinearLayout headerBar;
    // Hidden by the top-edge swipe: bars and the visibility each had.
    private boolean uiHidden;
    private final int[] barVisibility = new int[6];
    // Camera mode: the camera's bar in place of the motion strip and tools.
    private Button cameraModeButton;
    private LinearLayout cameraPanel;
    private LinearLayout cameraDialBar;
    private LinearLayout cameraAdjust;
    // The open dial's settings: at the top of the view, over it, so the bars
    // below stay where they are and the view keeps its size.
    private ScrollView cameraAdjustScroll;
    private FrameLayout viewportFrame;
    private Button cameraPlayButton;
    private Button cameraRecordButton;
    private Button cameraGyroButton;
    private Button cameraWalkButton;
    private boolean cameraUi;
    // The phone's turns for the gyroscope (camera mode, gyroscope on).
    private android.hardware.SensorManager sensorManager;
    private boolean gyroListening;
    private boolean volumeHeld;
    // Walking: ARCore follows the phone's steps (camera mode, walking on).
    private ArWalker arWalker;
    private boolean activityResumed;
    private static final int REQUEST_CAMERA_FOR_WALK = 4710;
    private static final String PREF_AR_NOTICE = "ar_notice_shown";
    private String cameraDialOpen;  // the open dial's indexes, or null
    // Archive the root scene was assembled from (re-opened on demand so the
    // per-file browser owns an independent read-only handle).
    private Uri assembledPacUri;
    // Stage archive the root scene was assembled from (its files stay
    // browsable from the menu).
    private Uri stagePacUri;
    /** The video being recorded from the ⋮ menu, or null. */
    private ViewRecorder recorder;
    // Selected class inside a shared enemy archive (em000.pac); 0 = first.
    private int enemyVariant;
    // Position buttons under the title (one per enemy class / weapon / dress state).
    private HorizontalScrollView variantScroll;
    private LinearLayout variantBar;
    // Extra archives (weapons, props) assembled onto the character, in order.
    private final ArrayList<Uri> addedPacUris = new ArrayList<>();

    private final ArrayDeque<NavigationEntry> navigation = new ArrayDeque<>();
    private final ArrayList<Uri> modelPartUris = new ArrayList<>();
    private final ArrayList<Uri> modelPartPtxUris = new ArrayList<>();
    private final ArrayList<StagedAsset> stagedAssets = new ArrayList<>();
    private Uri sharedModelPtxUri;

    private BlackWidowState blackWidowState = BlackWidowState.empty();
    private String infoText = "";
    private String pendingInfoExportText = "";

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        applyViewerSettings();
        // A gesture added later is on for a mask stored before it existed.
        if (!prefs().getBoolean(PREF_GESTURE_CAMERA_V2, false)) {
            prefs().edit()
                    .putInt(PREF_GESTURES, prefs().getInt(PREF_GESTURES, DmcRenderView.G_ALL)
                            | DmcRenderView.G_CLOSE_PAN | DmcRenderView.G_TURN)
                    .putBoolean(PREF_GESTURE_CAMERA_V2, true).apply();
        }
        if (!prefs().getBoolean(PREF_GESTURE_DOLLY, false)) {
            prefs().edit()
                    .putInt(PREF_GESTURES, prefs().getInt(PREF_GESTURES, DmcRenderView.G_ALL) | DmcRenderView.G_DOLLY)
                    .putBoolean(PREF_GESTURE_DOLLY, true).apply();
        }
        renderView.setGestures(prefs().getInt(PREF_GESTURES, DmcRenderView.G_ALL));
        renderView.setResolutionListener(side -> prefs().edit().putInt(SET_MAX_SIDE, side).apply());
        restoreRoom();
        restoreRoomEffects();
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

    // Unavailable tools are hidden rather than greyed out; the tool bar
    // scrolls sideways when the remaining ones do not fit.
    private void notice(CharSequence text, int length) {
        if (noticeView == null) return;
        noticeView.removeCallbacks(hideNotice);
        noticeView.animate().cancel();
        noticeView.setText(text);
        noticeView.setAlpha(1f);
        noticeView.setVisibility(View.VISIBLE);
        noticeView.postDelayed(hideNotice, length == Toast.LENGTH_LONG ? 3500 : 2000);
    }

    private void setToolAvailable(Button button, boolean available) {
        button.setEnabled(available);
        button.setAlpha(1.0f);
        button.setVisibility(available ? View.VISIBLE : View.GONE);
    }

    /** The drone's button: its picture in the square instead of a letter. */
    private Button makeDroneButton() {
        final Button button = makeSquareButton("", "Drone: orbit / fly", 18f);
        final android.graphics.drawable.Drawable drone = getDrawable(R.drawable.ic_drone);
        if (drone != null) button.setForeground(new android.graphics.drawable.InsetDrawable(drone, dp(6)));
        return button;
    }

    private void syncToggleButton(Button button, boolean available, boolean active) {
        button.setEnabled(available);
        button.setActivated(available && active);
        button.setAlpha(active ? 1.0f : 0.78f);
        button.setVisibility(available ? View.VISIBLE : View.GONE);
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
        // An assembled PAC already owns its part list; re-composing it from
        // user-picked URIs would drop the archive's own MODs.
        return isRootScene() && blackWidowState.canAddModelPart && assembledPacUri == null;
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
        // Black Widow state also follows core-global view state (room broken,
        // stage markers, the room loaded): read it fresh for every refresh.
        refreshBlackWidowState();
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

        setToolAvailable(openButton, !hasSession);

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

        // View tools: Black Widow decides which ones the session offers.
        final boolean viewTools = hasSession && !renderView.isUvLayoutVisible();
        syncToggleButton(shadowButton, viewTools && blackWidowState.canShowShadows, renderView.isShadowVisible());
        syncToggleButton(collisionButton, viewTools && blackWidowState.canShowHitboxes,
                renderView.isCollisionVisible());
        syncToggleButton(stageCollisionButton, viewTools && blackWidowState.canShowStageCollision,
                renderView.isRoomCollisionVisible());
        final boolean flyAvailable = viewTools && blackWidowState.canFly;
        if (!flyAvailable && renderView.isFlyMode()) renderView.setFlyMode(false);
        // A view opened in camera mode gets the free camera too (a cutscene
        // whose cuts drive the view keeps them: the mode films through them).
        if (flyAvailable && cameraUi && !renderView.isFlyMode() && !NativeBridge.cameraFollowsCuts(session)
                && renderView.setFlyMode(true, false)) {
            applyResourceUiState();
            return;
        }
        syncToggleButton(flyButton, flyAvailable, renderView.isDroneFlying());
        syncToggleButton(breakButton, viewTools && blackWidowState.canBreakRoom, blackWidowState.roomBroken);

        syncToggleButton(uvButton,
                hasSession && (blackWidowState.canShowUv || blackWidowState.canInspectUv),
                renderView.isUvLayoutVisible());

        syncToggleButton(soundButton, hasSession && blackWidowState.canPlaySound, soundTrack != null);

        setToolAvailable(infoButton,
                hasSession ? blackWidowState.canInspect : !infoText.isEmpty());
        refreshMotionStrip();
        refreshVariantBar();
        refreshEffectModeBar();
    }

    private void refreshEffectModeBar() {
        if (effectModeScroll == null || effectVisualButton == null || effectInfoButton == null) {
            return;
        }
        final boolean available = session != 0 && NativeBridge.hasDualPreview(session);
        if (!available) {
            effectModeScroll.setVisibility(View.GONE);
            return;
        }

        final boolean info = NativeBridge.infoPreviewActive(session);
        effectVisualButton.setActivated(!info);
        effectVisualButton.setAlpha(info ? 0.68f : 1.0f);
        effectInfoButton.setActivated(info);
        effectInfoButton.setAlpha(info ? 1.0f : 0.68f);
        effectModeScroll.setVisibility(uiHidden ? View.GONE : View.VISIBLE);
    }

    private void selectEffectPreviewMode(boolean info) {
        if (session == 0 || !NativeBridge.hasDualPreview(session)) return;
        if (!NativeBridge.setInfoPreviewActive(session, info)) return;
        renderView.refreshStaticImagePreview();
        refreshEffectModeBar();
        notice(info ? "Info view" : "Visual view", Toast.LENGTH_SHORT);
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
        headerBar = header;
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

        nonCanonicalBadge = makeSquareButton("▲", "Shown, but not read canonically", 22f);
        nonCanonicalBadge.setTextColor(NON_CANONICAL_ORANGE);
        nonCanonicalBadge.setVisibility(View.GONE);
        nonCanonicalBadge.setOnClickListener(v -> showNonCanonicalNotes());
        header.addView(nonCanonicalBadge, new LinearLayout.LayoutParams(
                dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP)));

        // Camera mode on and off: the same button goes back to the scene.
        cameraModeButton = makeSquareButton("📷", "Camera mode", 22f);
        cameraModeButton.setOnClickListener(v -> toggleCameraMode());
        header.addView(cameraModeButton, new LinearLayout.LayoutParams(
                dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP)));

        moreButton = makeSquareButton("⋮", "Add or attach DMC resource", 28f);
        moreButton.setOnClickListener(this::showCompanionMenu);
        header.addView(moreButton, new LinearLayout.LayoutParams(
                dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP)));

        root.addView(header, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        variantScroll = new HorizontalScrollView(this);
        variantScroll.setHorizontalScrollBarEnabled(false);
        variantScroll.setVisibility(View.GONE);
        variantBar = new LinearLayout(this);
        variantBar.setOrientation(LinearLayout.HORIZONTAL);
        variantBar.setGravity(Gravity.CENTER_VERTICAL);
        variantBar.setPadding(dp(8), dp(2), dp(8), dp(2));
        variantScroll.addView(variantBar, new HorizontalScrollView.LayoutParams(
                HorizontalScrollView.LayoutParams.WRAP_CONTENT,
                HorizontalScrollView.LayoutParams.WRAP_CONTENT));
        root.addView(variantScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        effectModeScroll = new HorizontalScrollView(this);
        effectModeScroll.setHorizontalScrollBarEnabled(false);
        effectModeScroll.setFillViewport(true);
        effectModeScroll.setVisibility(View.GONE);
        effectModeBar = new LinearLayout(this);
        effectModeBar.setOrientation(LinearLayout.HORIZONTAL);
        effectModeBar.setGravity(Gravity.CENTER);
        effectModeBar.setPadding(dp(8), dp(2), dp(8), dp(2));

        effectVisualButton = makeSquareButton("Visual", "Show reconstructed effect view", 13f);
        effectVisualButton.setPadding(dp(18), 0, dp(18), 0);
        effectVisualButton.setOnClickListener(v -> selectEffectPreviewMode(false));
        LinearLayout.LayoutParams visualParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(TOOL_SIZE_DP - 8));
        visualParams.setMarginStart(dp(TOOL_GAP_DP));
        visualParams.setMarginEnd(dp(TOOL_GAP_DP));
        effectModeBar.addView(effectVisualButton, visualParams);

        effectInfoButton = makeSquareButton("Info", "Show EXE-backed effect information", 13f);
        effectInfoButton.setPadding(dp(18), 0, dp(18), 0);
        effectInfoButton.setOnClickListener(v -> selectEffectPreviewMode(true));
        LinearLayout.LayoutParams infoParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(TOOL_SIZE_DP - 8));
        infoParams.setMarginStart(dp(TOOL_GAP_DP));
        infoParams.setMarginEnd(dp(TOOL_GAP_DP));
        effectModeBar.addView(effectInfoButton, infoParams);

        effectModeScroll.addView(effectModeBar, new HorizontalScrollView.LayoutParams(
                HorizontalScrollView.LayoutParams.WRAP_CONTENT,
                HorizontalScrollView.LayoutParams.WRAP_CONTENT));
        root.addView(effectModeScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        FrameLayout viewport = new FrameLayout(this);
        viewportFrame = viewport;
        // Under the view: the window the GPU shows its frames on; the view
        // draws its picture over it only for frames that went through one.
        final android.view.SurfaceView presentSurface = new android.view.SurfaceView(this);
        presentSurface.getHolder().addCallback(new android.view.SurfaceHolder.Callback() {
            @Override public void surfaceCreated(android.view.SurfaceHolder holder) {
                NativeBridge.setPresentSurface(holder.getSurface());
                if (renderView != null) renderView.renderNow();
            }

            @Override public void surfaceChanged(android.view.SurfaceHolder holder, int format, int width, int height) {
                if (renderView != null) renderView.renderNow();
            }

            @Override public void surfaceDestroyed(android.view.SurfaceHolder holder) {
                NativeBridge.setPresentSurface(null);
                if (renderView != null) renderView.presentSurfaceGone();
            }
        });
        viewport.addView(presentSurface, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        renderView = new DmcRenderView(this);
        renderView.setGestureListener(gestureListener);
        viewport.addView(renderView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        childBrowser = new ChildResourceBrowserView(this);
        childBrowser.setVisibility(View.GONE);
        childBrowser.setListener(this::openChildResource);
        viewport.addView(childBrowser, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        noticeView = new TextView(this);
        noticeView.setTextColor(Color.WHITE);
        noticeView.setTextSize(14f);
        noticeView.setMaxLines(3);
        noticeView.setEllipsize(TextUtils.TruncateAt.END);
        noticeView.setPadding(dp(14), dp(8), dp(14), dp(8));
        noticeView.setBackgroundColor(Color.argb(210, 32, 34, 44));
        noticeView.setVisibility(View.GONE);
        noticeView.setOnClickListener(v -> hideNotice.run());
        FrameLayout.LayoutParams noticeParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        noticeParams.topMargin = dp(10);
        noticeParams.leftMargin = dp(12);
        noticeParams.rightMargin = dp(12);
        viewport.addView(noticeView, noticeParams);

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

        // Tool bar: centred while it fits, scrolls sideways when it does not
        // (same as the motion strip).
        toolScroll = new HorizontalScrollView(this);
        toolScroll.setHorizontalScrollBarEnabled(false);
        toolScroll.setFillViewport(true);
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER);
        bar.setPadding(dp(8), dp(6), dp(8), dp(8));
        toolScroll.addView(bar, new HorizontalScrollView.LayoutParams(
                HorizontalScrollView.LayoutParams.WRAP_CONTENT,
                HorizontalScrollView.LayoutParams.WRAP_CONTENT));

        // Open: here while nothing is open; with a file open it is in the ⋮ menu.
        openButton = makeSquareButton("↑", "Open resource or combine multiple MOD files", 28f);
        openButton.setOnClickListener(v -> chooseFile());
        addToolButton(bar, openButton);

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

        shadowButton = makeSquareButton("\u25D0", "Shadows (SHW)", 20f);
        shadowButton.setOnClickListener(v -> {
            if (session == 0 || !blackWidowState.canShowShadows) return;
            renderView.toggleShadows();
            applyResourceUiState();
        });
        addToolButton(bar, shadowButton);

        // Hitboxes: off -> every attack -> each attack id in turn -> off.
        collisionButton = makeSquareButton("\u25CE", "Attack collision (hitboxes)", 20f);
        collisionButton.setOnClickListener(v -> {
            if (session == 0 || !blackWidowState.canShowHitboxes) return;
            final int[] ids = NativeBridge.collisionAttackIds(session);
            if (ids == null) return;
            if (!renderView.isCollisionVisible()) {
                collisionCursor = -1;
            } else {
                collisionCursor++;
            }
            if (collisionCursor >= ids.length) {
                collisionCursor = -2;
                renderView.setCollisionVisible(false);
                notice("Hitboxes off", Toast.LENGTH_SHORT);
            } else {
                final int attack = collisionCursor < 0 ? -1 : ids[collisionCursor];
                final String label = NativeBridge.selectCollisionAttack(session, attack);
                renderView.setCollisionVisible(true);
                notice("Hitboxes: " + label, Toast.LENGTH_SHORT);
            }
            applyResourceUiState();
        });
        // Long press: what the hitboxes are, grouped (the core's account).
        collisionButton.setOnLongClickListener(v -> {
            showCollisionReport(true);
            return true;
        });
        addToolButton(bar, collisionButton);

        // Stage collision: shows the HITS walls and floors of the room. They
        // are always active: characters stop at walls and follow floors,
        // shots hit them.
        stageCollisionButton = makeSquareButton("\u25A6", "Stage collision (HITS)", 20f);
        stageCollisionButton.setOnClickListener(v -> {
            final int stageSources = session != 0 ? NativeBridge.stageCollisionSourceCount(session) : 0;
            if (stageSources <= 0 && (!roomLoaded || NativeBridge.roomCollisionSourceCount() <= 0)) return;
            final boolean show = !renderView.isRoomCollisionVisible();
            if (show && stageSources <= 0 && !roomShown()) {
                prefs().edit().putBoolean(PREF_ROOM_SHOWN, true).apply();
            }
            renderView.setRoomCollisionVisible(show);
            notice(show ? "Stage collision shown: "
                            + (stageSources > 0 ? stageSources : NativeBridge.roomCollisionSourceCount())
                            + " HITS source(s)"
                         : "Stage collision hidden (still active)", Toast.LENGTH_SHORT);
            applyResourceUiState();
        });
        stageCollisionButton.setOnLongClickListener(v -> {
            showCollisionReport(false);
            return true;
        });
        addToolButton(bar, stageCollisionButton);

        // Sound: the core decodes it to PCM and names it; the platform player
        // plays it (tap: play / stop, long press: which sound).
        soundButton = makeSquareButton("\u25B6", "Play sound", 20f);
        soundButton.setOnClickListener(v -> {
            if (soundTrack != null) {
                stopSound();
            } else {
                playSound(soundIndex);
            }
            applyResourceUiState();
        });
        soundButton.setOnLongClickListener(v -> {
            if (session == 0) return true;
            showNativeMenu(NativeBridge.soundMenu(session), id -> {
                playSound(id);
                applyResourceUiState();
            });
            return true;
        });
        addToolButton(bar, soundButton);

        // Camera mode: orbit (turn round the model / stage) or fly (joysticks
        // under the fingers: left moves, right looks). The same button goes back.
        flyButton = makeDroneButton();
        flyButton.setOnClickListener(v -> toggleDrone());
        addToolButton(bar, flyButton);

        // Breakable stage objects ("# SET n BREAK"): show them broken (their
        // bmodel, the break effect played once) or intact.
        breakButton = makeSquareButton("\u2716", "Break stage objects", 18f);
        breakButton.setOnClickListener(v -> {
            if (session == 0 || !blackWidowState.canBreakRoom) return;
            final boolean broken = !blackWidowState.roomBroken;
            NativeBridge.setRoomBroken(broken);
            notice(broken ? "Stage objects broken" : "Stage objects intact", Toast.LENGTH_SHORT);
            applyResourceUiState();
            renderView.renderNow();
        });
        addToolButton(bar, breakButton);

        uvButton = makeSquareButton("UV", "UV layout", 14f);
        uvButton.setOnClickListener(v -> {
            if (!blackWidowState.canShowUv) return;
            final long gallery = NativeBridge.openUvGallery(session);
            if (gallery == 0) {
                notice("UV maps unavailable: incomplete bindings", Toast.LENGTH_LONG);
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


        root.addView(toolScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        buildCameraPanel(root);
        setContentView(root);
        setCameraUi(NativeBridge.cameraMode());
        showIdleStatus();
    }

    // ---- Room: a stage archive shown around every model instead of the floor.

    private boolean roomLoaded;
    private String roomName = "";

    private String effectsName = "";
    private String effectsDetail = "";

    private File effectsFile() {
        return new File(getFilesDir(), EFFECTS_FILE);
    }

    private void roomEffectsChoose() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_ROOM_EFFECTS);
    }

    private void roomEffectsRemove() {
        NativeBridge.clearRoomEffects();
        effectsName = "";
        effectsDetail = "";
        //noinspection ResultOfMethodCallIgnored
        effectsFile().delete();
        prefs().edit().remove(PREF_EFFECTS_NAME).apply();
        renderView.refreshRoom();
        notice("Stage effects removed", Toast.LENGTH_SHORT);
    }

    /** Copies the picked stage effect bank (st*_effect.pac) into app storage and loads it. */
    private void chooseRoomEffects(Uri uri) {
        final String name = displayName(uri);
        new Thread(() -> {
            final File target = effectsFile();
            final File partial = new File(getFilesDir(), EFFECTS_FILE + ".part");
            boolean copied = false;
            try (java.io.InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new java.io.FileOutputStream(partial)) {
                if (in != null) {
                    byte[] buffer = new byte[1 << 16];
                    int read;
                    while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
                    copied = true;
                }
            } catch (Exception ignored) {
                copied = false;
            }
            final String detail = copied ? loadEffectsFile(partial, name) : null;
            if (detail == null) {
                //noinspection ResultOfMethodCallIgnored
                partial.delete();
                runOnUiThread(() -> notice("Stage effects: " + name + " is not an effect bank", Toast.LENGTH_LONG));
                return;
            }
            //noinspection ResultOfMethodCallIgnored
            target.delete();
            //noinspection ResultOfMethodCallIgnored
            partial.renameTo(target);
            prefs().edit().putString(PREF_EFFECTS_NAME, name).apply();
            runOnUiThread(() -> {
                effectsName = name;
                effectsDetail = detail;
                renderView.refreshRoom();
                notice("Stage effects: " + name + " (" + detail + ")", Toast.LENGTH_LONG);
            });
        }).start();
    }

    private void restoreRoomEffects() {
        final File file = effectsFile();
        final String name = prefs().getString(PREF_EFFECTS_NAME, null);
        if (name == null || !file.isFile()) return;
        new Thread(() -> {
            final String detail = loadEffectsFile(file, name);
            if (detail == null) return;
            runOnUiThread(() -> {
                effectsName = name;
                effectsDetail = detail;
                renderView.refreshRoom();
            });
        }).start();
    }

    private static String loadEffectsFile(File file, String name) {
        try (ParcelFileDescriptor pfd = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)) {
            return NativeBridge.loadRoomEffects(pfd.getFd(), name);
        } catch (Exception e) {
            return null;
        }
    }

    private File roomFile() {
        return new File(getFilesDir(), ROOM_FILE);
    }

    private boolean roomShown() {
        return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(PREF_ROOM_SHOWN, true);
    }

    private String roomDetail = "";

    private android.content.SharedPreferences prefs() {
        return getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    // ---- Viewer settings (persisted; applied to the render view).

    private static final String SET_MAX_SIDE = "set.maxSide";
    private static final String SET_FRAME_MS = "set.frameMs";
    private static final String SET_SMOOTH = "set.smooth";
    private static final String SET_UNLIT = "set.unlit";
    private static final String SET_BACKGROUND = "set.background";
    private static final String SET_SHADOWS = "set.shadows";
    private static final String SET_SPEED = "set.speed";
    private static final String SET_FAST_PREVIEW = "set.fastPreview";
    private static final String SET_MESH_LINE = "set.meshLine";
    // Opacity of the room's wireframe lines behind a model, in tenths (0-10);
    // bits 28-31 of the render flags hold it + 1.
    private static final String SET_ROOM_LINES = "set.roomLines";
    private static final String SET_ROOM_ANIM = "set.roomAnimate";
    private static final String SET_COLLISION_LINE = "set.collisionLine";
    private static final String SET_GPU = "set.gpu";
    private static final String SET_GPU_MSAA = "set.gpuMsaa";
    private static final String SET_GPU_MIPMAPS = "set.gpuMipmaps";
    private static final String SET_GPU_ANISOTROPY = "set.gpuAnisotropy";
    private static final String SET_GPU_PRESENT = "set.gpuPresent";

    private void applyViewerSettings() {
        final android.content.SharedPreferences p = prefs();
        NativeBridge.setGpuRendering(p.getBoolean(SET_GPU, true));
        NativeBridge.setGpuPresent(p.getBoolean(SET_GPU_PRESENT, true));
        applyCameraChoices();
        NativeBridge.setGpuOptions(p.getInt(SET_GPU_MSAA, 4), p.getBoolean(SET_GPU_MIPMAPS, true),
                p.getInt(SET_GPU_ANISOTROPY, 8));
        final int flags = (p.getBoolean(SET_SMOOTH, false) ? 1 << 9 : 0)
                | (p.getBoolean(SET_UNLIT, false) ? 1 << 10 : 0)
                | ((p.getInt(SET_BACKGROUND, 0) & 3) << 11)
                | ((Math.max(0, Math.min(10, p.getInt(SET_ROOM_LINES, 5))) + 1) << 28)
                | (p.getBoolean(SET_ROOM_ANIM, true) ? 1 << 15 : 0);
        renderView.applySettings(p.getInt(SET_MAX_SIDE, 720), p.getInt(SET_FRAME_MS, 33),
                p.getFloat(SET_SPEED, 1.0f), flags, p.getBoolean(SET_SHADOWS, true),
                p.getBoolean(SET_FAST_PREVIEW, true),
                p.getInt(SET_MESH_LINE, 1), p.getInt(SET_COLLISION_LINE, 1));
    }

    private void roomToggle() {
        final boolean shown = !roomShown();
        prefs().edit().putBoolean(PREF_ROOM_SHOWN, shown).apply();
        renderView.setRoomVisible(shown && roomLoaded);
        applyResourceUiState();
        notice(shown ? "Room shown: " + roomName : "Room hidden", Toast.LENGTH_SHORT);
    }

    private void roomNextSpot() {
        final int spot = NativeBridge.nextRoomSpot();
        notice("Room: floor spot " + (spot + 1) + " / " + NativeBridge.roomSpotCount(), Toast.LENGTH_SHORT);
        renderView.refreshRoom();
    }

    private void roomChoose() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_ROOM);
    }

    private void roomRemove() {
        NativeBridge.clearRoom();
        roomLoaded = false;
        roomName = "";
        roomDetail = "";
        //noinspection ResultOfMethodCallIgnored
        roomFile().delete();
        prefs().edit().remove(PREF_ROOM_NAME).apply();
        renderView.setRoomVisible(false);
        applyResourceUiState();
        notice("Room removed: plain floor", Toast.LENGTH_SHORT);
    }

    /** One setting: a label and a row of choices, the stored one highlighted. */
    private LinearLayout choiceRow(String label, String[] names, int selected,
                                   java.util.function.IntConsumer onPick) {
        LinearLayout block = new LinearLayout(this);
        block.setOrientation(LinearLayout.VERTICAL);
        block.setPadding(0, dp(10), 0, dp(4));
        TextView title = new TextView(this);
        title.setText(label);
        title.setTextColor(0xffc8ccd6);
        title.setTextSize(14f);
        block.addView(title);
        // Many choices wrap onto rows of at most five.
        final int perRow = names.length > 5 ? 5 : names.length;
        final TextView[] chips = new TextView[names.length];
        LinearLayout row = null;
        for (int i = 0; i < names.length; ++i) {
            if (i % perRow == 0) {
                row = new LinearLayout(this);
                row.setOrientation(LinearLayout.HORIZONTAL);
                final LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
                if (i > 0) rowParams.topMargin = dp(6);
                block.addView(row, rowParams);
            }
            final int index = i;
            TextView chip = new TextView(this);
            chip.setText(names[i]);
            chip.setTextSize(14f);
            chip.setGravity(Gravity.CENTER);
            chip.setPadding(dp(12), dp(8), dp(12), dp(8));
            chips[i] = chip;
            chip.setOnClickListener(v -> {
                for (int k = 0; k < chips.length; ++k) styleChip(chips[k], k == index);
                onPick.accept(index);
            });
            styleChip(chip, i == selected);
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            params.setMarginEnd(dp(6));
            row.addView(chip, params);
        }
        // Keep the last row's chips the width of the others.
        while (names.length % perRow != 0 && row != null && row.getChildCount() < perRow) {
            final View spacer = new View(this);
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, 1, 1f);
            params.setMarginEnd(dp(6));
            row.addView(spacer, params);
        }
        return block;
    }

    private void styleChip(TextView chip, boolean selected) {
        chip.setBackgroundColor(selected ? 0xff8a2432 : 0xff2a2c34);
        chip.setTextColor(selected ? 0xffffffff : 0xffb8bcc6);
    }

    private TextView sectionTitle(String text) {
        TextView title = new TextView(this);
        title.setText(text);
        title.setTextColor(0xffffffff);
        title.setTextSize(18f);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setPadding(0, dp(18), 0, dp(2));
        return title;
    }

    private TextView actionChip(String text, Runnable action) {
        TextView chip = new TextView(this);
        chip.setText(text);
        chip.setTextSize(14f);
        chip.setGravity(Gravity.CENTER);
        chip.setPadding(dp(12), dp(10), dp(12), dp(10));
        styleChip(chip, false);
        chip.setOnClickListener(v -> action.run());
        return chip;
    }

    private static int indexOf(int[] values, int value, int fallback) {
        for (int i = 0; i < values.length; ++i) if (values[i] == value) return i;
        return fallback;
    }

    /** A labelled slider from `min` to `max`; the label shows `format(value)`. */
    private LinearLayout sliderRow(String label, int min, int max, int step, int value,
                                   java.util.function.IntFunction<String> format,
                                   java.util.function.IntConsumer onChange) {
        final LinearLayout block = new LinearLayout(this);
        block.setOrientation(LinearLayout.VERTICAL);
        block.setPadding(0, dp(10), 0, dp(4));
        final TextView title = new TextView(this);
        title.setTextColor(0xffc8ccd6);
        title.setTextSize(14f);
        final int clamped = Math.max(min, Math.min(max, value));
        title.setText(label + ": " + format.apply(clamped));
        block.addView(title);
        final android.widget.SeekBar bar = new android.widget.SeekBar(this);
        bar.setMax(max - min);
        bar.setProgress(clamped - min);
        bar.setOnSeekBarChangeListener(new android.widget.SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(android.widget.SeekBar seekBar, int progress, boolean fromUser) {
                final int v = min + progress;
                title.setText(label + ": " + format.apply(v));
                if (fromUser) onChange.accept(v);
            }
            @Override public void onStartTrackingTouch(android.widget.SeekBar seekBar) { }
            @Override public void onStopTrackingTouch(android.widget.SeekBar seekBar) { }
        });
        block.addView(bar, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        return block;
    }

    private TextView hint(String text) {
        final TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(0xff8c909c);
        view.setTextSize(12f);
        view.setPadding(0, dp(2), 0, dp(4));
        return view;
    }

    /**
     * The kinds of collision record (distinct flag values) of the HITS shown
     * now, each in the colour it is drawn with.
     */
    private void addCollisionKinds(LinearLayout content) {
        final String[] kinds = NativeBridge.collisionKinds(session);
        final TextView title = new TextView(this);
        title.setTextColor(0xffc8ccd6);
        title.setTextSize(14f);
        title.setPadding(0, dp(12), 0, dp(4));
        if (kinds == null || kinds.length == 0) {
            title.setText("Collision kinds: none shown. Open a stage, a .hits file or choose a room with collision.");
            content.addView(title);
            return;
        }
        title.setText("Collision kinds in the HITS shown now: " + kinds.length
                + " (one per distinct record flags value)");
        content.addView(title);
        for (int i = 0; i < kinds.length; ++i) {
            final String[] part = kinds[i].split("\\|");
            if (part.length < 6) continue;
            final long flags = Long.parseLong(part[0]);
            final int color = 0xff000000 | Integer.parseInt(part[5]);
            final LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(0, dp(3), 0, dp(3));
            final View swatch = new View(this);
            swatch.setBackgroundColor(color);
            row.addView(swatch, new LinearLayout.LayoutParams(dp(26), dp(6)));
            final TextView text = new TextView(this);
            text.setTextColor(0xffe0e3ea);
            text.setTextSize(13f);
            text.setTypeface(Typeface.MONOSPACE);
            text.setPadding(dp(10), 0, 0, 0);
            text.setText(String.format(java.util.Locale.US,
                    "%d  0x%08X  %s records  (floor %s / wall %s / ceiling %s)",
                    i + 1, flags, part[1], part[2], part[3], part[4]));
            row.addView(text);
            content.addView(row);
        }
    }

    /** The core's collision report in the app's window: a summary, then coloured groups. */
    private void showCollisionReport(boolean attack) {
        if (session == 0) return;
        final String[] report = NativeBridge.collisionReport(session, attack);
        if (report == null || report.length < 2) return;
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(16), dp(12), dp(16), dp(16));
        TextView summary = new TextView(this);
        summary.setText(report[1]);
        summary.setTextColor(0xffe0e3ea);
        summary.setTextSize(14f);
        summary.setPadding(0, 0, 0, dp(8));
        content.addView(summary);
        for (int i = 2; i < report.length; ++i) {
            final String[] part = report[i].split("\\|", 3);
            if (part.length < 3) continue;
            final long rgb = Long.parseLong(part[0]);
            final int color = rgb < 0 ? 0xffffffff : (0xff000000 | (int) rgb);
            LinearLayout header = new LinearLayout(this);
            header.setOrientation(LinearLayout.HORIZONTAL);
            header.setGravity(Gravity.CENTER_VERTICAL);
            header.setPadding(0, dp(10), 0, dp(4));
            if (rgb >= 0) {
                final View swatch = new View(this);
                swatch.setBackgroundColor(color);
                LinearLayout.LayoutParams swatchParams = new LinearLayout.LayoutParams(dp(18), dp(18));
                swatchParams.setMarginEnd(dp(10));
                header.addView(swatch, swatchParams);
            }
            TextView heading = new TextView(this);
            heading.setText(part[1]);
            heading.setTextColor(color);
            heading.setTextSize(15f);
            heading.setTypeface(Typeface.DEFAULT_BOLD);
            header.addView(heading, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            content.addView(header);
            if (!part[2].isEmpty()) {
                TextView lines = new TextView(this);
                lines.setText(part[2]);
                lines.setTextColor(0xffc8ccd6);
                lines.setTextSize(13f);
                lines.setTextIsSelectable(true);
                lines.setPadding(rgb >= 0 ? dp(28) : 0, 0, 0, 0);
                content.addView(lines);
            }
        }
        new PanelBuilder().setTitle(report[0]).setView(content).show();
    }

    // ---- Benchmark

    private final java.util.ArrayList<String> benchmarkResults = new java.util.ArrayList<>();

    /** The settings a benchmark ran with, for its report. */
    private String benchmarkSettings() {
        final android.content.SharedPreferences p = prefs();
        final boolean gpu = p.getBoolean(SET_GPU, true);
        final StringBuilder text = new StringBuilder();
        if (gpu) {
            final int msaa = p.getInt(SET_GPU_MSAA, 4);
            final int aniso = p.getInt(SET_GPU_ANISOTROPY, 8);
            text.append("MSAA ").append(msaa == 0 ? "off" : msaa + "x")
                    .append(", mipmaps ").append(p.getBoolean(SET_GPU_MIPMAPS, true) ? "on" : "off")
                    .append(", anisotropy ").append(aniso <= 1 ? "off" : aniso + "x")
                    .append(", shown ").append(p.getBoolean(SET_GPU_PRESENT, true) ? "directly" : "through a picture")
                    .append(", ");
        }
        text.append(p.getBoolean(SET_SMOOTH, false) ? "smooth" : "pixel").append(" textures");
        if (p.getBoolean(SET_UNLIT, false)) text.append(", unlit");
        if (roomLoaded && roomShown()) text.append(", room ").append(roomName);
        return text.toString();
    }

    private void runBenchmark() {
        final TextView progress = new TextView(this);
        progress.setTextColor(0xffe6e8ee);
        progress.setTextSize(15f);
        progress.setPadding(dp(20), dp(16), dp(20), dp(8));
        progress.setText("Drawing frames as fast as possible for 10 seconds…\nKeep the app open.");
        final AlertDialog running = new PanelBuilder()
                .setTitle("Benchmark")
                .setView(progress)
                .setCancelable(false)
                .show();
        final boolean started = renderView.runBenchmark(10.0f, benchmarkSettings(), report -> {
            running.dismiss();
            benchmarkResults.add(0, report);
            showBenchmarkResults();
        });
        if (!started) {
            running.dismiss();
            notice("Open a model or a stage first", Toast.LENGTH_SHORT);
        }
    }

    private void showBenchmarkResults() {
        final StringBuilder all = new StringBuilder();
        for (int i = 0; i < benchmarkResults.size(); ++i) {
            if (i > 0) all.append("\n\n— earlier —\n");
            all.append(benchmarkResults.get(i));
        }
        final TextView text = new TextView(this);
        text.setTextColor(0xffe6e8ee);
        text.setTextSize(14f);
        text.setTextIsSelectable(true);
        text.setPadding(dp(20), dp(12), dp(20), dp(8));
        text.setText(all.toString());
        final android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        scroll.addView(text);
        new PanelBuilder()
                .setTitle("Benchmark results")
                .setView(scroll)
                .setPositiveButton("Run again", (d, w) -> runBenchmark())
                .setNeutralButton("Copy", (d, w) -> {
                    final android.content.ClipboardManager clipboard =
                            (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                    if (clipboard != null) {
                        clipboard.setPrimaryClip(android.content.ClipData.newPlainText("Benchmark", all.toString()));
                        notice("Benchmark results copied", Toast.LENGTH_SHORT);
                    }
                })
                .setNegativeButton("Close", null)
                .show();
    }

    /** The settings window (the app's one window): render quality, animation, room. */
    private void showSettingsDialog() {
        final PanelRef dialog = new PanelRef();
        final android.content.SharedPreferences p = prefs();
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(16), dp(12), dp(16), dp(24));


        final Runnable apply = this::applyViewerSettings;
        content.addView(sectionTitle("Graphics"));
        content.addView(choiceRow("Renderer", new String[]{"GPU (graphics chip)", "CPU (software)"},
                p.getBoolean(SET_GPU, true) ? 0 : 1,
                i -> { p.edit().putBoolean(SET_GPU, i == 0).apply(); apply.run(); }));
        final int[] caps = NativeBridge.gpuCapabilities();
        final int maxSamples = caps != null && caps.length > 0 ? caps[0] : 0;
        final int maxAnisotropy = caps != null && caps.length > 1 ? caps[1] : 0;
        content.addView(hint("GPU draws the model, the room, the floor, shadows and effects with OpenGL ES 3; "
                + "lines and bones are drawn on top. Wireframe and UV views always use the CPU. Now: "
                + NativeBridge.rendererInfo()
                + (maxSamples > 0 ? "\nThis chip: anti-aliasing up to " + maxSamples + "x, anisotropic filtering "
                        + (maxAnisotropy > 1 ? "up to " + maxAnisotropy + "x." : "not supported.") : "")));
        content.addView(choiceRow("Show frames (GPU)", new String[]{"Straight on the screen", "Through a picture"},
                p.getBoolean(SET_GPU_PRESENT, true) ? 0 : 1,
                i -> { p.edit().putBoolean(SET_GPU_PRESENT, i == 0).apply(); apply.run(); renderView.renderNow(); }));
        content.addView(hint("Straight on the screen: the GPU puts the frame on the display itself, without "
                + "reading it back to the CPU and handing it over again. Frames with lines, markers or bones "
                + "drawn on them, videos and pictures still go through a picture. Choose \"Through a picture\" "
                + "if the view stays black or does not change."));
        final int[] msaa = {0, 2, 4, 8};
        content.addView(choiceRow("Anti-aliasing (MSAA, GPU): smooth edges", new String[]{"Off", "2x", "4x", "8x"},
                indexOf(msaa, p.getInt(SET_GPU_MSAA, 4), 2),
                i -> { p.edit().putInt(SET_GPU_MSAA, msaa[i]).apply(); apply.run(); }));
        content.addView(choiceRow("Mipmaps (GPU): no shimmer on distant textures", new String[]{"On", "Off"},
                p.getBoolean(SET_GPU_MIPMAPS, true) ? 0 : 1,
                i -> { p.edit().putBoolean(SET_GPU_MIPMAPS, i == 0).apply(); apply.run(); }));
        final int[] aniso = {1, 2, 4, 8, 16};
        content.addView(choiceRow("Anisotropic filtering (GPU, smooth textures): sharp floors at an angle",
                new String[]{"Off", "2x", "4x", "8x", "16x"},
                indexOf(aniso, p.getInt(SET_GPU_ANISOTROPY, 8), 3),
                i -> { p.edit().putInt(SET_GPU_ANISOTROPY, aniso[i]).apply(); apply.run(); }));
        content.addView(hint("Higher anti-aliasing costs memory at large resolutions: above the chip's limit or "
                + "about 256 MB of samples the next lower level is used. The CPU renderer ignores these three."));
        final int[] sides = {360, 540, 720, 1024, 2048, 3840, 5120, 6144, 7680};
        content.addView(choiceRow("Resolution (longest side, px; 2K-8K render larger than the screen)",
                new String[]{"360", "540", "720", "1024", "2K", "4K", "5K", "6K", "8K"},
                indexOf(sides, p.getInt(SET_MAX_SIDE, 720), 2),
                i -> { p.edit().putInt(SET_MAX_SIDE, sides[i]).apply(); apply.run(); }));
        content.addView(hint("2K = 2048, 4K = 3840, 5K = 5120, 6K = 6144, 8K = 7680 px. They need a lot of memory "
                + "and time per frame; while the view moves a small preview is shown. If memory runs out the "
                + "next lower size is chosen."));
        content.addView(choiceRow("While moving (drag, flick, animation)",
                new String[]{"Fast preview (half size)", "Full quality"},
                p.getBoolean(SET_FAST_PREVIEW, true) ? 0 : 1,
                i -> { p.edit().putBoolean(SET_FAST_PREVIEW, i == 0).apply(); apply.run(); }));
        content.addView(choiceRow("Model textures", new String[]{"Pixel (original)", "Smooth"},
                p.getBoolean(SET_SMOOTH, false) ? 1 : 0,
                i -> { p.edit().putBoolean(SET_SMOOTH, i == 1).apply(); apply.run(); }));
        content.addView(choiceRow("Model lighting", new String[]{"Camera light", "Off (flat)"},
                p.getBoolean(SET_UNLIT, false) ? 1 : 0,
                i -> { p.edit().putBoolean(SET_UNLIT, i == 1).apply(); apply.run(); }));
        content.addView(choiceRow("Background", new String[]{"Dark", "Grey", "Light", "Black"},
                p.getInt(SET_BACKGROUND, 0) & 3,
                i -> { p.edit().putInt(SET_BACKGROUND, i).apply(); apply.run(); }));
        content.addView(choiceRow("Shadows when a file opens", new String[]{"On", "Off"},
                p.getBoolean(SET_SHADOWS, true) ? 0 : 1,
                i -> { p.edit().putBoolean(SET_SHADOWS, i == 0).apply(); apply.run(); }));

        final LinearLayout.LayoutParams benchParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        benchParams.topMargin = dp(10);
        content.addView(actionChip("Benchmark: maximum FPS with these settings (10 s)", () -> {
            dialog.dismiss();
            runBenchmark();
        }), benchParams);
        content.addView(hint("Draws full-size frames back to back for 10 seconds while the camera turns "
                + "(and the motion plays, if one is playing), without waiting for the screen. Change a "
                + "setting and run again to compare; results stay listed until the app closes."));

        content.addView(sectionTitle("Graphics: lines and overlays"));
        final int[] widths = {1, 2, 3, 4, 6};
        final String[] widthNames = {"1", "2", "3", "4", "6"};
        content.addView(choiceRow("Mesh line width (wireframe, room meshes, bones; px at 720)", widthNames,
                indexOf(widths, p.getInt(SET_MESH_LINE, 1), 0),
                i -> { p.edit().putInt(SET_MESH_LINE, widths[i]).apply(); apply.run(); }));
        content.addView(choiceRow("Collision line width (HITS, attack shapes; px at 720)", widthNames,
                indexOf(widths, p.getInt(SET_COLLISION_LINE, 1), 0),
                i -> { p.edit().putInt(SET_COLLISION_LINE, widths[i]).apply(); apply.run(); }));
        content.addView(sliderRow("Room mesh lines behind a model (wireframe mode): opacity", 0, 10, 10,
                p.getInt(SET_ROOM_LINES, 5), value -> value * 10 + "%  " + (value == 0 ? "(hidden)" : ""),
                value -> { p.edit().putInt(SET_ROOM_LINES, value).apply(); apply.run(); }));
        addCollisionKinds(content);

        content.addView(sectionTitle("Animation"));
        final int[] frames = {50, 33, 16};
        content.addView(choiceRow("Animation frame rate", new String[]{"20 fps", "30 fps", "60 fps"},
                indexOf(frames, p.getInt(SET_FRAME_MS, 33), 1),
                i -> { p.edit().putInt(SET_FRAME_MS, frames[i]).apply(); apply.run(); }));
        final float[] speeds = {0.25f, 0.5f, 1.0f, 2.0f};
        int speedIndex = 2;
        for (int i = 0; i < speeds.length; ++i) {
            if (Math.abs(speeds[i] - p.getFloat(SET_SPEED, 1.0f)) < 0.01f) speedIndex = i;
        }
        content.addView(choiceRow("Playback speed", new String[]{"¼×", "½×", "1×", "2×"},
                speedIndex, i -> { p.edit().putFloat(SET_SPEED, speeds[i]).apply(); apply.run(); }));

        content.addView(sectionTitle("Room"));
        TextView status = new TextView(this);
        status.setTextColor(0xffc8ccd6);
        status.setTextSize(14f);
        status.setPadding(0, dp(6), 0, dp(6));
        status.setText(roomLoaded
                ? (roomDetail.isEmpty() ? roomName : roomDetail)
                : "No room: models stand on the plain floor. Choose a stage (st*.pac or .scm) to show "
                        + "every model inside it.");
        content.addView(status);
        LinearLayout roomActions = new LinearLayout(this);
        roomActions.setOrientation(LinearLayout.VERTICAL);
        final java.util.function.BiConsumer<String, Runnable> addAction = (text, action) -> {
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            params.topMargin = dp(6);
            roomActions.addView(actionChip(text, action), params);
        };
        addAction.accept(roomLoaded ? "Replace stage…" : "Choose stage…", () -> {
            dialog.dismiss();
            roomChoose();
        });
        if (roomLoaded) {
            content.addView(choiceRow("Show the room", new String[]{"On", "Off"}, roomShown() ? 0 : 1,
                    i -> { if ((i == 0) != roomShown()) roomToggle(); }));
            content.addView(choiceRow("Room animation (sky clouds, stage effects)", new String[]{"On", "Off"},
                    p.getBoolean(SET_ROOM_ANIM, true) ? 0 : 1,
                    i -> { p.edit().putBoolean(SET_ROOM_ANIM, i == 0).apply(); apply.run(); }));
            if (NativeBridge.roomCollisionSourceCount() > 0) {
                content.addView(choiceRow("HITS room collision", new String[]{"On", "Off"},
                        renderView.isRoomCollisionVisible() ? 0 : 1,
                        i -> {
                            if (i == 0 && !roomShown()) {
                                prefs().edit().putBoolean(PREF_ROOM_SHOWN, true).apply();
                            }
                            renderView.setRoomCollisionVisible(i == 0);
                            applyResourceUiState();
                        }));
            }
            if (NativeBridge.roomSpotCount() > 1) addAction.accept("Next floor spot", this::roomNextSpot);
            addAction.accept("Remove room", () -> { roomRemove(); dialog.dismiss(); });
        }
        content.addView(roomActions);
        // Stage effects: the layout's effects (burning drums) play from st*_effect.pac.
        final TextView effectsStatus = new TextView(this);
        effectsStatus.setTextColor(0xffc8ccd6);
        effectsStatus.setTextSize(14f);
        effectsStatus.setPadding(0, dp(12), 0, dp(4));
        effectsStatus.setText(effectsName.isEmpty()
                ? "Stage effects: none. Choose the stage's effect bank (st*_effect.pac) to play the effects its "
                        + "layout keeps on objects."
                : "Stage effects: " + effectsName + " (" + effectsDetail + ")");
        content.addView(effectsStatus);
        final LinearLayout effectsActions = new LinearLayout(this);
        effectsActions.setOrientation(LinearLayout.VERTICAL);
        final LinearLayout.LayoutParams effectsParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        effectsParams.topMargin = dp(6);
        effectsActions.addView(actionChip(effectsName.isEmpty() ? "Choose stage effects…" : "Replace stage effects…",
                () -> { dialog.dismiss(); roomEffectsChoose(); }), effectsParams);
        if (!effectsName.isEmpty()) {
            effectsActions.addView(actionChip("Remove stage effects", () -> { roomEffectsRemove(); dialog.dismiss(); }),
                    effectsParams);
        }
        content.addView(effectsActions);

        dialog.dialog = showPanel("Settings", content, null, null, null);
    }

    // ---- Gestures (DmcRenderView): the activity side and the Gestures window.

    private static final String PREF_GESTURES = "gestures.mask";
    private static final String PREF_GESTURE_DOLLY = "gestures.dollyAdded";
    private static final String PREF_GESTURE_CAMERA_V2 = "gestures.closePanTurnAdded";

    private final DmcRenderView.GestureListener gestureListener = new DmcRenderView.GestureListener() {
        @Override public void onStepMotion(int direction) {
            final ArrayList<MotionEntry> motions = motionEntries();
            if (session == 0 || motions.isEmpty()) {
                notice("No animations here", Toast.LENGTH_SHORT);
                return;
            }
            final int n = motions.size();
            final int next = selectedMotionIndex < 0
                    ? (direction > 0 ? 0 : n - 1)
                    : ((selectedMotionIndex + direction) % n + n) % n;
            selectMotion(next);
            if (motionScroll != null && next < motionCards.size()) {
                final View card = motionCards.get(next);
                motionScroll.post(() -> motionScroll.smoothScrollTo(
                        Math.max(0, card.getLeft() - dp(24)), 0));
            }
        }

        @Override public void onStepVariant(int direction) {
            final String[] positions = (isRootScene() && assembledPacUri != null)
                    ? NativeBridge.archiveVariantNames(displayName(assembledPacUri))
                    : null;
            if (positions == null || positions.length < 2) {
                notice("No other positions here", Toast.LENGTH_SHORT);
                return;
            }
            final int n = positions.length;
            enemyVariant = ((enemyVariant + direction) % n + n) % n;
            assembleWithAddedPacs(null);
            notice(positions[enemyVariant], Toast.LENGTH_SHORT);
        }

        @Override public void onToggleUi() {
            final View[] bars = {headerBar, variantScroll, effectModeScroll, motionScroll, toolScroll, cameraPanel};
            uiHidden = !uiHidden;
            for (int i = 0; i < bars.length; ++i) {
                if (bars[i] == null) continue;
                if (uiHidden) {
                    barVisibility[i] = bars[i].getVisibility();
                    bars[i].setVisibility(View.GONE);
                } else {
                    bars[i].setVisibility(barVisibility[i]);
                }
            }
            if (!uiHidden) {
                refreshMotionStrip();
                refreshVariantBar();
                refreshEffectModeBar();
            }
            notice(uiHidden ? "Full view — swipe down from the top to bring the bars back" : "Bars shown",
                    Toast.LENGTH_SHORT);
        }

        @Override public void onTakePicture() {
            takePicture();
        }

        @Override public void onCameraStep(String settingId, int step) {
            stepCameraOption(settingId, step);
        }

        @Override public void onCameraTaken() {
            // A finger took a cutscene's camera: camera mode's free camera starts at its eye.
            freeCamera();
        }

        @Override public void onCameraLevel() {
            NativeBridge.cameraLevel();
            NativeBridge.gyroReset();
            renderView.renderNow();
            notice("Level", Toast.LENGTH_SHORT);
        }

        @Override public void onGestureNotice(String text) {
            notice(text, Toast.LENGTH_SHORT);
        }

        @Override public void onActionOutcome(ActionOutcome outcome) {
            handleOutcome(outcome);
        }
    };

    /** Plays one sound the core decoded; the platform player is the only thing done here. */
    private void playSound(int index) {
        stopSound();
        if (session == 0) return;
        final SoundPcm pcm = NativeBridge.soundPcm(session, index);
        if (pcm == null) return;
        if (pcm.samples.length == 0 || pcm.sampleRate <= 0) {
            if (!pcm.message.isEmpty()) notice(pcm.message, Toast.LENGTH_SHORT);
            return;
        }
        try {
            final android.media.AudioTrack track = new android.media.AudioTrack.Builder()
                    .setAudioFormat(new android.media.AudioFormat.Builder()
                            .setEncoding(android.media.AudioFormat.ENCODING_PCM_16BIT)
                            .setSampleRate(pcm.sampleRate)
                            .setChannelMask(android.media.AudioFormat.CHANNEL_OUT_MONO)
                            .build())
                    .setTransferMode(android.media.AudioTrack.MODE_STATIC)
                    .setBufferSizeInBytes(pcm.samples.length * 2)
                    .build();
            track.write(pcm.samples, 0, pcm.samples.length);
            track.setNotificationMarkerPosition(pcm.samples.length);
            track.setPlaybackPositionUpdateListener(new android.media.AudioTrack.OnPlaybackPositionUpdateListener() {
                @Override public void onMarkerReached(android.media.AudioTrack t) {
                    runOnUiThread(() -> {
                        if (soundTrack == t) {
                            stopSound();
                            applyResourceUiState();
                        }
                    });
                }
                @Override public void onPeriodicNotification(android.media.AudioTrack t) { }
            });
            soundTrack = track;
            soundIndex = index;
            track.play();
            if (!pcm.message.isEmpty()) notice(pcm.message, Toast.LENGTH_SHORT);
        } catch (Exception error) {
            soundTrack = null;
        }
    }

    private void stopSound() {
        final android.media.AudioTrack track = soundTrack;
        soundTrack = null;
        if (track == null) return;
        try {
            track.stop();
        } catch (Exception ignored) {
        }
        track.release();
    }

    /** A menu the core wrote: its title, line and items; the picked item's id goes back. */
    private void showNativeMenu(NativeMenu menu, java.util.function.IntConsumer onPick) {
        if (menu == null || menu.ids.length == 0) return;
        final PanelBuilder builder = new PanelBuilder().setTitle(menu.title);
        if (menu.message.isEmpty()) {
            builder.setItems(menu.labels, (d, which) -> onPick.accept(menu.ids[which]));
        } else {
            // A message and a list cannot share an AlertDialog: the items go to buttons.
            builder.setMessage(menu.message);
            if (menu.ids.length > 0) builder.setPositiveButton(menu.labels[0], (d, w) -> onPick.accept(menu.ids[0]));
            if (menu.ids.length > 1) builder.setNeutralButton(menu.labels[1], (d, w) -> onPick.accept(menu.ids[1]));
            if (menu.ids.length > 2) builder.setNegativeButton(menu.labels[2], (d, w) -> onPick.accept(menu.ids[2]));
        }
        builder.show();
    }

    /** Platform step of the stage-config action: the system file picker. */
    private void pickStageConfig() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_STAGE_CONFIG);
    }

    /** Platform step of the cutscene-sound action: the system file picker. */
    private void pickCutsceneSound() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_CUTSCENE_SOUND);
    }

    /** The picked sound goes to the native action as a descriptor. */
    private void loadCutsceneSound(Uri uri) {
        ActionOutcome outcome = null;
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
            outcome = NativeBridge.loadCutsceneSound(session, pfd == null ? -1 : pfd.getFd(), displayName(uri));
        } catch (Exception ignored) {
            outcome = NativeBridge.loadCutsceneSound(session, -1, displayName(uri));
        }
        handleOutcome(outcome);
        // Playing already: the new sound joins from where the timeline is.
        if (renderView.isMotionPlaying()) renderView.resumeMotionAt(renderView.motionFrame());
    }

    /** The picked file goes to the native load action as a descriptor. */
    private void loadStageConfig(Uri uri) {
        ActionOutcome outcome = null;
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
            outcome = NativeBridge.loadStageConfig(pfd == null ? -1 : pfd.getFd(), displayName(uri));
        } catch (Exception ignored) {
            outcome = NativeBridge.loadStageConfig(-1, displayName(uri));
        }
        handleOutcome(outcome);
    }

    /** Carries out the one platform step a native action names, and shows its line. */
    private void handleOutcome(ActionOutcome outcome) {
        if (outcome == null) return;
        if (!outcome.message.isEmpty()) notice(outcome.message, Toast.LENGTH_LONG);
        switch (outcome.step) {
            case ActionOutcome.STEP_PICK_STAGE_CONFIG:
                pickStageConfig();
                break;
            case ActionOutcome.STEP_SHOW_MARKER_CARD:
                showMarkerCard(outcome.marker);
                break;
            case ActionOutcome.STEP_OPEN_SESSION:
                if (outcome.session != 0) navigateToSession(outcome.session, outcome.title);
                break;
            case ActionOutcome.STEP_LEAVE_FLY:
                renderView.setFlyMode(false);
                break;
            case ActionOutcome.STEP_PICK_CUTSCENE_SOUND:
                pickCutsceneSound();
                break;
            case ActionOutcome.STEP_RESET_VIEW:
                renderView.resetView();
                break;
            default:
                break;
        }
        renderView.renderNow();
        applyResourceUiState();
    }

    /** The held marker's card as the core wrote it; its buttons go back to the core. */
    private void showMarkerCard(int marker) {
        final MarkerCard card = NativeBridge.markerCard(marker);
        if (card == null) return;
        final StringBuilder body = new StringBuilder();
        for (int i = 0; i < card.names.length && i < card.values.length; ++i) {
            if (body.length() > 0) body.append('\n');
            body.append(card.names[i]).append(": ").append(card.values[i]);
        }
        final boolean[] answered = {false};
        final PanelBuilder builder = new PanelBuilder()
                .setTitle(card.title)
                .setMessage(body.toString());
        final NativeMenu buttons = card.actions;
        if (buttons != null) {
            for (int i = 0; i < buttons.ids.length && i < 2; ++i) {
                final int id = buttons.ids[i];
                final android.content.DialogInterface.OnClickListener click = (d, w) -> {
                    answered[0] = true;
                    handleOutcome(NativeBridge.runMarkerCardAction(marker, id));
                };
                if (i == 0) builder.setPositiveButton(buttons.labels[i], click);
                else if (i == buttons.ids.length - 1) builder.setNegativeButton(buttons.labels[i], click);
                else builder.setNeutralButton(buttons.labels[i], click);
            }
        }
        builder.setOnDismissListener(d -> {
            if (!answered[0]) handleOutcome(NativeBridge.runMarkerCardAction(marker, 0));
        });
        builder.show();
    }

    // ---- Photo and video camera: the core plans every picture and video
    // (spider/camera.h); this shell keeps the chosen options, counts the
    // timer down, saves the files and runs the encoder.

    // A choice is kept by its option's name, so a dial that gains values
    // (f/0.95, ISO 25 ...) keeps what was chosen.
    private static final String PREF_CAMERA = "camera_option_";
    private static final String PREF_POLARIZER = "camera_polarizer";
    private String[][] cameraRows;

    // A camera setting row: {id, section, label, hint, start, options...}.
    private static final int CAMERA_START_AT = 4;
    private static final int CAMERA_OPTIONS_AT = 5;

    private static String[] cameraOptions(String[] row) {
        return java.util.Arrays.copyOfRange(row, CAMERA_OPTIONS_AT, row.length);
    }

    /**
     * Keeps a camera setting's option chosen by hand, and what the core says goes with it (in Auto,
     * turning the ISO, the shutter or the aperture goes to Manual); the caller applies the choices.
     */
    private void storeCameraOption(String[] row, int option) {
        final String[] options = cameraOptions(row);
        if (option < 0 || option >= options.length) return;
        final String[][] rows = cameraRows();
        final android.content.SharedPreferences.Editor editor = prefs().edit();
        editor.putString(PREF_CAMERA + row[0], options[option]);
        int index = -1;
        for (int i = 0; i < rows.length; ++i) if (rows[i][0].equals(row[0])) index = i;
        final String[] more = index >= 0 ? NativeBridge.cameraFollowUps(index, option) : null;
        if (more != null && more.length > 0) {
            for (int k = 1; k < more.length; ++k) {
                final String[] pair = more[k].split("\t");
                if (pair.length != 2) continue;
                try {
                    final int at = Integer.parseInt(pair[0]);
                    final int choice = Integer.parseInt(pair[1]);
                    final String[] other = at >= 0 && at < rows.length ? cameraOptions(rows[at]) : new String[0];
                    if (choice >= 0 && choice < other.length) editor.putString(PREF_CAMERA + rows[at][0], other[choice]);
                } catch (NumberFormatException ignored) {
                    // A malformed row changes nothing.
                }
            }
            if (!more[0].isEmpty()) notice(more[0], Toast.LENGTH_LONG);
        }
        editor.apply();
    }

    /** The core's camera settings: {id, section, label, hint, start, options...} per row. */
    private String[][] cameraRows() {
        if (cameraRows == null) {
            final String[] rows = NativeBridge.cameraSettings();
            cameraRows = new String[rows == null ? 0 : rows.length][];
            for (int i = 0; i < cameraRows.length; ++i) cameraRows[i] = rows[i].split("\t", -1);
        }
        return cameraRows;
    }

    private int cameraChoice(String[] row) {
        if (row[0].equals(CAMERA_MODE_ID)) return cameraModeOn ? 1 : 0;
        final int start = Integer.parseInt(row[CAMERA_START_AT]);
        final String name = prefs().getString(PREF_CAMERA + row[0], null);
        if (name != null) {
            for (int i = CAMERA_OPTIONS_AT; i < row.length; ++i) if (row[i].equals(name)) return i - CAMERA_OPTIONS_AT;
        }
        return start;
    }

    // Camera mode is never kept: the app and every opened file start in the
    // standard view; the 📷 button (or the setting) turns it on for now.
    private static final String CAMERA_MODE_ID = "finder_mode";
    private boolean cameraModeOn;

    private void applyCameraChoices() {
        final String[][] rows = cameraRows();
        final int[] choices = new int[rows.length];
        for (int i = 0; i < rows.length; ++i) {
            choices[i] = rows[i][0].equals(CAMERA_MODE_ID) ? (cameraModeOn ? 1 : 0) : cameraChoice(rows[i]);
        }
        NativeBridge.setCameraChoices(choices);
        NativeBridge.setCameraPolarizer(prefs().getFloat(PREF_POLARIZER, 0.0f));
        if (cameraPanel != null) setCameraUi(NativeBridge.cameraMode());
        if (cameraPanel != null) updateGyro();
        if (cameraPanel != null) updateWalk(false);
        updateScreenOrientation();
    }

    // The orientation the 📱 button pinned (ActivityInfo), or -1: the phone decides.
    private int pinnedOrientation = -1;

    /** A pinned orientation, else the current one held while the phone aims the camera, else the sensor's. */
    private void updateScreenOrientation() {
        final int want = pinnedOrientation >= 0 ? pinnedOrientation
                : NativeBridge.cameraHoldsOrientation() ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED
                : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
        if (getRequestedOrientation() != want) setRequestedOrientation(want);
    }

    /** The 📱 button: the app turns to the other orientation and stays there. */
    private void rotateScreen() {
        final boolean landscape = getResources().getConfiguration().orientation
                == android.content.res.Configuration.ORIENTATION_LANDSCAPE;
        pinnedOrientation = landscape ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        updateScreenOrientation();
        notice(landscape ? "📱 Portrait, held; hold the button to let the phone turn the app again"
                         : "📱 Landscape, held; hold the button to let the phone turn the app again",
                Toast.LENGTH_SHORT);
    }

    /** Holding 📱: the phone turns the app again (unless the gyroscope or walking holds it). */
    private void releaseScreenRotation() {
        pinnedOrientation = -1;
        updateScreenOrientation();
        notice(NativeBridge.cameraHoldsOrientation()
                        ? "📱 Rotation follows the phone again once the gyroscope and walking are off"
                        : "📱 Rotation follows the phone", Toast.LENGTH_SHORT);
    }

    /**
     * Turning the phone no longer recreates the activity (the manifest takes the
     * change): the session, the camera and the bars stay as they are; the view
     * only lays itself out again at the new size.
     */
    @Override public void onConfigurationChanged(android.content.res.Configuration config) {
        super.onConfigurationChanged(config);
        // The gyroscope's readings are taken against the screen: start again from here.
        NativeBridge.gyroReset();
        if (renderView != null) renderView.renderNow();
    }

    // ---- Camera mode: the camera's own bar in place of the scene's two
    // bottom rows; the 📷 button beside ⋮ goes in and back out, so the scene
    // can be set up, shot, set up again.

    private void buildCameraPanel(LinearLayout root) {
        cameraPanel = new LinearLayout(this);
        cameraPanel.setOrientation(LinearLayout.VERTICAL);
        cameraPanel.setBackgroundColor(0xff111216);
        cameraPanel.setVisibility(View.GONE);

        final HorizontalScrollView dialScroll = new HorizontalScrollView(this);
        dialScroll.setHorizontalScrollBarEnabled(false);
        cameraDialBar = new LinearLayout(this);
        cameraDialBar.setOrientation(LinearLayout.HORIZONTAL);
        cameraDialBar.setPadding(dp(8), dp(6), dp(8), dp(2));
        dialScroll.addView(cameraDialBar, new HorizontalScrollView.LayoutParams(
                HorizontalScrollView.LayoutParams.WRAP_CONTENT, HorizontalScrollView.LayoutParams.WRAP_CONTENT));
        cameraPanel.addView(dialScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        cameraAdjust = new LinearLayout(this);
        cameraAdjust.setOrientation(LinearLayout.VERTICAL);
        cameraAdjust.setPadding(dp(14), dp(4), dp(14), dp(10));
        // At most a little over half the view tall; it scrolls past that.
        cameraAdjustScroll = new ScrollView(this) {
            @Override protected void onMeasure(int widthSpec, int heightSpec) {
                final View parent = (View) getParent();
                final int cap = parent != null && parent.getHeight() > 0
                        ? Math.round(parent.getHeight() * 0.55f) : dp(320);
                super.onMeasure(widthSpec, MeasureSpec.makeMeasureSpec(cap, MeasureSpec.AT_MOST));
            }
        };
        cameraAdjustScroll.setBackgroundColor(0xee111216);
        cameraAdjustScroll.setVisibility(View.GONE);
        cameraAdjustScroll.addView(cameraAdjust);
        if (viewportFrame != null) {
            viewportFrame.addView(cameraAdjustScroll, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP));
            if (noticeView != null) noticeView.bringToFront();  // notices stay readable over it
        }

        final LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setGravity(Gravity.CENTER);
        actions.setPadding(dp(8), dp(4), dp(8), dp(8));
        final Button settings = makeSquareButton("🎛", "Camera settings", 20f);
        settings.setOnClickListener(v -> showCameraSettings());
        addToolButton(actions, settings);
        cameraPlayButton = makeSquareButton("▶", "Play / pause the scene", 20f);
        cameraPlayButton.setOnClickListener(v -> toggleCameraPlay());
        addToolButton(actions, cameraPlayButton);
        final Button shutter = makeSquareButton("📷", "Take a picture", 28f);
        shutter.setOnClickListener(v -> takePicture());
        final LinearLayout.LayoutParams big = new LinearLayout.LayoutParams(
                dp(TOOL_SIZE_DP + 20), dp(TOOL_SIZE_DP + 20));
        big.setMarginStart(dp(14));
        big.setMarginEnd(dp(6));
        actions.addView(shutter, big);
        cameraRecordButton = makeSquareButton("🎥", "Record a video", 28f);
        cameraRecordButton.setOnClickListener(v -> {
            if (recorder == null) startRecording(); else stopRecording();
        });
        final LinearLayout.LayoutParams big2 = new LinearLayout.LayoutParams(
                dp(TOOL_SIZE_DP + 20), dp(TOOL_SIZE_DP + 20));
        big2.setMarginStart(dp(6));
        big2.setMarginEnd(dp(14));
        actions.addView(cameraRecordButton, big2);
        final Button reset = makeSquareButton("🔄", "Reset view", 20f);
        reset.setOnClickListener(v -> {
            renderView.resetView();
            freeCamera();
        });
        addToolButton(actions, reset);
        cameraGyroButton = makeSquareButton("🧭", "Gyroscope: turn the phone to turn the camera", 20f);
        cameraGyroButton.setOnClickListener(v -> toggleGyro());
        addToolButton(actions, cameraGyroButton);
        cameraWalkButton = makeSquareButton("🚶", "Walk: ARCore follows your steps", 20f);
        cameraWalkButton.setOnClickListener(v -> toggleWalk());
        addToolButton(actions, cameraWalkButton);
        final Button drone = makeDroneButton();
        drone.setOnClickListener(v -> toggleDrone());
        addToolButton(actions, drone);
        final Button rotate = makeSquareButton("📱", "Turn the app (hold: follow the phone)", 20f);
        rotate.setOnClickListener(v -> rotateScreen());
        rotate.setOnLongClickListener(v -> {
            releaseScreenRotation();
            return true;
        });
        addToolButton(actions, rotate);
        final Button help = makeSquareButton("ℹ", "Gestures and gyroscope", 20f);
        help.setOnClickListener(v -> showCameraHelp());
        addToolButton(actions, help);
        final Button back = makeSquareButton("↩", "Back to the scene", 20f);
        back.setOnClickListener(v -> toggleCameraMode());
        addToolButton(actions, back);
        // The actions scroll sideways when the screen is narrow.
        final HorizontalScrollView actionScroll = new HorizontalScrollView(this);
        actionScroll.setHorizontalScrollBarEnabled(false);
        actionScroll.setFillViewport(true);
        actionScroll.addView(actions, new HorizontalScrollView.LayoutParams(
                HorizontalScrollView.LayoutParams.WRAP_CONTENT, HorizontalScrollView.LayoutParams.WRAP_CONTENT));
        cameraPanel.addView(actionScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        root.addView(cameraPanel, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
    }

    /** The drone (the fly camera with its sticks) on or off; the same button goes back to the orbit. */
    private void toggleDrone() {
        // In camera mode the camera is already free: the drone adds the sticks.
        final boolean fly = !renderView.isDroneFlying();
        final boolean ok = cameraUi ? renderView.setFlyMode(true, fly) : renderView.setFlyMode(fly);
        if (!ok) {
            notice("No drone here", Toast.LENGTH_SHORT);
            return;
        }
        notice(fly ? "Drone: left thumb flies (forward / back, sideways), right thumb looks (turn, tilt); "
                        + "fly forward while looking up or down to climb or descend"
                   : cameraUi ? "Free camera: one finger turns it where it stands" : "Orbit camera",
                Toast.LENGTH_LONG);
        applyResourceUiState();
    }

    /** Camera mode's camera: free in space at its own eye (fly without the sticks), when the view has one. */
    private void freeCamera() {
        if (!cameraUi || session == 0 || renderView.isFlyMode() || !blackWidowState.canFly) return;
        // The cutscene's cuts keep driving the view until a finger takes it.
        if (NativeBridge.cameraFollowsCuts(session)) return;
        if (renderView.setFlyMode(true, false)) applyResourceUiState();
    }

    /** Camera mode in or out (the core's setting); the bars follow it. */
    private void toggleCameraMode() {
        final boolean on = !NativeBridge.cameraMode();
        setCameraOption(CAMERA_MODE_ID, on ? 1 : 0);
        renderView.renderNow();
        notice(on ? "📷 Camera mode — the 📷 button above goes back to the scene" : "Scene", Toast.LENGTH_SHORT);
    }

    private final Runnable cameraDialTick = new Runnable() {
        @Override public void run() {
            if (!cameraUi) return;
            updateCameraDials();
            renderView.postDelayed(this, 500L);
        }
    };

    private void setCameraUi(boolean on) {
        final boolean changed = on != cameraUi;
        cameraUi = on;
        if (cameraModeButton != null) {
            cameraModeButton.setActivated(on);
            cameraModeButton.setTextColor(on ? 0xffff5050 : Color.WHITE);
        }
        if (cameraPanel == null) return;
        cameraPanel.setVisibility(on && !uiHidden ? View.VISIBLE : View.GONE);
        toolScroll.setVisibility(on || uiHidden ? View.GONE : View.VISIBLE);
        if (on) {
            motionScroll.setVisibility(View.GONE);
            freeCamera();
            refreshCameraDials();
            updateCameraButtons();
            renderView.removeCallbacks(cameraDialTick);
            renderView.postDelayed(cameraDialTick, 500L);
        } else if (changed) {
            // Back to the scene: the orbit again (the drone keeps flying).
            if (renderView.isFlyMode() && !renderView.isDroneFlying()) {
                renderView.setFlyMode(false);
                applyResourceUiState();
            }
            cameraDialOpen = null;
            if (cameraAdjustScroll != null) cameraAdjustScroll.setVisibility(View.GONE);
            refreshMotionStrip();
        }
    }

    /** The dials as the core lists them; a tapped one opens its settings under the row. */
    private void refreshCameraDials() {
        if (cameraDialBar == null) return;
        cameraDialBar.removeAllViews();
        final String[] dials = NativeBridge.cameraDials();
        if (dials == null) return;
        for (final String dial : dials) {
            final String[] parts = dial.split("\t", -1);
            if (parts.length < 3) continue;
            // Every dial the same square: its name over its value.
            final TextView chip = squareTile(parts[1] + "\n" + parts[2]);
            chip.setTag(parts[0]);
            styleChip(chip, parts[0].equals(cameraDialOpen));
            chip.setOnClickListener(v -> openCameraDial(parts[0]));
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(DIAL_TILE_DP), dp(DIAL_TILE_DP));
            params.setMarginEnd(dp(6));
            cameraDialBar.addView(chip, params);
        }
    }

    /** New values on the dials without rebuilding them (a turned polarizer, a picked option). */
    private void updateCameraDials() {
        if (cameraDialBar == null) return;
        final String[] dials = NativeBridge.cameraDials();
        if (dials == null || dials.length != cameraDialBar.getChildCount()) {
            refreshCameraDials();
            return;
        }
        for (int i = 0; i < dials.length; ++i) {
            final String[] parts = dials[i].split("\t", -1);
            final TextView chip = (TextView) cameraDialBar.getChildAt(i);
            if (parts.length < 3) continue;
            final String text = parts[1] + "\n" + parts[2];
            if (!text.contentEquals(chip.getText())) chip.setText(text);
            styleChip(chip, parts[0].equals(cameraDialOpen));
        }
        updateCameraButtons();
    }

    private void updateCameraButtons() {
        if (cameraPlayButton != null) cameraPlayButton.setText(renderView.isMotionPlaying() ? "⏸" : "▶");
        if (cameraRecordButton != null) cameraRecordButton.setText(recorder != null ? "⏹" : "🎥");
        if (cameraGyroButton != null) {
            final boolean on = NativeBridge.gyroActive();
            cameraGyroButton.setActivated(on);
            cameraGyroButton.setAlpha(on ? 1.0f : 0.55f);
        }
        if (cameraWalkButton != null) {
            final boolean on = NativeBridge.walkActive();
            cameraWalkButton.setActivated(on);
            cameraWalkButton.setAlpha(on ? 1.0f : 0.55f);
        }
    }

    /** A camera setting one option up or down (by its id), as the dials and gestures turn it. */
    private void stepCameraOption(String id, int step) {
        for (final String[] row : cameraRows()) {
            if (!row[0].equals(id)) continue;
            final int count = row.length - CAMERA_OPTIONS_AT;
            final int next = Math.max(0, Math.min(count - 1, cameraChoice(row) + step));
            notice(row[2] + ": " + row[CAMERA_OPTIONS_AT + next], Toast.LENGTH_SHORT);
            storeCameraOption(row, next);
            applyCameraChoices();
            renderView.renderNow();
            updateCameraDials();
            return;
        }
    }

    private void setCameraOption(String id, int option) {
        if (id.equals(CAMERA_MODE_ID)) {
            cameraModeOn = option == 1;
            applyCameraChoices();
            return;
        }
        for (final String[] row : cameraRows()) {
            if (row[0].equals(id)) storeCameraOption(row, option);
        }
        applyCameraChoices();
    }

    /** Walking on or off (the core's setting); it works in camera mode and flies the drone. */
    private void toggleWalk() {
        final boolean on = !NativeBridge.walkActive();
        if (on && !prefs().getBoolean(PREF_AR_NOTICE, false)) {
            // ARCore's terms: say what it is and whose it is before it runs.
            new PanelBuilder().setTitle("Walking with ARCore")
                    .setMessage("Walking moves the camera by your own steps. It uses Google Play Services for AR "
                            + "(ARCore), provided by Google LLC and governed by the Google Privacy Policy. "
                            + "ARCore watches through the phone's camera to follow its position; the picture is "
                            + "never shown, kept or sent by DMC Native Reader. The camera permission is asked once.")
                    .setPositiveButton("Walk", (d, w) -> {
                        prefs().edit().putBoolean(PREF_AR_NOTICE, true).apply();
                        startWalking();
                    })
                    .setNegativeButton("Not now", null)
                    .show();
            return;
        }
        if (on) startWalking(); else {
            setCameraOption("walk_mode", 0);
            notice("Walking off", Toast.LENGTH_SHORT);
        }
        updateCameraButtons();
    }

    private void startWalking() {
        setCameraOption("walk_mode", 1);
        updateWalk(true);
        updateCameraButtons();
    }

    /** Follows the phone's steps while walking works (camera mode, walking on, the app in front). */
    private void updateWalk(boolean userAsked) {
        final boolean want = cameraUi && NativeBridge.walkActive() && activityResumed && session != 0;
        if (!want) {
            if (arWalker != null) arWalker.stop();
            return;
        }
        if (checkSelfPermission(android.Manifest.permission.CAMERA)
                != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            if (userAsked) requestPermissions(new String[]{android.Manifest.permission.CAMERA}, REQUEST_CAMERA_FOR_WALK);
            return;
        }
        final String unavailable = ArWalker.unavailable(this);
        if (unavailable != null) {
            notice(unavailable, Toast.LENGTH_LONG);
            setCameraOption("walk_mode", 0);
            updateCameraButtons();
            return;
        }
        // Walking flies the drone: its eye is what the steps move.
        if (!renderView.isFlyMode()) {
            if (!renderView.setFlyMode(true, false)) {
                notice("Walking needs a camera free in space, and this view has none", Toast.LENGTH_LONG);
                return;
            }
            applyResourceUiState();
        }
        if (arWalker == null) {
            arWalker = new ArWalker(this, new ArWalker.Listener() {
                @Override public void onStep(float forward, float right, float up) {
                    if (NativeBridge.walk(renderView.viewYaw(), forward, right, up)) renderView.turnBy(0.0f, 0.0f);
                }

                @Override public void onTrackingState(String text) {
                    notice(text, Toast.LENGTH_SHORT);
                }
            });
        }
        arWalker.start(userAsked);
    }

    @Override public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode != REQUEST_CAMERA_FOR_WALK) return;
        if (results.length > 0 && results[0] == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            updateWalk(true);
        } else {
            notice("Walking needs the camera (ARCore follows your steps through it)", Toast.LENGTH_LONG);
            setCameraOption("walk_mode", 0);
            updateCameraButtons();
        }
    }

    /** The gyroscope on or off (the core's setting); it works in camera mode. */
    private void toggleGyro() {
        final boolean on = !NativeBridge.gyroActive();
        setCameraOption("gyro_mode", on ? 1 : 0);
        notice(on ? "🧭 Gyroscope: turn the phone to turn the camera; two-finger tap levels it"
                  : "Gyroscope off", Toast.LENGTH_SHORT);
        updateCameraButtons();
    }

    // The phone's orientation (game rotation vector: no compass jumps) as the
    // camera's heading, elevation and roll for the core, with the screen's
    // own rotation taken out.
    private final android.hardware.SensorEventListener gyroListener = new android.hardware.SensorEventListener() {
        private final float[] rotation = new float[9];
        private final float[] screen = new float[9];

        @Override public void onSensorChanged(android.hardware.SensorEvent event) {
            android.hardware.SensorManager.getRotationMatrixFromVector(rotation, event.values);
            int x = android.hardware.SensorManager.AXIS_X;
            int y = android.hardware.SensorManager.AXIS_Y;
            switch (getWindowManager().getDefaultDisplay().getRotation()) {
                case android.view.Surface.ROTATION_90:
                    x = android.hardware.SensorManager.AXIS_Y;
                    y = android.hardware.SensorManager.AXIS_MINUS_X;
                    break;
                case android.view.Surface.ROTATION_180:
                    x = android.hardware.SensorManager.AXIS_MINUS_X;
                    y = android.hardware.SensorManager.AXIS_MINUS_Y;
                    break;
                case android.view.Surface.ROTATION_270:
                    x = android.hardware.SensorManager.AXIS_MINUS_Y;
                    y = android.hardware.SensorManager.AXIS_X;
                    break;
                default:
                    break;
            }
            android.hardware.SensorManager.remapCoordinateSystem(rotation, x, y, screen);
            // The screen's right, up and out of it, in the world (z is up).
            final float rightZ = screen[6], upZ = screen[7];
            final float forwardX = -screen[2], forwardY = -screen[5], forwardZ = -screen[8];
            final float heading = (float) Math.atan2(forwardX, forwardY);
            final float elevation = (float) Math.asin(Math.max(-1.0f, Math.min(1.0f, forwardZ)));
            final float roll = (float) Math.atan2(-rightZ, upZ);
            final float[] turn = NativeBridge.gyroTurn(heading, elevation, roll);
            // The phone's turns wait while a cutscene's cuts drive the view.
            if (session != 0 && NativeBridge.cameraFollowsCuts(session)) return;
            if (turn != null && turn.length == 2) renderView.turnBy(turn[0], turn[1]);
        }

        @Override public void onAccuracyChanged(android.hardware.Sensor sensor, int accuracy) { }
    };

    /** Listens to the phone while the gyroscope works (camera mode, on, app in front). */
    private void updateGyro() {
        final boolean want = cameraUi && NativeBridge.gyroActive() && !isFinishing();
        if (want == gyroListening) return;
        if (sensorManager == null) sensorManager = (android.hardware.SensorManager) getSystemService(SENSOR_SERVICE);
        if (sensorManager == null) return;
        if (want) {
            android.hardware.Sensor sensor = sensorManager.getDefaultSensor(android.hardware.Sensor.TYPE_GAME_ROTATION_VECTOR);
            if (sensor == null) sensor = sensorManager.getDefaultSensor(android.hardware.Sensor.TYPE_ROTATION_VECTOR);
            if (sensor == null) {
                notice("This phone has no gyroscope", Toast.LENGTH_LONG);
                return;
            }
            NativeBridge.gyroReset();
            gyroListening = sensorManager.registerListener(gyroListener, sensor,
                    android.hardware.SensorManager.SENSOR_DELAY_GAME);
        } else {
            sensorManager.unregisterListener(gyroListener);
            gyroListening = false;
            NativeBridge.gyroReset();
            renderView.renderNow();
        }
    }

    private void stopGyro() {
        if (sensorManager != null && gyroListening) sensorManager.unregisterListener(gyroListener);
        gyroListening = false;
    }

    /** How camera mode is used: its gestures, the gyroscope, the volume keys. */
    private void showCameraHelp() {
        final String text = "CAMERA MODE\n"
                + "📷 beside ⋮ goes in and back out. Set the scene up, shoot, go back, set it up again.\n"
                + "In camera mode the camera is free in space: it stands where it is and turns there (no orbit "
                + "round a point); the drone button adds the sticks to fly it; back in the scene it orbits again "
                + "with the same picture.\n\n"
                + "GESTURES\n"
                + "• Pinch — zoom (focal length)\n"
                + "• One finger — turn the camera where it stands (with the gyroscope too: aim it without "
                + "moving the phone)\n"
                + "• Hold one finger, slide another up / down — move the camera forward / back\n"
                + "• Two fingers drag — move the camera sideways, up and down\n"
                + "• Two fingers twist — turn the polarizer (Filter → Polarizer on)\n"
                + "• Two-finger tap — level the camera (no roll) and re-centre the gyroscope\n"
                + "• Three fingers twist — roll the camera left / right (a Dutch angle)\n"
                + "• Three fingers up / down — exposure compensation, a step per 50 dp\n"
                + "• Three-finger tap — take a picture\n"
                + "• Double tap — focus there (Focus dial: depth of field on to see it)\n"
                + "• Long press — lock the focus where it is (AF lock); again — auto focus\n"
                + "• Slide along the right edge — the focus ring: up farther, down nearer\n"
                + "• Volume button — the shutter; hold it — start / stop a video\n\n"
                + "SCREEN\n"
                + "• While the gyroscope or walking is on, the app keeps the orientation it has: turning "
                + "the phone aims the camera, not the screen.\n"
                + "• 📱 turns the app to the other orientation and keeps it there; hold 📱 to let the phone "
                + "turn it again.\n\n"
                + "GYROSCOPE\n"
                + "• 🧭 in the camera bar turns it on (or 🎛 → Gyroscope).\n"
                + "• Turn the phone left / right — the camera turns; tilt it up / down — the camera looks "
                + "up / down. Hold the phone upright or sideways: the picture follows the screen.\n"
                + "• It adds the phone's turns to the camera's, so a finger, the drone's sticks and the phone "
                + "all turn it together.\n"
                + "• Horizon: World (the start) keeps the scene level with the world as you tilt the phone, "
                + "like a real camera (the pictures come out tilted); Screen keeps the pictures level, like a "
                + "gimbal (the scene turns with the phone).\n"
                + "• Sensitivity 2× turns the camera twice as far as the phone; Smoothing steadies the hands.\n"
                + "• Looking straight up or down the turn left / right is held (the heading means nothing there).\n\n"
                + "DRONE\n"
                + "• The drone button (in the camera bar and the tools) flies the camera: the left stick flies, the right "
                + "stick looks; with the gyroscope the phone looks around too.\n\n"
                + "WALKING (ARCore)\n"
                + "• 🚶 in the camera bar (or 🎛 → Walking). The first time it says what ARCore is and asks for "
                + "the camera; Google Play Services for AR may be installed.\n"
                + "• Walk, step sideways, crouch, stand on tiptoe: the camera does the same in the scene. It "
                + "flies the drone, so the sticks still work.\n"
                + "• A metre walked moves the camera 1 m (🎛 → Walking: 0.25 m … 20 m, so a room can cross a "
                + "street). Height: follows you, or fixed for walking only.\n"
                + "• Keep the gyroscope on to turn by turning the phone.\n"
                + "• Move the phone slowly at first and point it at something with detail; in the dark or when "
                + "moving fast ARCore waits (it says why).\n"
                + "• ARCore is Google's (Google Privacy Policy); the camera's picture is never shown or kept.";
        new PanelBuilder().setTitle("Camera: gestures and gyroscope").setMessage(text)
                .setPositiveButton("Close", null).show();
    }

    @Override public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        if (cameraUi && (keyCode == android.view.KeyEvent.KEYCODE_VOLUME_UP
                || keyCode == android.view.KeyEvent.KEYCODE_VOLUME_DOWN)) {
            if (event.getRepeatCount() == 0) {
                volumeHeld = false;
                event.startTracking();
            }
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override public boolean onKeyLongPress(int keyCode, android.view.KeyEvent event) {
        if (cameraUi && (keyCode == android.view.KeyEvent.KEYCODE_VOLUME_UP
                || keyCode == android.view.KeyEvent.KEYCODE_VOLUME_DOWN)) {
            volumeHeld = true;
            if (recorder == null) startRecording(); else stopRecording();
            updateCameraButtons();
            return true;
        }
        return super.onKeyLongPress(keyCode, event);
    }

    @Override public boolean onKeyUp(int keyCode, android.view.KeyEvent event) {
        if (cameraUi && (keyCode == android.view.KeyEvent.KEYCODE_VOLUME_UP
                || keyCode == android.view.KeyEvent.KEYCODE_VOLUME_DOWN)) {
            if (!volumeHeld && !event.isCanceled()) takePicture();
            volumeHeld = false;
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    private void openCameraDial(String keys) {
        if (keys.equals(cameraDialOpen)) {
            cameraDialOpen = null;
            cameraAdjustScroll.setVisibility(View.GONE);
            updateCameraDials();
            return;
        }
        cameraDialOpen = keys;
        cameraAdjust.removeAllViews();
        final String[][] rows = cameraRows();
        for (final String index : keys.split(",")) {
            final int i;
            try {
                i = Integer.parseInt(index);
            } catch (NumberFormatException error) {
                continue;
            }
            if (i < 0 || i >= rows.length) continue;
            final String[] row = rows[i];
            final String[] options = cameraOptions(row);
            final java.util.function.IntConsumer pick = k -> {
                storeCameraOption(row, k);
                applyCameraChoices();
                renderView.renderNow();
                updateCameraDials();
            };
            cameraAdjust.addView(cameraSettingRow(row, options, pick));
        }
        cameraAdjustScroll.scrollTo(0, 0);
        cameraAdjustScroll.setVisibility(View.VISIBLE);
        updateCameraDials();
    }

    /** Play or pause what moves (a motion, a cutscene) from where it stands. */
    private void toggleCameraPlay() {
        if (renderView.isMotionPlaying()) {
            renderView.pauseMotion();
        } else if (session != 0 && NativeBridge.hasMotion(session)) {
            if (renderView.motionFrame() > 0.0f) renderView.resumeMotionAt(renderView.motionFrame());
            else renderView.startMotion();
        } else {
            notice("Nothing moves here", Toast.LENGTH_SHORT);
        }
        updateCameraButtons();
    }

    private static final int DIAL_TILE_DP = 64;

    /** A square tile's text: centred, shrunk to fit (two lines at most). */
    private TextView squareTile(String text) {
        final TextView tile = new TextView(this);
        tile.setGravity(Gravity.CENTER);
        tile.setMaxLines(2);
        tile.setPadding(dp(4), dp(4), dp(4), dp(4));
        tile.setAutoSizeTextTypeUniformWithConfiguration(9, 15, 1, android.util.TypedValue.COMPLEX_UNIT_SP);
        tile.setText(text);
        return tile;
    }

    /** A view whose height is its width: the tiles of the camera's options. */
    private static final class SquareFrame extends FrameLayout {
        SquareFrame(android.content.Context context) {
            super(context);
        }

        @Override protected void onMeasure(int widthSpec, int heightSpec) {
            super.onMeasure(widthSpec, widthSpec);
        }
    }

    /** A camera setting: its name and hint, then its options as equal squares (a long dial: a slider). */
    private LinearLayout cameraSettingRow(String[] row, String[] options, java.util.function.IntConsumer pick) {
        if (options.length > 12) {
            final LinearLayout block = sliderRow(row[2], options, cameraChoice(row), pick);
            if (!row[3].isEmpty()) block.addView(hint(row[3]), 1);
            return block;
        }
        return tileRow(row[2], row[3], options, cameraChoice(row), pick);
    }

    /** Options as rows of equal squares (as many a row as fit, at least five). */
    private LinearLayout tileRow(String label, String hintText, String[] names, int selected,
                                 java.util.function.IntConsumer onPick) {
        final LinearLayout block = new LinearLayout(this);
        block.setOrientation(LinearLayout.VERTICAL);
        block.setPadding(0, dp(10), 0, dp(4));
        final TextView title = new TextView(this);
        title.setText(label);
        title.setTextColor(0xffc8ccd6);
        title.setTextSize(14f);
        block.addView(title);
        if (hintText != null && !hintText.isEmpty()) {
            final TextView note = new TextView(this);
            note.setText(hintText);
            note.setTextColor(0xff8a90a0);
            note.setTextSize(12f);
            block.addView(note);
        }
        final int width = getResources().getDisplayMetrics().widthPixels - dp(60);
        final int perRow = Math.max(5, width / dp(DIAL_TILE_DP + 6));
        final TextView[] tiles = new TextView[names.length];
        LinearLayout row = null;
        for (int i = 0; i < names.length; ++i) {
            if (i % perRow == 0) {
                row = new LinearLayout(this);
                row.setOrientation(LinearLayout.HORIZONTAL);
                final LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
                rowParams.topMargin = dp(6);
                block.addView(row, rowParams);
            }
            final int index = i;
            final TextView tile = squareTile(names[i]);
            tiles[i] = tile;
            tile.setOnClickListener(v -> {
                for (int k = 0; k < tiles.length; ++k) styleChip(tiles[k], k == index);
                onPick.accept(index);
            });
            styleChip(tile, i == selected);
            final SquareFrame square = new SquareFrame(this);
            square.addView(tile, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            params.setMarginEnd(dp(6));
            row.addView(square, params);
        }
        // The last row's squares as wide as the others.
        while (row != null && row.getChildCount() < perRow) {
            final View spacer = new View(this);
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, 1, 1f);
            params.setMarginEnd(dp(6));
            row.addView(spacer, params);
        }
        return block;
    }

    /** A long dial (ISO, shutter, megapixels ...) as a slider; the label shows the value. */
    private LinearLayout sliderRow(String label, String[] names, int selected,
                                   java.util.function.IntConsumer onPick) {
        LinearLayout block = new LinearLayout(this);
        block.setOrientation(LinearLayout.VERTICAL);
        block.setPadding(0, dp(10), 0, dp(4));
        final TextView title = new TextView(this);
        title.setTextColor(0xffc8ccd6);
        title.setTextSize(14f);
        title.setText(label + ":  " + names[selected]);
        block.addView(title);
        final android.widget.SeekBar slider = new android.widget.SeekBar(this);
        slider.setMax(names.length - 1);
        slider.setProgress(selected);
        slider.setOnSeekBarChangeListener(new android.widget.SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(android.widget.SeekBar bar, int value, boolean fromUser) {
                title.setText(label + ":  " + names[value]);
                if (fromUser) onPick.accept(value);
            }
            @Override public void onStartTrackingTouch(android.widget.SeekBar bar) { }
            @Override public void onStopTrackingTouch(android.widget.SeekBar bar) { }
        });
        block.addView(slider, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        final TextView range = new TextView(this);
        range.setTextColor(0xff8a90a0);
        range.setTextSize(12f);
        range.setText(names[0] + "  …  " + names[names.length - 1]);
        block.addView(range);
        return block;
    }

    /** The camera's settings window: every setting by section, and the starting values. */
    private void showCameraSettings() {
        final PanelRef dialog = new PanelRef();
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(16), dp(12), dp(16), dp(24));
        content.addView(hint("One camera for pictures and videos: 📷 takes a picture (also the three-finger tap), "
                + "🎥 records a video, both through this lens, in this frame, with this look, without the "
                + "interface. Camera mode shows it live in the view; there, with the polarizer on, two fingers "
                + "turn the filter."));
        String section = null;
        for (final String[] row : cameraRows()) {
            if (!row[1].equals(section)) {
                section = row[1];
                content.addView(sectionTitle(section));
            }
            final String[] options = cameraOptions(row);
            final java.util.function.IntConsumer pick = i -> {
                if (row[0].equals(CAMERA_MODE_ID)) {
                    setCameraOption(CAMERA_MODE_ID, i);
                } else {
                    storeCameraOption(row, i);
                    applyCameraChoices();
                }
                renderView.renderNow();
            };
            content.addView(cameraSettingRow(row, options, pick));
        }
        dialog.dialog = showPanel("Camera: picture and video", content, null, "Starting values", () -> {
            final android.content.SharedPreferences.Editor editor = prefs().edit();
            for (final String[] row : cameraRows()) editor.remove(PREF_CAMERA + row[0]);
            editor.remove(PREF_POLARIZER);
            editor.apply();
            applyCameraChoices();
            renderView.renderNow();
            notice("Camera: starting values", Toast.LENGTH_SHORT);
            showCameraSettings();
        });
    }

    private boolean pictureTimerRunning;

    /** The ⋮ picture square and the three-finger tap: as the camera settings say. */
    private void takePicture() {
        if (pictureTimerRunning) return;
        final int[] view = renderView.captureViewSize();
        final int[] plan = NativeBridge.cameraPlan(0, view[0], view[1]);
        if (plan == null) return;
        final int timer = plan[7];
        if (timer <= 0) {
            shoot(plan, Math.max(1, plan[8]));
            return;
        }
        pictureTimerRunning = true;
        for (int left = timer; left > 0; --left) {
            final int shown = left;
            renderView.postDelayed(() -> notice("⏱ " + shown, Toast.LENGTH_SHORT), (timer - left) * 1000L);
        }
        renderView.postDelayed(() -> {
            pictureTimerRunning = false;
            final int[] now = renderView.captureViewSize();
            final int[] current = NativeBridge.cameraPlan(0, now[0], now[1]);
            if (current != null) shoot(current, Math.max(1, current[8]));
        }, timer * 1000L);
    }

    // Pictures are rendered and saved one after another off the UI thread
    // (a 124 MP picture takes seconds).
    private final java.util.concurrent.ExecutorService captureWorker =
            java.util.concurrent.Executors.newSingleThreadExecutor();

    /** One picture, then the rest of a burst a quarter second apart. */
    private void shoot(int[] plan, int left) {
        final DmcRenderView.CaptureState state = renderView.captureState(plan);
        if (state.session == 0 && state.still == null) {
            notice("Nothing to take a picture of", Toast.LENGTH_SHORT);
            return;
        }
        if ((long) plan[0] * plan[1] > 40_000_000L) {
            notice("📸 " + plan[0] + "×" + plan[1] + " — a few seconds…", Toast.LENGTH_SHORT);
        }
        captureWorker.execute(() -> savePicture(state, plan));
        if (left > 1) renderView.postDelayed(() -> shoot(plan, left - 1), 250L);
    }

    private long recordingStartMs;
    private final Runnable recordingClock = new Runnable() {
        @Override public void run() {
            if (recorder == null) return;
            final int seconds = (int) ((android.os.SystemClock.uptimeMillis() - recordingStartMs) / 1000L);
            if (recordingLimit > 0 && seconds >= recordingLimit) {
                stopRecording();
                return;
            }
            renderView.setRecordedSeconds(seconds);
            renderView.postDelayed(this, 1000L);
        }
    };
    private int recordingLimit;

    /** Starts a video as the camera settings say (no sound); the same square stops it. */
    private void startRecording() {
        final int[] view = renderView.captureViewSize();
        final int[] plan = NativeBridge.cameraPlan(1, view[0], view[1]);
        if (plan == null) return;
        try {
            NativeBridge.beginVideo();
            renderView.setRecording(plan[0], plan[1], plan[9] != 0);
            recorder = ViewRecorder.start(this, plan, renderView::drawCapture);
            recordingLimit = plan[6];
            recordingStartMs = android.os.SystemClock.uptimeMillis();
            renderView.postDelayed(recordingClock, 1000L);
            notice("🎥 Recording " + plan[0] + "×" + plan[1] + " " + plan[4] + " fps — ⋮ ⏹ stops it",
                    Toast.LENGTH_SHORT);
        } catch (java.io.IOException | RuntimeException error) {
            recorder = null;
            renderView.setRecording(0, 0, false);
            notice("Recording failed: " + error.getMessage(), Toast.LENGTH_LONG);
        }
    }

    private void stopRecording() {
        final ViewRecorder current = recorder;
        recorder = null;
        if (current == null) return;
        renderView.removeCallbacks(recordingClock);
        renderView.setRecording(0, 0, false);
        current.stop(saved -> notice(saved != null ? "🎥 Saved " + saved : "Recording failed",
                Toast.LENGTH_LONG));
    }

    /** A file being written into Pictures/DMC Native Reader (Android 10+) or the app's picture folder. */
    private static final class PictureFile {
        Uri uri;
        File file;
        ParcelFileDescriptor descriptor;
        String where;
    }

    private PictureFile openPicture(String name, String mime) throws java.io.IOException {
        final PictureFile out = new PictureFile();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            final android.content.ContentValues values = new android.content.ContentValues();
            values.put(android.provider.MediaStore.Images.Media.DISPLAY_NAME, name);
            values.put(android.provider.MediaStore.Images.Media.MIME_TYPE, mime);
            values.put(android.provider.MediaStore.Images.Media.RELATIVE_PATH, "Pictures/DMC Native Reader");
            values.put(android.provider.MediaStore.Images.Media.IS_PENDING, 1);
            out.uri = getContentResolver().insert(android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
            if (out.uri == null) throw new java.io.IOException("no place for the picture");
            out.descriptor = getContentResolver().openFileDescriptor(out.uri, "w");
            out.where = "Pictures/DMC Native Reader/" + name;
        } else {
            final File dir = getExternalFilesDir(android.os.Environment.DIRECTORY_PICTURES);
            if (dir == null || !(dir.isDirectory() || dir.mkdirs())) throw new java.io.IOException("no place for the picture");
            out.file = new File(dir, name);
            out.descriptor = ParcelFileDescriptor.open(out.file, ParcelFileDescriptor.MODE_WRITE_ONLY
                    | ParcelFileDescriptor.MODE_CREATE | ParcelFileDescriptor.MODE_TRUNCATE);
            out.where = out.file.getPath();
        }
        if (out.descriptor == null) {
            closePicture(out, false);
            throw new java.io.IOException("no place for the picture");
        }
        return out;
    }

    /** Closes a picture file: shown in the gallery when written, removed when not. */
    private void closePicture(PictureFile out, boolean written) {
        try {
            if (out.descriptor != null) out.descriptor.close();
        } catch (java.io.IOException ignored) {
            written = false;
        }
        if (out.uri != null) {
            if (written) {
                final android.content.ContentValues values = new android.content.ContentValues();
                values.put(android.provider.MediaStore.Images.Media.IS_PENDING, 0);
                getContentResolver().update(out.uri, values, null, null);
            } else {
                getContentResolver().delete(out.uri, null, null);
            }
        } else if (!written && out.file != null) {
            out.file.delete();
        }
    }

    /** A bitmap into a picture file: format 0 PNG, 1 JPEG (quality), 2 WebP lossless. */
    @SuppressWarnings("deprecation")
    private String writeImage(Bitmap image, String base, int format, int quality) {
        final String extension = format == 1 ? ".jpg" : format == 2 ? ".webp" : ".png";
        final String mime = format == 1 ? "image/jpeg" : format == 2 ? "image/webp" : "image/png";
        final Bitmap.CompressFormat compress = format == 1 ? Bitmap.CompressFormat.JPEG
                : format == 2 ? (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                        ? Bitmap.CompressFormat.WEBP_LOSSLESS : Bitmap.CompressFormat.WEBP)
                : Bitmap.CompressFormat.PNG;
        PictureFile out = null;
        boolean written = false;
        try {
            out = openPicture(base + extension, mime);
            try (OutputStream stream = new FileOutputStream(out.descriptor.getFileDescriptor())) {
                written = image.compress(compress, format == 1 ? quality : 100, stream);
            }
        } catch (Exception ignored) {
            written = false;
        } finally {
            if (out != null) closePicture(out, written);
        }
        return written ? out.where : null;
    }

    /**
     * Renders and saves one picture (on the capture worker) as the plan says: PNG, JPEG, WebP,
     * RAW (DNG), or RAW + JPEG. A still image has no scene to take a RAW of: it is a PNG.
     */
    private void savePicture(DmcRenderView.CaptureState state, int[] plan) {
        final String base = "dmc-" + new java.text.SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.US)
                .format(new java.util.Date());
        final int format = plan[2];
        String saved = null;
        String note = plan[0] + "×" + plan[1];
        if (format >= 3 && state.still == null) {
            Bitmap developed = null;
            if (format == 4) {
                try {
                    developed = Bitmap.createBitmap(plan[0], plan[1], Bitmap.Config.ARGB_8888);
                } catch (IllegalArgumentException | OutOfMemoryError error) {
                    developed = null;
                }
            }
            PictureFile out = null;
            boolean written = false;
            try {
                out = openPicture(base + ".dng", "image/x-adobe-dng");
                written = DmcRenderView.writeRaw(state, out.descriptor.getFd(), developed, plan[9] != 0);
            } catch (Exception ignored) {
                written = false;
            } finally {
                if (out != null) closePicture(out, written);
            }
            if (written) {
                saved = out.where;
                note += " RAW";
                if (developed != null) {
                    final String jpeg = writeImage(developed, base, 1, plan[3]);
                    if (jpeg != null) note += " + JPEG";
                }
            }
            if (developed != null) developed.recycle();
        } else {
            final Bitmap image = DmcRenderView.renderPhoto(state, plan);
            if (image != null) {
                note = image.getWidth() + "×" + image.getHeight();
                saved = writeImage(image, base, Math.min(format, 2), plan[3]);
                image.recycle();
            }
        }
        final String where = saved;
        final String size = note;
        runOnUiThread(() -> notice(where != null ? "📸 Saved " + where + " (" + size + ")"
                : "Picture failed (" + size + ": not enough memory?)", Toast.LENGTH_LONG));
    }

    /** Full-screen list of the gestures, each switchable, plus the camera follow. */
    private void showGesturesDialog() {
        final PanelRef dialog = new PanelRef();
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(16), dp(12), dp(16), dp(24));


        TextView always = new TextView(this);
        always.setTextColor(0xffc8ccd6);
        always.setTextSize(14f);
        always.setPadding(0, dp(8), 0, dp(4));
        always.setText("Always on: one finger turns the view, pinch zooms. Camera mode has gestures of its own "
                + "(ℹ in the camera bar): roll, exposure, focus by touch, the focus ring, the gyroscope.");
        content.addView(always);

        content.addView(choiceRow("Camera (a four-finger tap switches it)",
                new String[]{"Follows the model", "Stays in place"}, renderView.isFollowing() ? 0 : 1,
                i -> renderView.setFollow(i == 0)));

        final Object[][] list = {
                {DmcRenderView.G_PAN, "Two fingers drag — pan the camera; pinch — zoom (zoom and lens shown)"},
                {DmcRenderView.G_DOLLY, "Hold one finger, slide another up / down on the other half — move the camera (distance shown)"},
                {DmcRenderView.G_TWIST, "Two fingers twist — turn the model in the room (the view when there is no room)"},
                {DmcRenderView.G_CLOSE_PAN, "Two fingers held together, dragged — move the camera up / down, left / right (no zoom)"},
                {DmcRenderView.G_TURN, "Hold one finger, twist two others round it — turn the camera round the model / centre"},
                {DmcRenderView.G_DOUBLE_TAP, "Double tap — reset the view; on the room floor: stand the model there"},
                {DmcRenderView.G_TAP_PAUSE, "Tap the model — pause / resume the animation"},
                {DmcRenderView.G_FLING, "Flick — the view keeps turning and slows down"},
                {DmcRenderView.G_SCRUB, "Long press, then drag sideways — step through animation frames"},
                {DmcRenderView.G_BONE, "Long press — name and number of the joint under the finger"},
                {DmcRenderView.G_EDGE_MOTION, "Swipe in from the left / right edge — previous / next animation"},
                {DmcRenderView.G_THREE_SWIPE, "Three fingers sideways — previous / next position (enemy class, weapon, dress)"},
                {DmcRenderView.G_TOP_UI, "Swipe down from the top of the view — hide / show the bars"},
                {DmcRenderView.G_SCREENSHOT, "Three-finger tap — save a PNG of the view"},
                {DmcRenderView.G_FOLLOW, "Four-finger tap — camera follows the model / stays in place"},
        };
        for (Object[] entry : list) {
            final int bit = (Integer) entry[0];
            final int mask = prefs().getInt(PREF_GESTURES, DmcRenderView.G_ALL);
            content.addView(choiceRow((String) entry[1], new String[]{"On", "Off"}, (mask & bit) != 0 ? 0 : 1, i -> {
                int current = prefs().getInt(PREF_GESTURES, DmcRenderView.G_ALL);
                current = i == 0 ? (current | bit) : (current & ~bit);
                prefs().edit().putInt(PREF_GESTURES, current).apply();
                renderView.setGestures(current);
            }));
        }

        dialog.dialog = showPanel("Gestures", content, null, null, null);
    }

    /** The ⋮ menu in the app's panel window: square shortcuts (settings first) in rows, then actions. */
    private void showCompanionMenu(View anchor) {
        // The core decides the items and names them; this shell lays them out
        // and runs the platform step of the picked id.
        final CompanionMenu menu = NativeBridge.companionMenu(session, isRootScene(),
                assembledPacUri != null, stagePacUri != null, roomLoaded, roomShown(), recorder != null);
        if (menu == null) return;
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(12), dp(12), dp(12), dp(12));
        final AlertDialog[] shown = new AlertDialog[1];
        final java.util.function.IntConsumer pick = id -> {
            if (shown[0] != null) shown[0].dismiss();
            runCompanion(id);
        };
        // Squares in rows as wide as the window allows (a long press names one).
        final int screenWidth = getResources().getDisplayMetrics().widthPixels;
        final int perRow = Math.max(3, Math.min(6, (screenWidth - dp(96)) / dp(TOOL_SIZE_DP + 6)));
        LinearLayout icons = null;
        for (int i = 0; menu.shortcuts != null && i < menu.shortcuts.ids.length; ++i) {
            if (i % perRow == 0) {
                icons = new LinearLayout(this);
                icons.setOrientation(LinearLayout.HORIZONTAL);
                LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
                rowParams.bottomMargin = dp(6);
                panel.addView(icons, rowParams);
            }
            final int id = menu.shortcuts.ids[i];
            final String glyph = i < menu.shortcuts.icons.length ? menu.shortcuts.icons[i] : "";
            final Button button = makeSquareButton(glyph, menu.shortcuts.labels[i], 22f);
            button.setTooltipText(menu.shortcuts.labels[i]);
            if (menu.isDimmed(id)) button.setAlpha(0.5f);
            button.setOnClickListener(v -> pick.accept(id));
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP));
            params.setMarginEnd(dp(6));
            icons.addView(button, params);
        }
        for (int i = 0; menu.rows != null && i < menu.rows.ids.length; ++i) {
            final int id = menu.rows.ids[i];
            panel.addView(menuRow(menu.rows.labels[i], 0xffe8eaf0, v -> pick.accept(id)));
        }
        // The rest, folded: "Other actions" opens it.
        if (menu.more != null && menu.more.ids.length > 0) {
            final LinearLayout folded = new LinearLayout(this);
            folded.setOrientation(LinearLayout.VERTICAL);
            folded.setVisibility(View.GONE);
            for (int i = 0; i < menu.more.ids.length; ++i) {
                final int id = menu.more.ids[i];
                folded.addView(menuRow(menu.more.labels[i], 0xffb8bcc8, v -> pick.accept(id)));
            }
            final TextView header = menuRow("\u25b8 Other actions (" + menu.more.ids.length + ")", 0xff9aa0ae, null);
            header.setOnClickListener(v -> {
                final boolean opening = folded.getVisibility() != View.VISIBLE;
                folded.setVisibility(opening ? View.VISIBLE : View.GONE);
                header.setText((opening ? "\u25be" : "\u25b8") + " Other actions (" + menu.more.ids.length + ")");
            });
            panel.addView(header);
            panel.addView(folded);
        }
        shown[0] = showPanel(titleView.getText(), panel, null, null, null);
    }

    /** One text row of the ⋮ menu. */
    private TextView menuRow(String label, int color, View.OnClickListener click) {
        TextView row = new TextView(this);
        row.setText(label);
        row.setTextSize(16f);
        row.setTextColor(color);
        row.setPadding(dp(4), dp(12), dp(16), dp(12));
        if (click != null) row.setOnClickListener(click);
        return row;
    }

    /** A ⋮ item: the core's own actions go back to it; the rest are platform steps. */
    private void runCompanion(int id) {
        switch (id) {
            case CompanionMenu.SETTINGS: showSettingsDialog(); break;
            case CompanionMenu.GESTURES: showGesturesDialog(); break;
            case CompanionMenu.ROOM_TOGGLE: roomToggle(); break;
            case CompanionMenu.NEXT_ROOM_SPOT: roomNextSpot(); break;
            case CompanionMenu.OPEN_FILE: chooseFile(); break;
            case CompanionMenu.TEXTURE_FORMAT: showTextureFormatDialog(); break;
            case CompanionMenu.SAVE_FILE: chooseSaveSource(); break;
            case CompanionMenu.ADD_PAC: chooseAdditionalPac(); break;
            case CompanionMenu.BROWSE_ASSEMBLED_PAC: browseAssembledPac(); break;
            case CompanionMenu.BROWSE_STAGE_PAC: if (stagePacUri != null) browsePac(stagePacUri); break;
            case CompanionMenu.STAGE_EFFECTS: roomEffectsChoose(); break;
            case CompanionMenu.ADD_MOD_PARTS: chooseAdditionalMods(); break;
            case CompanionMenu.COMPOSITE_PARTS: showCompositePartsDialog(); break;
            case CompanionMenu.PLACE_MOD_PART: showPlaceModPartDialog(); break;
            case CompanionMenu.RESET_MOD_PLACEMENT: showResetModPlacementDialog(); break;
            case CompanionMenu.ATTACH_PTX: choosePtxForCurrentSession(); break;
            case CompanionMenu.STAGE_MOTION: chooseStagedAssets(REQUEST_STAGE_MOTION, true); break;
            case CompanionMenu.STAGE_TEXTURE: chooseStagedAssets(REQUEST_STAGE_TEXTURE, true); break;
            case CompanionMenu.STAGE_PHYSICS: chooseStagedAssets(REQUEST_STAGE_PHYSICS, true); break;
            case CompanionMenu.STAGE_CLOTH: chooseStagedAssets(REQUEST_STAGE_CLOTH, true); break;
            case CompanionMenu.STAGE_OTHER: chooseStagedAssets(REQUEST_STAGE_OTHER, true); break;
            case CompanionMenu.DIAGNOSTICS: showDiagnosticsDialog(); break;
            case CompanionMenu.PHOTO: takePicture(); break;
            case CompanionMenu.CAMERA_SETTINGS: showCameraSettings(); break;
            case CompanionMenu.VIDEO: if (recorder == null) startRecording(); else stopRecording(); break;
            default: handleOutcome(NativeBridge.runCompanionAction(session, id)); break;
        }
    }

    /** Copies the picked stage into app storage (it is reloaded on start). */
    private void chooseRoom(Uri uri) {
        final String name = displayName(uri);
        notice("Room: loading " + name + "\u2026", Toast.LENGTH_SHORT);
        new Thread(() -> {
            final File target = roomFile();
            final File partial = new File(getFilesDir(), ROOM_FILE + ".part");
            boolean copied = false;
            try (java.io.InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new java.io.FileOutputStream(partial)) {
                if (in != null) {
                    byte[] buffer = new byte[1 << 16];
                    int read;
                    while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
                    copied = true;
                }
            } catch (Exception ignored) {
                copied = false;
            }
            if (!copied) {
                //noinspection ResultOfMethodCallIgnored
                partial.delete();
                runOnUiThread(() -> notice("Room: could not read " + name, Toast.LENGTH_LONG));
                return;
            }
            final String detail = loadRoomFile(partial, name);
            if (detail == null) {
                //noinspection ResultOfMethodCallIgnored
                partial.delete();
                runOnUiThread(() -> notice("Room: nothing to draw in " + name, Toast.LENGTH_LONG));
                return;
            }
            //noinspection ResultOfMethodCallIgnored
            target.delete();
            //noinspection ResultOfMethodCallIgnored
            partial.renameTo(target);
            getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                    .putString(PREF_ROOM_NAME, name).putBoolean(PREF_ROOM_SHOWN, true).apply();
            runOnUiThread(() -> {
                roomLoaded = true;
                roomName = name;
                roomDetail = detail;
                renderView.setRoomVisible(true);
                applyResourceUiState();
                notice("Room: " + detail, Toast.LENGTH_LONG);
            });
        }).start();
    }

    private void restoreRoom() {
        final File file = roomFile();
        final String name = getSharedPreferences(PREFS, MODE_PRIVATE).getString(PREF_ROOM_NAME, null);
        if (name == null || !file.isFile()) return;
        new Thread(() -> {
            final String detail = loadRoomFile(file, name);
            if (detail == null) return;
            runOnUiThread(() -> {
                roomLoaded = true;
                roomName = name;
                roomDetail = detail;
                renderView.setRoomVisible(roomShown());
                applyResourceUiState();
            });
        }).start();
    }

    /** Native room build (stage_room.h) from a local file; its summary or null. */
    private static String loadRoomFile(File file, String name) {
        try (ParcelFileDescriptor pfd = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)) {
            return NativeBridge.loadRoom(pfd.getFd(), name);
        } catch (Exception e) {
            return null;
        }
    }


    /** One motion card: a MOT found in the assembled PAC or a staged file. */
    private static final class MotionEntry {
        final String name;
        final int libraryIndex;   // >= 0: native Session::motion_library
        final Uri uri;            // staged file otherwise
        final int packSlot;       // top-level nested PAC slot; -1 unknown, -2 staged

        MotionEntry(String name, int libraryIndex, Uri uri, int packSlot) {
            this.name = name;
            this.libraryIndex = libraryIndex;
            this.uri = uri;
            this.packSlot = packSlot;
        }
    }

    private ArrayList<MotionEntry> motionEntries() {
        ArrayList<MotionEntry> result = new ArrayList<>();
        if (session != 0 && isRootScene()) {
            final int count = NativeBridge.motionLibraryCount(session);
            for (int index = 0; index < count; ++index) {
                result.add(new MotionEntry(
                        NativeBridge.motionLibraryName(session, index),
                        index,
                        null,
                        NativeBridge.motionLibraryPackSlot(session, index)));
            }
        }
        for (StagedAsset asset : motionAssets()) {
            result.add(new MotionEntry(asset.name, -1, asset.uri, -2));
        }
        return result;
    }

    private void refreshMotionStrip() {
        if (motionBar == null || motionScroll == null) return;
        motionBar.removeAllViews();
        motionCards.clear();
        ArrayList<MotionEntry> motions = motionEntries();
        if (motions.isEmpty() || !isRootScene()) {
            motionScroll.setVisibility(View.GONE);
            if (motions.isEmpty()) {
                selectedMotionIndex = -1;
                selectedScriptIndex = -1;
            }
            return;
        }

        final int scriptCount = session == 0
                ? 0 : Math.max(0, NativeBridge.motionScriptCount(session));
        int previousPack = Integer.MIN_VALUE;
        for (int index = 0; index < motions.size(); ++index) {
            final int motionIndex = index;
            MotionEntry entry = motions.get(index);
            if (index > 0 && entry.packSlot != previousPack) {
                addMotionPackSeparator(entry.packSlot);
            }
            previousPack = entry.packSlot;

            // One visual row per MOT:
            // [linked Script Play ...] [raw MOT].
            // A controller without a real link to this MOT is not a disabled
            // action: it is absent data, so do not spend UI space on it.
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);

            if (entry.libraryIndex >= 0) {
                for (int script = 0; script < scriptCount; ++script) {
                    final int scriptIndex = script;
                    final int slot = NativeBridge.motionScriptSlot(session, scriptIndex);
                    final boolean available = NativeBridge.motionScriptCanPlayMotion(
                            session, scriptIndex, entry.libraryIndex);
                    if (!available) continue;
                    final boolean activeScript =
                            index == selectedMotionIndex &&
                            scriptIndex == selectedScriptIndex;

                    Button scriptButton = makeSquareButton(
                            "▶\nS" + (slot >= 0 ? Integer.toString(slot) : "?"),
                            "Play " + entry.name + " through MotionScript " +
                                    (slot >= 0 ? "slot " + slot : Integer.toString(scriptIndex)),
                            9f);
                    scriptButton.setActivated(activeScript);
                    scriptButton.setAlpha(activeScript ? 1.0f : 0.78f);
                    scriptButton.setOnClickListener(
                            v -> selectMotionScript(motionIndex, scriptIndex));
                    addToolButton(row, scriptButton);
                }
            }

            final boolean activeMot =
                    index == selectedMotionIndex && selectedScriptIndex < 0;
            Button motButton = makeSquareButton(
                    "", "Play raw MOT " + entry.name, 11f);
            motButton.setText(motionCardLabel(entry.name));
            motButton.setActivated(activeMot);
            motButton.setAlpha(activeMot ? 1.0f : 0.72f);
            motButton.setOnClickListener(v -> selectMotion(motionIndex));
            addToolButton(row, motButton);

            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, dp(TOOL_SIZE_DP));
            rowParams.setMarginStart(dp(1));
            rowParams.setMarginEnd(dp(1));
            motionBar.addView(row, rowParams);
            motionCards.add(row);
        }
        motionScroll.setVisibility(uiHidden || cameraUi ? View.GONE : View.VISIBLE);
    }

    private void addMotionPackSeparator(int packSlot) {
        TextView separator = new TextView(this);
        separator.setText("\u2502");
        separator.setTextColor(0x88ffffff);
        separator.setTextSize(28f);
        separator.setGravity(Gravity.CENTER);
        separator.setContentDescription(
                packSlot >= 0 ? "Animation pack slot " + packSlot : "Animation source boundary");
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                dp(18), dp(TOOL_SIZE_DP));
        params.setMarginStart(dp(2));
        params.setMarginEnd(dp(2));
        motionBar.addView(separator, params);
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

    // Raw MOT playback: no MotionScript state/channels are executed.
    private void selectMotion(int index) {
        ArrayList<MotionEntry> motions = motionEntries();
        if (session == 0 || index < 0 || index >= motions.size()) return;
        if (index == selectedMotionIndex && selectedScriptIndex < 0 &&
                NativeBridge.hasMotion(session)) {
            if (renderView.isMotionPlaying()) {
                renderView.pauseMotion();
            } else {
                renderView.startMotion();
            }
            return;
        }

        renderView.pauseMotion();
        final MotionEntry entry = motions.get(index);
        String report;
        if (entry.libraryIndex >= 0) {
            report = NativeBridge.loadLibraryMotion(session, entry.libraryIndex);
        } else {
            try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(entry.uri)) {
                if (pfd == null) throw new FileNotFoundException("No file descriptor");
                report = NativeBridge.loadMotion(session, pfd.getFd(), entry.name);
            } catch (Exception error) {
                report = "Motion: could not read " + entry.name + ": " + error;
            }
        }

        final boolean bound = NativeBridge.hasMotion(session);
        selectedMotionIndex = bound ? index : -1;
        selectedScriptIndex = -1;
        refreshMotionStrip();
        if (bound) renderView.startMotion();
        else renderView.renderNow();
        notice(bound ? entry.name + " · raw MOT ▶"
                     : (report == null ? "Motion rejected" : report),
                Toast.LENGTH_LONG);
        rebuildInfo(titleView.getText().toString());
        if (report != null && !report.isEmpty()) {
            setInfo(infoText + "\nMOTION · RAW MOT\n" + report + "\n");
        }
    }

    // MotionScript playback: script action + MOT + class runtime bridge.
    private void selectMotionScript(int index, int scriptIndex) {
        ArrayList<MotionEntry> motions = motionEntries();
        if (session == 0 || index < 0 || index >= motions.size() ||
                scriptIndex < 0) {
            return;
        }
        final MotionEntry entry = motions.get(index);
        if (entry.libraryIndex < 0 ||
                !NativeBridge.motionScriptCanPlayMotion(
                        session, scriptIndex, entry.libraryIndex)) {
            return;
        }

        if (index == selectedMotionIndex && scriptIndex == selectedScriptIndex &&
                NativeBridge.hasMotion(session)) {
            if (renderView.isMotionPlaying()) {
                renderView.pauseMotion();
            } else {
                renderView.startMotion();
            }
            return;
        }

        renderView.pauseMotion();
        final int slot = NativeBridge.motionScriptSlot(session, scriptIndex);
        final String report = NativeBridge.loadLibraryMotionScript(
                session, scriptIndex, entry.libraryIndex);
        final boolean bound = NativeBridge.hasMotion(session);
        selectedMotionIndex = bound ? index : -1;
        selectedScriptIndex = bound ? scriptIndex : -1;
        refreshMotionStrip();
        if (bound) renderView.startMotion();
        else renderView.renderNow();

        final String scriptName =
                slot >= 0 ? "S" + slot : "Script " + scriptIndex;
        notice(bound ? scriptName + " · " + entry.name + " ▶"
                     : (report == null ? "MotionScript rejected" : report),
                Toast.LENGTH_LONG);
        rebuildInfo(titleView.getText().toString());
        if (report != null && !report.isEmpty()) {
            setInfo(infoText + "\nMOTION SCRIPT · " + scriptName +
                    "\n" + report + "\n");
        }
    }

    // Archives with several in-game looks (em000.pac: enemy classes and their
    // weapons; em028.pac: Nevan's dress with the bats in or out) get one button
    // per position under the title; tapping one re-assembles that look.
    private void refreshVariantBar() {
        if (variantBar == null || variantScroll == null) return;
        variantBar.removeAllViews();
        final String[] positions = (isRootScene() && assembledPacUri != null)
                ? NativeBridge.archiveVariantNames(displayName(assembledPacUri))
                : null;
        if (positions == null || positions.length < 2) {
            variantScroll.setVisibility(View.GONE);
            return;
        }
        for (int index = 0; index < positions.length; ++index) {
            final int position = index;
            final boolean active = index == enemyVariant;
            Button button = makeSquareButton(positions[index], "Show " + positions[index], 13f);
            button.setPadding(dp(10), 0, dp(10), 0);
            button.setActivated(active);
            button.setAlpha(active ? 1.0f : 0.72f);
            button.setOnClickListener(v -> {
                if (position == enemyVariant) return;
                enemyVariant = position;
                assembleWithAddedPacs(null);
            });
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, dp(TOOL_SIZE_DP - 8));
            params.setMarginStart(dp(TOOL_GAP_DP));
            params.setMarginEnd(dp(TOOL_GAP_DP));
            variantBar.addView(button, params);
        }
        // Class events of the assembled enemy (em000 family: Death = control
        // code 0x3E7). They run on top of the playing Script Play action.
        final String[] events = session == 0 ? null : NativeBridge.classEventNames(session);
        for (int index = 0; events != null && index < events.length; ++index) {
            final int event = index;
            final String name = events[index];
            Button button = makeSquareButton("\u2620 " + name, "Send class event " + name, 13f);
            button.setPadding(dp(10), 0, dp(10), 0);
            button.setAlpha(0.85f);
            button.setOnClickListener(v -> {
                if (session == 0) return;
                final boolean sent = NativeBridge.triggerClassEvent(session, event);
                if (sent && !renderView.isMotionPlaying()) renderView.startMotion();
                notice(sent ? name + " ▶" : "Play an action through Script Play first",
                        Toast.LENGTH_SHORT);
            });
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, dp(TOOL_SIZE_DP - 8));
            params.setMarginStart(dp(TOOL_GAP_DP * 4));
            params.setMarginEnd(dp(TOOL_GAP_DP));
            variantBar.addView(button, params);
        }
        variantScroll.setVisibility(uiHidden ? View.GONE : View.VISIBLE);
    }

    private void chooseAdditionalPac() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/vnd.dmc.pac",
                "application/octet-stream",
                "*/*"
        });
        startActivityForResult(intent, REQUEST_ADD_PAC);
    }

    // Re-assemble the character PAC together with every added archive. Weapon
    // PACs (plwp_*.pac) hang from the body joint recorded by the game.
    private void assembleWithAddedPacs(Uri added) {
        if (assembledPacUri == null) return;
        ArrayList<Uri> uris = new ArrayList<>();
        uris.add(assembledPacUri);
        uris.addAll(addedPacUris);
        if (added != null && !uris.contains(added)) uris.add(added);

        long[] handles = new long[uris.size()];
        String[] names = new String[uris.size()];
        long scene = 0;
        String failure = null;
        try {
            for (int index = 0; index < uris.size(); ++index) {
                names[index] = displayName(uris.get(index));
                try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uris.get(index))) {
                    if (pfd == null) throw new FileNotFoundException("No file descriptor");
                    handles[index] = NativeBridge.open(pfd.getFd(), names[index]);
                }
                if (handles[index] == 0) {
                    failure = names[index] + " is not a readable DMC archive";
                    break;
                }
            }
            if (failure == null) {
                scene = NativeBridge.assemblePacs(handles, names, enemyVariant);
                if (scene == 0) failure = "Archives could not be assembled";
            }
        } catch (Exception error) {
            failure = "Could not read archives: " + error;
        } finally {
            for (long handle : handles) if (handle != 0) NativeBridge.close(handle);
        }
        if (scene == 0) {
            notice(failure, Toast.LENGTH_LONG);
            return;
        }
        final Uri character = assembledPacUri;
        final ArrayList<Uri> extras = new ArrayList<>(uris.subList(1, uris.size()));
        renderView.pauseMotion();
        closeAllSessions();
        resetCompositionState();
        assembledPacUri = character;
        addedPacUris.addAll(extras);
        StringBuilder title = new StringBuilder(displayName(character));
        final String[] classes = NativeBridge.archiveVariantNames(displayName(character));
        if (classes != null && enemyVariant < classes.length) {
            title.append(" · ").append(classes[enemyVariant]);
        }
        for (Uri uri : extras) title.append(" + ").append(displayName(uri));
        activateSession(scene, title.toString());
        notice(title + " assembled", Toast.LENGTH_LONG);
    }

    private void browseAssembledPac() {
        browsePac(assembledPacUri);
    }

    private void browsePac(Uri pac) {
        if (pac == null) return;
        final String name = displayName(pac);
        long archive = 0;
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(pac)) {
            if (pfd != null) archive = NativeBridge.open(pfd.getFd(), name);
        } catch (Exception ignored) {
            archive = 0;
        }
        if (archive == 0) {
            notice("Could not re-open " + name, Toast.LENGTH_LONG);
            return;
        }
        renderView.pauseMotion();
        navigateToSession(archive, name + " · files");
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
        final String snapshot = text == null || text.isEmpty()
                ? "No resource information available."
                : text;
        Button download = makeSquareButton("\u21e9", "Download inspection report", 24f);
        download.setOnClickListener(v -> chooseInfoExport(snapshot));
        showPanel(titleView.getText(), panelText(snapshot, 13f), download, null, null);
    }

    /** Selectable monospace text for a panel. */
    private TextView panelText(String text, float size) {
        TextView details = new TextView(this);
        details.setText(text);
        details.setTextColor(Color.WHITE);
        details.setTextSize(size);
        details.setTypeface(Typeface.MONOSPACE);
        details.setTextIsSelectable(true);
        details.setPadding(dp(16), dp(12), dp(16), dp(20));
        return details;
    }

    /**
     * The app's one window. Every dialog is built here: a title bar (with an
     * optional square action), then the message, a view and the items, all
     * scrolling, and the buttons at the bottom (Close when none is given).
     * It takes AlertDialog.Builder's calls, so each window only says what it
     * holds.
     */
    final class PanelBuilder {
        private CharSequence title = "";
        private Button action;
        private CharSequence message;
        private boolean monospace;
        private View view;
        private CharSequence[] items;
        private android.content.DialogInterface.OnClickListener itemClick;
        private final CharSequence[] labels = new CharSequence[3];  // positive, neutral, negative
        private final android.content.DialogInterface.OnClickListener[] clicks =
                new android.content.DialogInterface.OnClickListener[3];
        private boolean cancelable = true;
        private android.content.DialogInterface.OnDismissListener dismissed;
        private CharSequence[] choices;  // one of them picked, the window stays
        private int chosen = -1;
        private android.content.DialogInterface.OnClickListener choiceClick;

        PanelBuilder setTitle(CharSequence value) { title = value == null ? "" : value; return this; }
        PanelBuilder setAction(Button value) { action = value; return this; }
        PanelBuilder setMessage(CharSequence value) { message = value; return this; }
        PanelBuilder setMonospace(boolean value) { monospace = value; return this; }
        PanelBuilder setView(View value) { view = value; return this; }
        PanelBuilder setItems(CharSequence[] values, android.content.DialogInterface.OnClickListener click) {
            items = values;
            itemClick = click;
            return this;
        }
        PanelBuilder setPositiveButton(CharSequence label, android.content.DialogInterface.OnClickListener click) {
            labels[0] = label; clicks[0] = click; return this;
        }
        PanelBuilder setNeutralButton(CharSequence label, android.content.DialogInterface.OnClickListener click) {
            labels[1] = label; clicks[1] = click; return this;
        }
        PanelBuilder setNegativeButton(CharSequence label, android.content.DialogInterface.OnClickListener click) {
            labels[2] = label; clicks[2] = click; return this;
        }
        PanelBuilder setPositiveButton(int label, android.content.DialogInterface.OnClickListener click) {
            return setPositiveButton(getString(label), click);
        }
        PanelBuilder setNegativeButton(int label, android.content.DialogInterface.OnClickListener click) {
            return setNegativeButton(getString(label), click);
        }
        PanelBuilder setSingleChoiceItems(CharSequence[] values, int selected,
                                          android.content.DialogInterface.OnClickListener click) {
            choices = values;
            chosen = selected;
            choiceClick = click;
            return this;
        }
        PanelBuilder setCancelable(boolean value) { cancelable = value; return this; }
        PanelBuilder setOnDismissListener(android.content.DialogInterface.OnDismissListener value) {
            dismissed = value;
            return this;
        }

        AlertDialog show() {
            LinearLayout body = new LinearLayout(MainActivity.this);
            body.setOrientation(LinearLayout.VERTICAL);
            if (message != null && message.length() > 0) {
                TextView text = new TextView(MainActivity.this);
                text.setText(message);
                text.setTextColor(Color.WHITE);
                text.setTextSize(monospace ? 13f : 15f);
                if (monospace) text.setTypeface(Typeface.MONOSPACE);
                text.setTextIsSelectable(true);
                text.setPadding(dp(16), dp(12), dp(16), dp(12));
                body.addView(text);
            }
            if (view != null) {
                if (view.getParent() instanceof android.view.ViewGroup) {
                    ((android.view.ViewGroup) view.getParent()).removeView(view);
                }
                body.addView(view);
            }
            final AlertDialog[] built = new AlertDialog[1];
            for (int k = 0; items != null && k < items.length; ++k) {
                final int which = k;
                TextView row = menuRow(String.valueOf(items[k]), 0xffe8eaf0, v -> {
                    if (built[0] != null) built[0].dismiss();
                    if (itemClick != null) itemClick.onClick(built[0], which);
                });
                row.setPadding(dp(16), dp(12), dp(16), dp(12));
                body.addView(row);
            }
            final java.util.List<TextView> choiceRows = new java.util.ArrayList<>();
            for (int k = 0; choices != null && k < choices.length; ++k) {
                final int which = k;
                TextView row = menuRow((k == chosen ? "\u25c9 " : "\u25cb ") + choices[k], 0xffe8eaf0, null);
                row.setPadding(dp(16), dp(10), dp(16), dp(10));
                row.setOnClickListener(v -> {
                    chosen = which;
                    for (int r = 0; r < choiceRows.size(); ++r) {
                        choiceRows.get(r).setText((r == which ? "\u25c9 " : "\u25cb ") + choices[r]);
                    }
                    if (choiceClick != null) choiceClick.onClick(built[0], which);
                });
                choiceRows.add(row);
                body.addView(row);
            }
            ScrollView scroll = new ScrollView(MainActivity.this);
            scroll.setBackgroundColor(0xff141418);
            scroll.addView(body, new ScrollView.LayoutParams(
                    ScrollView.LayoutParams.MATCH_PARENT, ScrollView.LayoutParams.WRAP_CONTENT));

            LinearLayout bar = new LinearLayout(MainActivity.this);
            bar.setOrientation(LinearLayout.HORIZONTAL);
            bar.setGravity(Gravity.CENTER_VERTICAL);
            bar.setPadding(dp(16), dp(6), dp(8), dp(6));
            bar.setBackgroundColor(0xff3a3a3e);
            bar.setMinimumHeight(dp(60));
            TextView heading = new TextView(MainActivity.this);
            heading.setText(title);
            heading.setTextColor(Color.WHITE);
            heading.setTextSize(18f);
            heading.setSingleLine(true);
            heading.setEllipsize(TextUtils.TruncateAt.END);
            bar.addView(heading, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            if (action != null) {
                if (action.getParent() instanceof android.view.ViewGroup) {
                    ((android.view.ViewGroup) action.getParent()).removeView(action);
                }
                bar.addView(action, new LinearLayout.LayoutParams(dp(48), dp(48)));
            }

            AlertDialog.Builder builder = new AlertDialog.Builder(MainActivity.this)
                    .setCustomTitle(bar)
                    .setView(scroll)
                    .setCancelable(cancelable);
            final boolean any = labels[0] != null || labels[1] != null || labels[2] != null;
            if (!any) {
                builder.setPositiveButton("Close", null);
            } else {
                if (labels[0] != null) builder.setPositiveButton(labels[0], clicks[0]);
                if (labels[1] != null) builder.setNeutralButton(labels[1], clicks[1]);
                if (labels[2] != null) builder.setNegativeButton(labels[2], clicks[2]);
            }
            if (dismissed != null) builder.setOnDismissListener(dismissed);
            built[0] = builder.show();
            return built[0];
        }
    }

    /** A dismiss handle for a window whose content is built before it shows. */
    private static final class PanelRef {
        AlertDialog dialog;
        void dismiss() { if (dialog != null) dialog.dismiss(); }
    }

    /** The one window with a content view (Info, the ⋮ menu, Diagnostics, Settings, Gestures). */
    private AlertDialog showPanel(CharSequence title, View content, Button action,
                                  String extraLabel, Runnable extra) {
        PanelBuilder builder = new PanelBuilder().setTitle(title).setView(content).setAction(action);
        if (extraLabel != null && extra != null) {
            builder.setPositiveButton("Close", null).setNeutralButton(extraLabel, (d, w) -> extra.run());
        }
        return builder.show();
    }

    private void chooseInfoExport(String snapshot) {
        final String[] choices = {
                "Markdown (.md)",
                "DMC download snapshot (.download)",
                "Plain text (.txt)",
                "JSON (.json)"
        };
        new PanelBuilder()
                .setTitle("Download info")
                .setItems(choices, (dialog, which) -> launchInfoExport(snapshot, which))
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    private void launchInfoExport(String snapshot, int format) {
        final String extension;
        final String mime;
        switch (format) {
            case 0:
                extension = "md";
                mime = "text/markdown";
                break;
            case 1:
                extension = "download";
                mime = "application/octet-stream";
                break;
            case 2:
                extension = "txt";
                mime = "text/plain";
                break;
            case 3:
                extension = "json";
                mime = "application/json";
                break;
            default:
                return;
        }

        pendingInfoExportText = formatInfoExport(snapshot, format);
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mime);
        intent.putExtra(Intent.EXTRA_TITLE, infoExportFileName(extension));
        startActivityForResult(intent, REQUEST_EXPORT_INFO);
    }

    private String infoExportFileName(String extension) {
        String title = sanitizeFileComponent(titleView.getText().toString());
        if (title.isEmpty()) title = "resource";
        return title + "_info." + extension;
    }

    private String formatInfoExport(String snapshot, int format) {
        final String title = titleView.getText().toString();
        if (format == 0) {
            return "# " + title + "\n\n"
                    + "- DMC Native Reader: " + BuildConfig.VERSION_NAME + "\n"
                    + "- Export: inspection snapshot\n\n"
                    + "~~~text\n" + snapshot + "\n~~~\n";
        }
        if (format == 1) {
            return "DMC_NATIVE_READER_DOWNLOAD/1\n"
                    + "version=" + BuildConfig.VERSION_NAME + "\n"
                    + "resource=" + title + "\n"
                    + "encoding=UTF-8\n"
                    + "payload=inspection-text\n\n"
                    + snapshot + "\n";
        }
        if (format == 3) {
            return "{\n"
                    + "  \"schema\": \"dmc-native-reader.inspection.v1\",\n"
                    + "  \"version\": \"" + jsonEscape(BuildConfig.VERSION_NAME) + "\",\n"
                    + "  \"resource\": \"" + jsonEscape(title) + "\",\n"
                    + "  \"report\": \"" + jsonEscape(snapshot) + "\"\n"
                    + "}\n";
        }
        return snapshot + (snapshot.endsWith("\n") ? "" : "\n");
    }

    private String jsonEscape(String value) {
        if (value == null) return "";
        StringBuilder out = new StringBuilder(value.length() + 16);
        for (int index = 0; index < value.length(); ++index) {
            char c = value.charAt(index);
            switch (c) {
                case '\\': out.append("\\\\"); break;
                case '\"': out.append("\\\""); break;
                case '\n': out.append("\\n"); break;
                case '\r': out.append("\\r"); break;
                case '\t': out.append("\\t"); break;
                default:
                    if (c < 0x20) {
                        out.append(String.format(Locale.ROOT, "\\u%04x", (int) c));
                    } else {
                        out.append(c);
                    }
            }
        }
        return out.toString();
    }

    /** Spider texture re-encode: pick a format, run natively, open the result. */
    private void showTextureFormatDialog() {
        final long target = session;
        if (target == 0) return;
        final String[] names = NativeBridge.textureFormatNames();
        final String[] labels = NativeBridge.textureFormatLabels();
        if (names == null || labels == null || names.length != labels.length) return;
        final android.widget.CheckBox dx10 = new android.widget.CheckBox(this);
        dx10.setText("Always write the DX10 header");
        dx10.setPadding(dp(8), dp(4), dp(8), dp(4));
        new PanelBuilder()
                .setTitle(blackWidowState.reencodeRebuildsContainer
                        ? "Texture format (whole .PAC)" : "Texture format")
                .setItems(labels, (dialog, which) -> runTextureReencode(
                        target, names[which], dx10.isChecked()))
                .setView(dx10)
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void runTextureReencode(long target, String format, boolean dx10) {
        final String title = titleView.getText().toString();
        notice("Re-encoding textures to " + format.toUpperCase(java.util.Locale.ROOT) + "…",
                Toast.LENGTH_SHORT);
        new Thread(() -> {
            final long result = NativeBridge.reencodeTextures(target, format, dx10);
            final String detail = NativeBridge.reencodeTexturesDetail();
            runOnUiThread(() -> {
                if (result == 0) {
                    notice("Format change failed: " + detail, Toast.LENGTH_LONG);
                    return;
                }
                final String name = NativeBridge.sourceFileName(result);
                navigateToSession(result, (name.isEmpty() ? title : name)
                        + " · " + format.toUpperCase(java.util.Locale.ROOT));
                final int newline = detail.indexOf('\n');
                notice((newline > 0 ? detail.substring(0, newline) : detail)
                        + " — use ⋮ → Save file…", Toast.LENGTH_LONG);
            });
        }, "texture-reencode").start();
    }

    private void chooseSaveSource() {
        if (session == 0 || !blackWidowState.canSaveSource) return;
        pendingSaveSession = session;
        String name = NativeBridge.sourceFileName(session);
        if (name == null || name.isEmpty()) name = "resource.bin";
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/octet-stream");
        intent.putExtra(Intent.EXTRA_TITLE, name);
        startActivityForResult(intent, REQUEST_SAVE_SOURCE);
    }

    private void saveSourceToUri(Uri target) {
        boolean saved = false;
        if (target != null && pendingSaveSession != 0) {
            try (android.os.ParcelFileDescriptor pfd =
                         getContentResolver().openFileDescriptor(target, "rwt")) {
                saved = pfd != null && NativeBridge.writeSource(pendingSaveSession, pfd.getFd());
            } catch (Exception ignored) {
                saved = false;
            }
        }
        pendingSaveSession = 0;
        notice(saved ? "File saved" : "Could not save the file", Toast.LENGTH_LONG);
    }

    private void exportInfoToUri(Uri target) {
        boolean saved = false;
        if (target != null && !pendingInfoExportText.isEmpty()) {
            try (OutputStream output = getContentResolver().openOutputStream(target, "w")) {
                if (output != null) {
                    output.write(pendingInfoExportText.getBytes(StandardCharsets.UTF_8));
                    output.flush();
                    saved = true;
                }
            } catch (Exception ignored) {
                saved = false;
            }
        }
        notice(saved ? "Info saved" : "Could not save info", Toast.LENGTH_LONG);
    }

    private String nativeBuildIdentity() {
        try {
            String identity = NativeBridge.buildIdentity();
            if (identity == null || identity.trim().isEmpty()) return "unknown";
            return identity.trim();
        } catch (RuntimeException error) {
            return "unknown";
        }
    }

    private File diagnosticsFile() {
        return new File(getFilesDir(), "diagnostics.log");
    }

    private String sanitizeDiagnosticField(String value) {
        if (value == null || value.isEmpty()) return "unknown";
        String sanitized = value.replace('\n', ' ').replace('\r', ' ').replace('\t', ' ');
        if (sanitized.contains("content://")) return "[content-uri]";
        return sanitized.length() <= 180 ? sanitized : sanitized.substring(0, 180);
    }

    private void appendDiagnostics(String route, String name, long openedHandle,
                                   String rejectedDetail) {
        StringBuilder entry = new StringBuilder();
        entry.append("[")
                .append(LocalDateTime.now().format(DIAGNOSTICS_TIME_FORMAT))
                .append("] build=")
                .append(sanitizeDiagnosticField(nativeBuildIdentity()))
                .append(" route=")
                .append(sanitizeDiagnosticField(route))
                .append(" name=")
                .append(sanitizeDiagnosticField(name))
                .append("\n");

        if (openedHandle != 0) {
            String nativeDiagnostic = "";
            try {
                nativeDiagnostic = NativeBridge.diagnostics(openedHandle);
            } catch (RuntimeException ignored) {}
            entry.append("  result=OPENED");
            if (nativeDiagnostic == null || nativeDiagnostic.isEmpty()) {
                entry.append(" detail=native diagnostics unavailable");
            } else {
                entry.append(" ")
                        .append(nativeDiagnostic.replace("\r", "")
                                .replace("\n", "\n  "));
            }
            entry.append("\n");
        } else {
            entry.append("  result=REJECTED detail=")
                    .append(sanitizeDiagnosticField(
                            rejectedDetail == null || rejectedDetail.isEmpty()
                                    ? "not accepted by any native module"
                                    : rejectedDetail))
                    .append("\n");
        }

        try (FileOutputStream output = new FileOutputStream(diagnosticsFile(), true)) {
            output.write(entry.toString().getBytes(StandardCharsets.UTF_8));
        } catch (Exception ignored) {
            // Diagnostics are evidence only. Logging failure must never block opening.
        }
    }

    private String readDiagnosticsPreview() {
        File file = diagnosticsFile();
        if (!file.isFile() || file.length() == 0) return "No diagnostics recorded yet.";
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long length = input.length();
            long start = Math.max(0L, length - DIAGNOSTICS_PREVIEW_BYTES);
            input.seek(start);
            int count = (int) (length - start);
            byte[] bytes = new byte[count];
            input.readFully(bytes);
            String text = new String(bytes, StandardCharsets.UTF_8);
            if (start > 0) {
                return "… showing last " + count + " of " + length + " bytes …\n\n" + text;
            }
            return text;
        } catch (Exception error) {
            return "Diagnostics log could not be read.";
        }
    }

    private void showDiagnosticsDialog() {
        File file = diagnosticsFile();
        StringBuilder text = new StringBuilder();
        text.append("DMC Native Reader ").append(BuildConfig.VERSION_NAME).append("\n")
                .append("Native build: ").append(nativeBuildIdentity()).append("\n")
                .append("Log: ")
                .append(file.isFile() ? file.length() + " bytes" : "not created")
                .append("\n\n")
                .append(readDiagnosticsPreview());

        final AlertDialog[] shown = new AlertDialog[1];
        Button export = makeSquareButton("\u21e9", "Export diagnostics", 24f);
        export.setOnClickListener(v -> {
            if (shown[0] != null) shown[0].dismiss();
            chooseDiagnosticsExportDestination();
        });
        shown[0] = showPanel("Diagnostics / build info", panelText(text.toString(), 12f), export,
                "Clear log", this::confirmClearDiagnosticsLog);
    }

    private void chooseDiagnosticsExportDestination() {
        File file = diagnosticsFile();
        if (!file.isFile() || file.length() == 0) {
            Toast.makeText(this, "No diagnostics to export", Toast.LENGTH_LONG).show();
            return;
        }
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/plain");
        intent.putExtra(Intent.EXTRA_TITLE,
                "dmc-native-reader-diagnostics-" + nativeBuildIdentity() + ".log");
        startActivityForResult(intent, REQUEST_EXPORT_DIAGNOSTICS);
    }

    private void exportDiagnosticsToUri(Uri target) {
        File file = diagnosticsFile();
        if (!file.isFile()) {
            Toast.makeText(this, "No diagnostics to export", Toast.LENGTH_LONG).show();
            return;
        }
        try (FileInputStream input = new FileInputStream(file);
             OutputStream output = getContentResolver().openOutputStream(target, "w")) {
            if (output == null) throw new FileNotFoundException("No output stream");
            byte[] buffer = new byte[16 * 1024];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                if (count > 0) output.write(buffer, 0, count);
            }
            output.flush();
            Toast.makeText(this, "Diagnostics exported", Toast.LENGTH_LONG).show();
        } catch (Exception error) {
            Toast.makeText(this, "Could not export diagnostics", Toast.LENGTH_LONG).show();
        }
    }

    private void confirmClearDiagnosticsLog() {
        new PanelBuilder()
                .setTitle("Clear diagnostics log?")
                .setMessage("This removes only the private diagnostics history. Resources and sessions are unchanged.")
                .setPositiveButton("Clear", (dialog, which) -> {
                    File file = diagnosticsFile();
                    boolean cleared = !file.exists() || file.delete();
                    Toast.makeText(this,
                            cleared ? "Diagnostics log cleared" : "Could not clear diagnostics log",
                            Toast.LENGTH_LONG).show();
                })
                .setNegativeButton("Cancel", null)
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
            notice("Open a MOD scene first", Toast.LENGTH_LONG);
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

    private void showCompositePartsDialog() {
        final int partCount = NativeBridge.compositePartCount(session);
        if (!isRootScene() || partCount <= 1) return;

        String[] rows = new String[partCount];
        for (int index = 0; index < partCount; ++index) {
            String state = NativeBridge.compositePartState(session, index);
            rows[index] = state == null || state.isEmpty()
                    ? compositePartLabel(index) + "\nstate unavailable"
                    : state;
        }

        new PanelBuilder()
                .setTitle("Composite MOD parts")
                .setItems(rows, (dialog, which) ->
                        showCompositePartActions(which))
                .setNegativeButton("Close", null)
                .show();
    }

    private void showCompositePartActions(int partIndex) {
        final String state = NativeBridge.compositePartState(session, partIndex);
        final String title = compositePartLabel(partIndex);
        if (partIndex <= 0) {
            new PanelBuilder()
                    .setTitle(title)
                    .setMessage(state == null || state.isEmpty()
                            ? "State unavailable"
                            : state)
                    .setPositiveButton("Close", null)
                    .show();
            return;
        }

        String[] actions = new String[] {
                "Place on primary host joint…",
                "Reset to source coordinates",
                "Show native state"
        };
        new PanelBuilder()
                .setTitle(title)
                .setItems(actions, (dialog, which) -> {
                    if (which == 0) {
                        showHostJointDialog(partIndex);
                    } else if (which == 1) {
                        resetCompositePartPlacement(partIndex);
                    } else {
                        new PanelBuilder()
                                .setTitle(title)
                                .setMessage(state == null || state.isEmpty()
                                        ? "State unavailable"
                                        : state)
                                .setPositiveButton("Close", null)
                                .show();
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void resetCompositePartPlacement(int childPartIndex) {
        String status = NativeBridge.resetModPartPlacement(session, childPartIndex);
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
    }

    private void showPlaceModPartDialog() {
        final int partCount = NativeBridge.compositePartCount(session);
        if (!isRootScene() || partCount <= 1) return;

        String[] children = new String[partCount - 1];
        for (int index = 1; index < partCount; ++index) {
            children[index - 1] = compositePartLabel(index);
        }

        new PanelBuilder()
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
                        session, childPartIndex);
        final int[] selected = {
                defaultSelector >= 0 && defaultSelector < nodeCount
                        ? defaultSelector : -1
        };
        final String hostName = compositePartLabel(hostPartIndex);
        final String childName = compositePartLabel(childPartIndex);

        new PanelBuilder()
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

        new PanelBuilder()
                .setTitle("Reset MOD part placement")
                .setItems(children, (dialog, which) -> {
                    final int childPartIndex = which + 1;
                    resetCompositePartPlacement(childPartIndex);
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
        new PanelBuilder()
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
            renderView.resetView();  // also leaves the fly camera
            applyResourceUiState();
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
            if (requestCode == REQUEST_EXPORT_INFO) pendingInfoExportText = "";
            if (requestCode == REQUEST_SAVE_SOURCE) pendingSaveSession = 0;
            return;
        }

        if (requestCode == REQUEST_SAVE_SOURCE) {
            saveSourceToUri(data.getData());
            return;
        }

        if (requestCode == REQUEST_EXPORT_INFO) {
            Uri target = data.getData();
            if (target != null) exportInfoToUri(target);
            pendingInfoExportText = "";
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

        if (requestCode == REQUEST_EXPORT_DIAGNOSTICS) {
            Uri target = data.getData();
            if (target != null) exportDiagnosticsToUri(target);
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

        if (requestCode == REQUEST_ROOM) {
            if (data != null && data.getData() != null) chooseRoom(data.getData());
            return;
        }
        if (requestCode == REQUEST_STAGE_CONFIG) {
            if (data != null && data.getData() != null) loadStageConfig(data.getData());
            return;
        }
        if (requestCode == REQUEST_CUTSCENE_SOUND) {
            if (data != null && data.getData() != null) loadCutsceneSound(data.getData());
            return;
        }
        if (requestCode == REQUEST_ROOM_EFFECTS) {
            if (data != null && data.getData() != null) chooseRoomEffects(data.getData());
            return;
        }
        if (requestCode == REQUEST_ADD_PAC) {
            Uri uri = data.getData();
            if (uri == null) return;
            persistUriPermission(uri, data.getFlags(), false);
            assembleWithAddedPacs(uri);
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
                composeCurrentWithAdditionalMods(added, "add-mod");
            } else {
                ArrayList<Uri> combined = new ArrayList<>(modelPartUris);
                for (Uri uri : added) if (!combined.contains(uri)) combined.add(uri);
                openCompositeUris(combined, true, "add-mod");
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
            openUri(uris.get(0), "picker");
        } else {
            openCompositeUris(uris, false, "multi-mod");
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
        if (ROLE_MOTION.equals(role)) refreshMotionStrip();
        rebuildInfo(titleView.getText().toString());
        applyResourceUiState();
        notice(added + " " + role + " resource(s) staged", Toast.LENGTH_LONG);
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
                openCompositeUris(uris, false, "external");
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
            openUri(uri, "external");
        }
    }

    private void resetCompositionState() {
        modelPartUris.clear();
        modelPartPtxUris.clear();
        sharedModelPtxUri = null;
        stagedAssets.clear();
        selectedMotionIndex = -1;
        selectedScriptIndex = -1;
        assembledPacUri = null;
        stagePacUri = null;
        addedPacUris.clear();
    }

    private void showIdleStatus() {
        titleView.setText("DMC Native Reader");
        blackWidowState = BlackWidowState.empty();
        pendingPtxPart = -1;
        pendingExportSession = 0;
        pendingInfoExportText = "";
        resetCompositionState();
        // The core writes the start Info from its module registry.
        final String welcome = NativeBridge.welcomeText(BuildConfig.VERSION_NAME);
        setInfo(welcome == null ? "DMC Native Reader " + BuildConfig.VERSION_NAME : welcome);
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
        stopSound();
        session = handle;
        selectedMotionIndex = -1;
        selectedScriptIndex = -1;
        titleView.setText(name);
        renderView.setSession(session);
        refreshBlackWidowState();
        rebuildInfo(name);
        applyResourceUiState();
    }

    private String nonCanonicalNotes() {
        if (session == 0) return "";
        final String notes = NativeBridge.nonCanonicalNotes(session);
        return notes == null ? "" : notes;
    }

    private void refreshNonCanonicalBadge() {
        if (nonCanonicalBadge == null) return;
        nonCanonicalBadge.setVisibility(nonCanonicalNotes().isEmpty() ? View.GONE : View.VISIBLE);
    }

    private void showNonCanonicalNotes() {
        final String notes = nonCanonicalNotes();
        if (notes.isEmpty()) return;
        new PanelBuilder()
                .setTitle("▲ Shown, not canonical")
                .setMessage("This view is displayed, but part of it was not read the canonical "
                        + "way the game data is specified:\n\n" + notes)
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }

    private void rebuildInfo(String name) {
        final String inspection = session == 0 ? "" : NativeBridge.inspection(session);
        final String nativeInfo = session == 0 ? "" : NativeBridge.info(session);

        StringBuilder details = new StringBuilder();
        details.append(name).append("\n\nSTRUCTURE\n");
        details.append(inspection == null || inspection.isEmpty()
                ? "No typed inspection document.\n"
                : inspection);
        final String nonCanonical = nonCanonicalNotes();
        if (!nonCanonical.isEmpty()) {
            details.append("\n▲ NOT CANONICAL (shown anyway)\n").append(nonCanonical).append("\n");
        }
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
            details.append("MOT cards play on tap (tap again to pause). Physics/cloth companions stay staged: their native runtime is not promoted yet.\n");
        }
        setInfo(details.toString());
        refreshNonCanonicalBadge();
    }

    private void openUri(Uri uri, String route) {
        closeAllSessions();
        resetCompositionState();
        String name = displayName(uri);
        titleView.setText(name);

        long opened;
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
            if (pfd == null) throw new FileNotFoundException("No file descriptor");
            opened = NativeBridge.open(pfd.getFd(), name);
        } catch (Exception error) {
            appendDiagnostics(route, name, 0,
                    "platform transport failure: " + error.getClass().getSimpleName());
            setInfo(name + "\nOpen failed: " + error);
            applyResourceUiState();
            notice("Could not read file", Toast.LENGTH_LONG);
            return;
        }

        // A stage's effect bank opened on its own is also the stage effects of the room.
        if (opened != 0 && name.toLowerCase(java.util.Locale.ROOT).matches("st\\d+.*effect.*")) {
            chooseRoomEffects(uri);
        }

        if (opened == 0) {
            appendDiagnostics(route, name, 0, "not accepted by any native module");
            setInfo(name + "\nRejected: supported route failed structural validation or format is outside MOD / SCM / DDS / PTX / PAC / MOT.");
            applyResourceUiState();
            notice("Unsupported or malformed DMC resource", Toast.LENGTH_LONG);
            return;
        }

        // A PAC opens as an assembled character/scene when it holds MODs; the
        // raw archive stays browsable from ⋮ (read-only, nothing is written).
        enemyVariant = 0;
        final long assembled = NativeBridge.assemblePac(opened, name);
        if (assembled != 0) {
            NativeBridge.close(opened);
            opened = assembled;
            assembledPacUri = uri;
            name = name + " · assembled";
        } else if (NativeBridge.isStageSession(opened)
                && NativeBridge.childResourceCount(opened) > 0) {
            // A stage archive opens as its assembled scene (the same build as
            // the room), not as the file gallery.
            long stage = 0;
            try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
                if (pfd != null) stage = NativeBridge.openStage(pfd.getFd(), name);
            } catch (Exception ignored) {
                stage = 0;
            }
            if (stage != 0) {
                NativeBridge.close(opened);
                opened = stage;
                stagePacUri = uri;
                name = name + " · stage";
            }
        }

        appendDiagnostics(route, name, opened, null);
        activateSession(opened, name);
        if (assembledPacUri == null && blackWidowState.canAddModelPart) {
            modelPartUris.add(uri);
            modelPartPtxUris.add(null);
        }
    }

    private void composeCurrentWithAdditionalMods(ArrayList<Uri> requested, String route) {
        if (session == 0 || requested == null || requested.isEmpty()) return;
        if (NativeBridge.compositePartCount(session) != 0) {
            ArrayList<Uri> combined = new ArrayList<>(modelPartUris);
            for (Uri uri : requested) if (!combined.contains(uri)) combined.add(uri);
            openCompositeUris(combined, true, route);
            return;
        }

        ArrayList<Uri> additions = new ArrayList<>();
        for (Uri uri : requested) {
            if (uri != null && !modelPartUris.contains(uri) && !additions.contains(uri)) {
                additions.add(uri);
            }
        }
        if (additions.isEmpty()) {
            notice("No new MOD parts selected", Toast.LENGTH_LONG);
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
        String failureName = "MOD composite · " + totalParts + " parts";
        try {
            for (int index = 0; index < additions.size(); ++index) {
                Uri uri = additions.get(index);
                final int target = index + 1;
                names[target] = displayName(uri);
                failureName = names[target];
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
                failureName = "MOD composite · " + totalParts + " parts";
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
            appendDiagnostics(route, failureName, 0,
                    failure == null
                            ? "native composition rejected"
                            : (failure.startsWith("Could not read selected MOD files:")
                                    ? "platform transport failure"
                                    : failure));
            Toast.makeText(this,
                    failure == null ? "MOD composition failed" : failure,
                    Toast.LENGTH_LONG).show();
            return;
        }

        appendDiagnostics(route, "MOD composite · " + totalParts + " parts", composite, null);
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

    private void openCompositeUris(ArrayList<Uri> uris, boolean preserveAssets, String route) {
        if (uris.size() < 2) {
            if (!uris.isEmpty() && !preserveAssets) openUri(uris.get(0), route);
            return;
        }

        ArrayList<Uri> previousPtx = preserveAssets
                ? new ArrayList<>(modelPartPtxUris)
                : new ArrayList<>();
        long[] handles = new long[uris.size()];
        String[] names = new String[uris.size()];
        long composite = 0;
        String failure = null;
        String failureName = "MOD composite · " + uris.size() + " parts";

        try {
            for (int index = 0; index < uris.size(); ++index) {
                Uri uri = uris.get(index);
                names[index] = displayName(uri);
                failureName = names[index];
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
                failureName = "MOD composite · " + uris.size() + " parts";
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
            appendDiagnostics(route, failureName, 0,
                    failure == null
                            ? "native composition rejected"
                            : (failure.startsWith("Could not read selected MOD files:")
                                    ? "platform transport failure"
                                    : failure));
            Toast.makeText(this,
                    failure == null ? "MOD composition failed" : failure,
                    Toast.LENGTH_LONG).show();
            return;
        }

        appendDiagnostics(route, "MOD composite · " + uris.size() + " parts", composite, null);
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
            notice("Could not read PTX", Toast.LENGTH_LONG);
            return;
        }

        refreshBlackWidowState();
        final String diagnostic = NativeBridge.textureAttachmentInfo(session);
        if (attached) {
            if (remember) rememberPtxForPart(uri, partIndex);
            renderView.renderNow();
            rebuildInfo(titleView.getText().toString());
            applyResourceUiState();
            notice(diagnostic == null || diagnostic.isEmpty()
                            ? "PTX textures attached"
                            : diagnostic, Toast.LENGTH_LONG);
        } else {
            rebuildInfo(titleView.getText().toString());
            applyResourceUiState();
            notice(diagnostic == null || diagnostic.isEmpty()
                            ? "PTX could not be matched to this model"
                            : diagnostic, Toast.LENGTH_LONG);
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
        final int width;
        final int height;
        // The core chooses the picture and its size (a UV map at its
        // texture's resolution, anything else its preview).
        final int[] size = NativeBridge.exportImageSize(handle);
        if (size == null || size.length < 2) return null;
        width = size[0];
        height = size[1];

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

        final boolean filled = NativeBridge.renderExportImage(handle, bitmap);
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
        notice(saved ? "PNG saved" : "Could not export PNG", Toast.LENGTH_LONG);
    }

    private void exportGalleryToTree(Uri treeUri) {
        final long handle = pendingExportSession != 0 ? pendingExportSession : session;
        if (handle == 0) return;
        final int count = NativeBridge.childResourceCount(handle);
        if (count <= 0) {
            notice("No gallery images to export", Toast.LENGTH_LONG);
            return;
        }

        final Uri parent;
        try {
            String documentId = DocumentsContract.getTreeDocumentId(treeUri);
            parent = DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId);
        } catch (RuntimeException error) {
            notice("Selected folder is not writable", Toast.LENGTH_LONG);
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

        notice("PNG export: " + saved + "/" + count, Toast.LENGTH_LONG);
    }

    private void openChildResource(int index, String childTitle) {
        if (session == 0) return;
        final long child = NativeBridge.openChild(session, index);
        if (child == 0) {
            notice("Could not open child resource", Toast.LENGTH_LONG);
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

    @Override protected void onResume() {
        super.onResume();
        activityResumed = true;
        if (cameraPanel != null) updateGyro();
        if (cameraPanel != null) updateWalk(false);
    }

    @Override protected void onPause() {
        activityResumed = false;
        if (arWalker != null) arWalker.stop();
        stopGyro();
        stopRecording();
        prefs().edit().putFloat(PREF_POLARIZER, NativeBridge.cameraPolarizer(0.0f)).apply();
        super.onPause();
    }

    @Override protected void onDestroy() {
        if (arWalker != null) arWalker.close();
        stopRecording();
        stopSound();
        closeAllSessions();
        super.onDestroy();
    }
}
