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
import java.io.OutputStream;
import java.util.ArrayDeque;
import java.util.ArrayList;

public final class MainActivity extends Activity {
    private static final int REQUEST_OPEN = 1001;
    private static final int REQUEST_ATTACH_PTX = 1002;
    private static final int REQUEST_EXPORT_SINGLE_PNG = 1003;
    private static final int REQUEST_EXPORT_GALLERY_TREE = 1004;
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
    private long pendingExportSession;
    private int pendingPtxPart = -1;
    private final ArrayDeque<NavigationEntry> navigation = new ArrayDeque<>();
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

        // Back is a permanent Android-shell navigation control. Black Widow
        // owns resource/action availability; Java only projects that state.
        parentButton.setVisibility(View.VISIBLE);

        final boolean ptxAvailable = canAttachPtx();
        ptxButton.setVisibility(ptxAvailable ? View.VISIBLE : View.GONE);
        syncToggleButton(ptxButton, ptxAvailable, hasAttachedPtx());

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
        ptxButton.setOnClickListener(v -> choosePtxForCurrentSession());
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

    private void choosePtxForCurrentSession() {
        if (!canAttachPtx()) return;
        final int partCount = NativeBridge.compositePartCount(session);
        if (partCount <= 0) {
            choosePtxCompanion(-1);
            return;
        }

        String[] names = new String[partCount];
        for (int index = 0; index < partCount; ++index) {
            String name = NativeBridge.compositePartName(session, index);
            names[index] = name == null || name.isEmpty()
                    ? "MOD part " + (index + 1)
                    : name;
        }
        new AlertDialog.Builder(this)
                .setTitle("Attach PTX to MOD part")
                .setItems(names, (dialog, which) -> choosePtxCompanion(which))
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
            attachPtxUri(uri, targetPart);
            return;
        }

        if (requestCode != REQUEST_OPEN) return;
        ArrayList<Uri> uris = selectedUris(data);
        if (uris.isEmpty()) return;
        for (Uri uri : uris) persistUriPermission(uri, data.getFlags(), false);
        if (uris.size() == 1) {
            openUri(uris.get(0));
        } else {
            openCompositeUris(uris);
        }
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
                openCompositeUris(uris);
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
            openUri(uri);
        }
    }

    private void showIdleStatus() {
        titleView.setText("DMC Native Reader");
        blackWidowState = BlackWidowState.empty();
        pendingPtxPart = -1;
        pendingExportSession = 0;
        setInfo("DMC Native Reader " + BuildConfig.VERSION_NAME + "\n"
                + "Architecture v2 core: MOD / SCM / DDS / PTX.\n"
                + "Unpromoted DMC families are intentionally excluded from main.\n\n"
                + "Open a supported resource from My Files or use ↑.\n"
                + "Select multiple canonical MOD files to compose them in one scene.");
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

    private void openCompositeUris(ArrayList<Uri> uris) {
        if (uris.size() < 2) {
            if (!uris.isEmpty()) openUri(uris.get(0));
            return;
        }

        closeAllSessions();
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
            titleView.setText("DMC Native Reader");
            blackWidowState = BlackWidowState.empty();
            setInfo(failure == null ? "MOD composition failed" : failure);
            applyResourceUiState();
            Toast.makeText(this,
                    failure == null ? "MOD composition failed" : failure,
                    Toast.LENGTH_LONG).show();
            return;
        }

        activateSession(composite, "MOD scene · " + uris.size() + " parts");
        Toast.makeText(this,
                uris.size() + " MOD parts composed in source coordinates",
                Toast.LENGTH_LONG).show();
    }

    private void attachPtxUri(Uri uri, int partIndex) {
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

        // Refresh typed application state after the native action. Diagnostic
        // text below is presentation only and never controls button state.
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
        int width;
        int height;
        int[] pixels;

        if (state.uvMapView) {
            width = UV_EXPORT_SIZE;
            height = UV_EXPORT_SIZE;
            pixels = NativeBridge.render(handle, width, height, 0.0f, 0.0f, 1.0f, 0);
        } else {
            width = NativeBridge.imagePreviewWidth(handle);
            height = NativeBridge.imagePreviewHeight(handle);
            pixels = NativeBridge.imagePreview(handle);
        }

        if (width <= 0 || height <= 0 || pixels == null
                || (long) pixels.length != (long) width * (long) height) {
            return null;
        }
        try {
            return Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888);
        } catch (RuntimeException error) {
            return null;
        }
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
