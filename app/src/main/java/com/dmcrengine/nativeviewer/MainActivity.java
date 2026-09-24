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
    private static final int REQUEST_ADD_PAC = 1011;
    private static final int REQUEST_ROOM = 1012;
    private static final String PREFS = "viewer";
    private static final String PREF_ROOM_NAME = "room.name";
    private static final String PREF_ROOM_SHOWN = "room.shown";
    private static final String ROOM_FILE = "room.bin";
    private static final int REQUEST_STAGE_MOTION = 1006;
    private static final int REQUEST_STAGE_TEXTURE = 1007;
    private static final int REQUEST_STAGE_PHYSICS = 1008;
    private static final int REQUEST_STAGE_CLOTH = 1009;
    private static final int REQUEST_STAGE_OTHER = 1010;


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
    // Orange warning: something on screen was read through a non-canonical path.
    private Button nonCanonicalBadge;
    private static final int NON_CANONICAL_ORANGE = 0xffff9800;
    private Button resetButton;
    private Button wireButton;
    private Button hierarchyButton;
    private Button uvButton;
    private Button shadowButton;
    private Button collisionButton;
    // Position in the collision cycle: -1 all attacks, then each used id.
    private int collisionCursor = -2;
    private Button infoButton;
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

    private long session;
    private long pendingExportSession;
    private int pendingPtxPart = -1;
    private int selectedMotionIndex = -1;
    private LinearLayout headerBar;
    // Hidden by the top-edge swipe: bars and the visibility each had.
    private boolean uiHidden;
    private final int[] barVisibility = new int[4];
    // Archive the root scene was assembled from (re-opened on demand so the
    // per-file browser owns an independent read-only handle).
    private Uri assembledPacUri;
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

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        applyViewerSettings();
        renderView.setGestures(prefs().getInt(PREF_GESTURES, DmcRenderView.G_ALL));
        restoreRoom();
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

        syncToggleButton(shadowButton,
                hasSession && NativeBridge.hasShadows(session) && !renderView.isUvLayoutVisible(),
                renderView.isShadowVisible());

        syncToggleButton(collisionButton,
                hasSession && NativeBridge.hasCollision(session) && !renderView.isUvLayoutVisible(),
                renderView.isCollisionVisible());

        syncToggleButton(uvButton,
                hasSession && (blackWidowState.canShowUv || blackWidowState.canInspectUv),
                renderView.isUvLayoutVisible());

        setToolAvailable(infoButton,
                hasSession ? blackWidowState.canInspect : !infoText.isEmpty());
        refreshMotionStrip();
        refreshVariantBar();
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

        FrameLayout viewport = new FrameLayout(this);
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

        shadowButton = makeSquareButton("\u25D0", "Shadows (SHW)", 20f);
        shadowButton.setOnClickListener(v -> {
            if (session == 0 || !NativeBridge.hasShadows(session)) return;
            renderView.toggleShadows();
            applyResourceUiState();
        });
        addToolButton(bar, shadowButton);

        // Hitboxes: off -> every attack -> each attack id in turn -> off.
        collisionButton = makeSquareButton("\u25CE", "Attack collision (hitboxes)", 20f);
        collisionButton.setOnClickListener(v -> {
            if (session == 0 || !NativeBridge.hasCollision(session)) return;
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
        collisionButton.setOnLongClickListener(v -> {
            collisionCursor = -2;
            renderView.setCollisionVisible(false);
            applyResourceUiState();
            return true;
        });
        addToolButton(bar, collisionButton);

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
        setContentView(root);
        showIdleStatus();
    }

    // ---- Room: a stage archive shown around every model instead of the floor.

    private boolean roomLoaded;
    private String roomName = "";

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

    private void applyViewerSettings() {
        final android.content.SharedPreferences p = prefs();
        final int flags = (p.getBoolean(SET_SMOOTH, false) ? 1 << 9 : 0)
                | (p.getBoolean(SET_UNLIT, false) ? 1 << 10 : 0)
                | ((p.getInt(SET_BACKGROUND, 0) & 3) << 11);
        renderView.applySettings(p.getInt(SET_MAX_SIDE, 720), p.getInt(SET_FRAME_MS, 33),
                p.getFloat(SET_SPEED, 1.0f), flags, p.getBoolean(SET_SHADOWS, true));
    }

    private void roomToggle() {
        final boolean shown = !roomShown();
        prefs().edit().putBoolean(PREF_ROOM_SHOWN, shown).apply();
        renderView.setRoomVisible(shown && roomLoaded);
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
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        final TextView[] chips = new TextView[names.length];
        for (int i = 0; i < names.length; ++i) {
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
        block.addView(row, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
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

    /** Full-screen settings window: render quality, animation, room. */
    private void showSettingsDialog() {
        final android.app.Dialog dialog = new android.app.Dialog(this,
                android.R.style.Theme_DeviceDefault_NoActionBar);
        final android.content.SharedPreferences p = prefs();
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(16), dp(12), dp(16), dp(24));

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = new TextView(this);
        title.setText("Settings");
        title.setTextSize(22f);
        title.setTextColor(0xffffffff);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        header.addView(title, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        Button close = makeSquareButton("✕", "Close settings", 20f);
        close.setOnClickListener(v -> dialog.dismiss());
        header.addView(close, new LinearLayout.LayoutParams(dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP)));
        content.addView(header);

        final Runnable apply = this::applyViewerSettings;
        content.addView(sectionTitle("Render"));
        final int[] sides = {360, 540, 720, 1024};
        content.addView(choiceRow("Resolution (longest side, px)", new String[]{"360", "540", "720", "1024"},
                indexOf(sides, p.getInt(SET_MAX_SIDE, 720), 2),
                i -> { p.edit().putInt(SET_MAX_SIDE, sides[i]).apply(); apply.run(); }));
        final int[] frames = {50, 33, 16};
        content.addView(choiceRow("Animation frame rate", new String[]{"20 fps", "30 fps", "60 fps"},
                indexOf(frames, p.getInt(SET_FRAME_MS, 33), 1),
                i -> { p.edit().putInt(SET_FRAME_MS, frames[i]).apply(); apply.run(); }));
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

        content.addView(sectionTitle("Animation"));
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
            if (NativeBridge.roomSpotCount() > 1) addAction.accept("Next floor spot", this::roomNextSpot);
            addAction.accept("Remove room", () -> { roomRemove(); dialog.dismiss(); });
        }
        content.addView(roomActions);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(content);
        LinearLayout frame = new LinearLayout(this);
        frame.setOrientation(LinearLayout.VERTICAL);
        frame.setBackgroundColor(0xff16171c);
        applySystemBarInsets(frame);
        frame.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.MATCH_PARENT));
        dialog.setContentView(frame);
        dialog.show();
    }

    // ---- Gestures (DmcRenderView): the activity side and the Gestures window.

    private static final String PREF_GESTURES = "gestures.mask";

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
            if (motionBar != null && next < motionBar.getChildCount()) {
                final View card = motionBar.getChildAt(next);
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
            final View[] bars = {headerBar, variantScroll, motionScroll, toolScroll};
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
            }
            notice(uiHidden ? "Full view — swipe down from the top to bring the bars back" : "Bars shown",
                    Toast.LENGTH_SHORT);
        }

        @Override public void onScreenshot(Bitmap image) {
            saveScreenshot(image);
        }

        @Override public void onGestureNotice(String text) {
            notice(text, Toast.LENGTH_SHORT);
        }
    };

    /** PNG into Pictures/DMC Native Reader (Android 10+) or the app's picture folder. */
    private void saveScreenshot(Bitmap image) {
        final String name = "dmc-" + new java.text.SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)
                .format(new java.util.Date()) + ".png";
        new Thread(() -> {
            String where = null;
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    final android.content.ContentValues values = new android.content.ContentValues();
                    values.put(android.provider.MediaStore.Images.Media.DISPLAY_NAME, name);
                    values.put(android.provider.MediaStore.Images.Media.MIME_TYPE, "image/png");
                    values.put(android.provider.MediaStore.Images.Media.RELATIVE_PATH, "Pictures/DMC Native Reader");
                    final Uri target = getContentResolver().insert(
                            android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
                    if (target != null) {
                        try (OutputStream out = getContentResolver().openOutputStream(target)) {
                            if (out != null && image.compress(Bitmap.CompressFormat.PNG, 100, out)) {
                                where = "Pictures/DMC Native Reader/" + name;
                            }
                        }
                    }
                } else {
                    final File dir = getExternalFilesDir(android.os.Environment.DIRECTORY_PICTURES);
                    if (dir != null && (dir.isDirectory() || dir.mkdirs())) {
                        final File file = new File(dir, name);
                        try (OutputStream out = new java.io.FileOutputStream(file)) {
                            if (image.compress(Bitmap.CompressFormat.PNG, 100, out)) where = file.getPath();
                        }
                    }
                }
            } catch (Exception ignored) {
                where = null;
            }
            final String saved = where;
            runOnUiThread(() -> notice(saved != null ? "📸 Saved " + saved : "Screenshot failed",
                    Toast.LENGTH_LONG));
        }).start();
    }

    /** Full-screen list of the gestures, each switchable, plus the camera follow. */
    private void showGesturesDialog() {
        final android.app.Dialog dialog = new android.app.Dialog(this,
                android.R.style.Theme_DeviceDefault_NoActionBar);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(16), dp(12), dp(16), dp(24));

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = new TextView(this);
        title.setText("Gestures");
        title.setTextSize(22f);
        title.setTextColor(0xffffffff);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        header.addView(title, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        Button close = makeSquareButton("✕", "Close gestures", 20f);
        close.setOnClickListener(v -> dialog.dismiss());
        header.addView(close, new LinearLayout.LayoutParams(dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP)));
        content.addView(header);

        TextView always = new TextView(this);
        always.setTextColor(0xffc8ccd6);
        always.setTextSize(14f);
        always.setPadding(0, dp(8), 0, dp(4));
        always.setText("Always on: one finger turns the view, pinch zooms.");
        content.addView(always);

        content.addView(choiceRow("Camera (a four-finger tap switches it)",
                new String[]{"Follows the model", "Stays in place"}, renderView.isFollowing() ? 0 : 1,
                i -> renderView.setFollow(i == 0)));

        final Object[][] list = {
                {DmcRenderView.G_PAN, "Two fingers drag — pan the camera"},
                {DmcRenderView.G_TWIST, "Two fingers twist — turn the model in the room (the view when there is no room)"},
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

        ScrollView scroll = new ScrollView(this);
        scroll.addView(content);
        LinearLayout frame = new LinearLayout(this);
        frame.setOrientation(LinearLayout.VERTICAL);
        frame.setBackgroundColor(0xff16171c);
        applySystemBarInsets(frame);
        frame.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.MATCH_PARENT));
        dialog.setContentView(frame);
        dialog.show();
    }

    /** The ⋮ menu: a row of square shortcuts (settings first), then actions. */
    private void showCompanionMenu(View anchor) {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(0xff23252c);
        panel.setPadding(dp(8), dp(8), dp(8), dp(8));
        final android.widget.PopupWindow popup = new android.widget.PopupWindow(panel,
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT, true);
        popup.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(0xff23252c));
        popup.setOutsideTouchable(true);
        popup.setElevation(dp(8));

        LinearLayout icons = new LinearLayout(this);
        icons.setOrientation(LinearLayout.HORIZONTAL);
        final java.util.function.BiConsumer<Button, Runnable> addIcon = (button, action) -> {
            button.setOnClickListener(v -> { popup.dismiss(); action.run(); });
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(TOOL_SIZE_DP), dp(TOOL_SIZE_DP));
            params.setMarginEnd(dp(6));
            icons.addView(button, params);
        };
        addIcon.accept(makeSquareButton("⚙", "Settings", 22f), this::showSettingsDialog);
        addIcon.accept(makeSquareButton("\u270b", "Gestures", 22f), this::showGesturesDialog);
        if (roomLoaded) {
            Button room = makeSquareButton("⌂", roomShown() ? "Hide the room" : "Show the room", 22f);
            room.setAlpha(roomShown() ? 1f : 0.5f);
            addIcon.accept(room, this::roomToggle);
            if (NativeBridge.roomSpotCount() > 1) {
                addIcon.accept(makeSquareButton("⇄", "Next floor spot", 20f), this::roomNextSpot);
            }
        }
        panel.addView(icons);

        final java.util.function.BiConsumer<String, Runnable> addRow = (text, action) -> {
            TextView row = new TextView(this);
            row.setText(text);
            row.setTextSize(16f);
            row.setTextColor(0xffe8eaf0);
            row.setPadding(dp(8), dp(12), dp(16), dp(12));
            row.setOnClickListener(v -> { popup.dismiss(); action.run(); });
            panel.addView(row);
        };
        addRow.accept("Open / replace resource", this::chooseFile);
        if (isRootScene() && assembledPacUri != null) {
            addRow.accept("Add weapon / .PAC…", this::chooseAdditionalPac);
            addRow.accept("Browse .PAC files…", this::browseAssembledPac);
        }
        if (hasModCompositionContext()) addRow.accept("Add .MOD part(s)", this::chooseAdditionalMods);
        if (isRootScene() && canAttachPtx()) addRow.accept("Attach .PTX texture", this::choosePtxForCurrentSession);
        if (isRootScene() && blackWidowState.canStageCompanion) {
            addRow.accept("Add animation / motion…", () -> chooseStagedAssets(REQUEST_STAGE_MOTION, true));
            addRow.accept("Add texture asset (.TM2 / .DDS / …)", () -> chooseStagedAssets(REQUEST_STAGE_TEXTURE, true));
            addRow.accept("Add physics resource…", () -> chooseStagedAssets(REQUEST_STAGE_PHYSICS, true));
            addRow.accept("Add cloth resource…", () -> chooseStagedAssets(REQUEST_STAGE_CLOTH, true));
            addRow.accept("Add other companion…", () -> chooseStagedAssets(REQUEST_STAGE_OTHER, true));
        }
        popup.showAsDropDown(anchor);
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

        MotionEntry(String name, int libraryIndex, Uri uri) {
            this.name = name;
            this.libraryIndex = libraryIndex;
            this.uri = uri;
        }
    }

    private ArrayList<MotionEntry> motionEntries() {
        ArrayList<MotionEntry> result = new ArrayList<>();
        if (session != 0 && isRootScene()) {
            final int count = NativeBridge.motionLibraryCount(session);
            for (int index = 0; index < count; ++index) {
                result.add(new MotionEntry(
                        NativeBridge.motionLibraryName(session, index), index, null));
            }
        }
        for (StagedAsset asset : motionAssets()) {
            result.add(new MotionEntry(asset.name, -1, asset.uri));
        }
        return result;
    }

    private void refreshMotionStrip() {
        if (motionBar == null || motionScroll == null) return;
        motionBar.removeAllViews();
        ArrayList<MotionEntry> motions = motionEntries();
        if (motions.isEmpty() || !isRootScene()) {
            motionScroll.setVisibility(View.GONE);
            if (motions.isEmpty()) selectedMotionIndex = -1;
            return;
        }
        for (int index = 0; index < motions.size(); ++index) {
            final int motionIndex = index;
            MotionEntry entry = motions.get(index);
            final boolean active = index == selectedMotionIndex;
            Button button = makeSquareButton("", "Play animation " + entry.name, 11f);
            button.setText(motionCardLabel(entry.name));
            button.setActivated(active);
            button.setAlpha(active ? 1.0f : 0.72f);
            button.setOnClickListener(v -> selectMotion(motionIndex));
            addToolButton(motionBar, button);
        }
        motionScroll.setVisibility(uiHidden ? View.GONE : View.VISIBLE);
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

    // Tap a card: bind + play. Tap the playing card again: pause/resume.
    private void selectMotion(int index) {
        ArrayList<MotionEntry> motions = motionEntries();
        if (session == 0 || index < 0 || index >= motions.size()) return;
        if (index == selectedMotionIndex && NativeBridge.hasMotion(session)) {
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
        refreshMotionStrip();
        if (bound) renderView.startMotion();
        else renderView.renderNow();
        notice(bound ? entry.name + " ▶" : (report == null ? "Motion rejected" : report), Toast.LENGTH_LONG);
        rebuildInfo(titleView.getText().toString());
        if (report != null && !report.isEmpty()) {
            setInfo(infoText + "\nMOTION\n" + report + "\n");
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
        if (assembledPacUri == null) return;
        final String name = displayName(assembledPacUri);
        long archive = 0;
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(assembledPacUri)) {
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

        if (requestCode == REQUEST_ROOM) {
            if (data != null && data.getData() != null) chooseRoom(data.getData());
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
        assembledPacUri = null;
        addedPacUris.clear();
    }

    private void showIdleStatus() {
        titleView.setText("DMC Native Reader");
        blackWidowState = BlackWidowState.empty();
        pendingPtxPart = -1;
        pendingExportSession = 0;
        resetCompositionState();
        setInfo("DMC Native Reader " + BuildConfig.VERSION_NAME + "\n"
                + "Architecture v2 core: MOD / SCM / DDS / PTX / PAC / MOT (read-only).\n"
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
        selectedMotionIndex = -1;
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
        new AlertDialog.Builder(this)
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
            notice("Could not read file", Toast.LENGTH_LONG);
            return;
        }

        if (opened == 0) {
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
        }

        activateSession(opened, name);
        if (assembledPacUri == null && blackWidowState.canAddModelPart) {
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
            notice(failure == null ? "MOD composition failed" : failure, Toast.LENGTH_LONG);
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
        notice(combinedUris.size() + " MOD parts composed from the live base session", Toast.LENGTH_LONG);
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
            notice(failure == null ? "MOD composition failed" : failure, Toast.LENGTH_LONG);
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
        notice(uris.size() + " MOD parts composed in source coordinates", Toast.LENGTH_LONG);
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

    @Override protected void onDestroy() {
        closeAllSessions();
        super.onDestroy();
    }
}
