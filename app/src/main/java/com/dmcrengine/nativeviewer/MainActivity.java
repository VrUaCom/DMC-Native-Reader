package com.dmcrengine.nativeviewer;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.ArrayDeque;

public final class MainActivity extends Activity {
    private static final int REQUEST_OPEN = 1001;
    private static final int REQUEST_ATTACH_PTX = 1002;
    private static final int TOOL_SIZE_DP = 48;
    private static final int TOOL_GAP_DP = 4;

    private static final class NavigationEntry {
        final long session;
        final String title;

        NavigationEntry(long session, String title) {
            this.session = session;
            this.title = title;
        }
    }

    private DmcRenderView renderView;
    private ChildResourceBrowserView childBrowser;
    private TextView titleView;
    private Button parentButton;
    private Button ptxButton;
    private Button resetButton;
    private Button wireButton;
    private Button hierarchyButton;
    private Button uvButton;
    private Button infoButton;

    private long session;
    private final ArrayDeque<NavigationEntry> navigation = new ArrayDeque<>();
    private ResourceUiState uiState = ResourceUiState.empty();
    private BlackWidowState blackWidowState = BlackWidowState.empty();
    private boolean spatialHierarchyAvailable;
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

    private void applyPrimaryPresentation() {
        final boolean hasSession = session != 0;
        final boolean childBrowserMode = hasSession
                && uiState.hasChildResources
                && !uiState.canRender
                && !uiState.canPreviewImage
                && NativeBridge.childResourceCount(session) > 0;

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

        // Back is a permanent top-left navigation control. Inside a child it
        // returns to the parent resource; at the top level it leaves the viewer.
        parentButton.setVisibility(View.VISIBLE);

        // Spider Black Widow owns whether a companion action is available and
        // whether it is active. Android only projects the returned typed state.
        final boolean ptxAvailable = canAttachPtx();
        ptxButton.setVisibility(ptxAvailable ? View.VISIBLE : View.GONE);
        syncToggleButton(ptxButton, ptxAvailable, hasAttachedPtx());

        setToolAvailable(resetButton, hasSession && uiState.canRender);
        syncToggleButton(wireButton,
                hasSession && uiState.canWireframe && !renderView.isUvLayoutVisible(),
                renderView.isWireframe());

        final boolean hierarchyAvailable = hasSession
                && uiState.canShowHierarchy
                && spatialHierarchyAvailable
                && !renderView.isUvLayoutVisible();
        renderView.setHierarchyAvailable(hierarchyAvailable);
        syncToggleButton(hierarchyButton,
                hierarchyAvailable,
                renderView.isHierarchyVisible());

        syncToggleButton(uvButton,
                hasSession && uiState.canRender && uiState.hasUvCoordinates,
                renderView.isUvLayoutVisible());

        setToolAvailable(infoButton,
                hasSession ? uiState.canInspect : !infoText.isEmpty());
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

        ptxButton = makeSquareButton(".PTX", "Attach PTX texture companion", 12f);
        ptxButton.setVisibility(View.GONE);
        ptxButton.setOnClickListener(v -> choosePtxCompanion());
        header.addView(ptxButton, new LinearLayout.LayoutParams(
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

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER);
        bar.setPadding(dp(8), dp(6), dp(8), dp(8));

        Button open = makeSquareButton("↑", "Open MOD / SCM / DDS / PTX", 28f);
        open.setOnClickListener(v -> chooseFile());
        addToolButton(bar, open);

        resetButton = makeSquareButton("🔄", "Reset view", 20f);
        resetButton.setOnClickListener(v -> renderView.resetView());
        addToolButton(bar, resetButton);

        wireButton = makeSquareButton("W", "Wireframe", 18f);
        wireButton.setOnClickListener(v -> {
            renderView.toggleWireframe();
            applyResourceUiState();
        });
        addToolButton(bar, wireButton);

        hierarchyButton = makeSquareButton("🦴", "Bones / hierarchy", 20f);
        hierarchyButton.setOnClickListener(v -> {
            renderView.toggleHierarchy();
            applyResourceUiState();
        });
        addToolButton(bar, hierarchyButton);

        uvButton = makeSquareButton("UV", "UV layout", 14f);
        uvButton.setOnClickListener(v -> {
            renderView.toggleUvLayout();
            applyResourceUiState();
        });
        addToolButton(bar, uvButton);

        infoButton = makeSquareButton("\u2139", "Resource information", 22f);
        infoButton.setOnClickListener(v -> showInfoDialog());
        addToolButton(bar, infoButton);

        root.addView(bar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        setContentView(root);
        showIdleStatus();
    }

    private void setInfo(String text) {
        infoText = text == null ? "" : text;
    }

    private void showInfoDialog() {
        TextView details = new TextView(this);
        details.setText(infoText.isEmpty() ? "No resource information yet." : infoText);
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

    private void choosePtxCompanion() {
        if (!canAttachPtx()) return;
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

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;
        if (requestCode != REQUEST_OPEN && requestCode != REQUEST_ATTACH_PTX) return;

        Uri uri = data.getData();
        final int flags = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try {
            getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (SecurityException ignored) {}

        if (requestCode == REQUEST_ATTACH_PTX) {
            attachPtxUri(uri);
        } else {
            openUri(uri);
        }
    }

    private void handleIncomingIntent(Intent intent) {
        if (intent == null) {
            showIdleStatus();
            return;
        }

        Uri uri = intent.getData();
        if (uri == null && Intent.ACTION_SEND.equals(intent.getAction())) {
            try {
                Object value = intent.getParcelableExtra(Intent.EXTRA_STREAM);
                if (value instanceof Uri) uri = (Uri) value;
            } catch (RuntimeException ignored) {}
        }
        if (uri == null) {
            ClipData clip = intent.getClipData();
            if (clip != null && clip.getItemCount() > 0) {
                uri = clip.getItemAt(0).getUri();
            }
        }

        if (uri == null) {
            showIdleStatus();
        } else {
            openUri(uri);
        }
    }

    private void showIdleStatus() {
        titleView.setText("DMC Native Reader");
        uiState = ResourceUiState.empty();
        blackWidowState = BlackWidowState.empty();
        spatialHierarchyAvailable = false;
        setInfo("DMC Native Reader " + BuildConfig.VERSION_NAME + "\n"
                + "Architecture v2 core: MOD / SCM / DDS / PTX.\n"
                + "Unpromoted DMC families are intentionally excluded from main.\n\n"
                + "Open a supported resource from My Files or use ↑.");
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
        uiState = ResourceUiState.fromCapabilities(NativeBridge.capabilities(session));
        refreshBlackWidowState();
        spatialHierarchyAvailable = NativeBridge.hierarchyAvailable(session);
        rebuildInfo(name);
        applyResourceUiState();
    }

    private void rebuildInfo(String name) {
        final String inspection = NativeBridge.inspection(session);
        final String nativeInfo = NativeBridge.info(session);

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
        setInfo(details.toString());
    }

    private void openUri(Uri uri) {
        closeAllSessions();
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
    }

    private void attachPtxUri(Uri uri) {
        if (!canAttachPtx()) return;
        final String ptxName = displayName(uri);
        final boolean attached;
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
            if (pfd == null) throw new FileNotFoundException("No file descriptor");
            attached = NativeBridge.attachPtx(session, pfd.getFd(), ptxName);
        } catch (Exception error) {
            Toast.makeText(this, "Could not read PTX", Toast.LENGTH_LONG).show();
            return;
        }

        // Refresh typed application state after the native action. Diagnostic
        // text below is presentation only and never controls the button state.
        refreshBlackWidowState();
        final String diagnostic = NativeBridge.textureAttachmentInfo(session);
        if (attached) {
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

    private void openChildResource(int index, String childTitle) {
        if (session == 0) return;
        final long child = NativeBridge.openChild(session, index);
        if (child == 0) {
            Toast.makeText(this, "Could not open child resource", Toast.LENGTH_LONG).show();
            return;
        }

        navigation.push(new NavigationEntry(session, titleView.getText().toString()));
        activateSession(child, childTitle);
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
        uiState = ResourceUiState.empty();
        blackWidowState = BlackWidowState.empty();
        spatialHierarchyAvailable = false;

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
