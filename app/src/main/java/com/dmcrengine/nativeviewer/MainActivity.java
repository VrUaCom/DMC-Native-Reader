package com.dmcrengine.nativeviewer;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Insets;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.WindowInsets;
import android.webkit.MimeTypeMap;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.List;
import java.util.Set;

public final class MainActivity extends Activity {
    private static final int REQUEST_OPEN = 1001;
    private DmcRenderView renderView;
    private TextView titleView;
    private Button wireButton;
    private long session;
    private String infoText = "";
    private String routingSelfTest = "";
    private String systemMimeDiag = "";
    private String lastIntentDiag = "";
    private String lastProviderDiag = "";

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        routingSelfTest = buildRoutingSelfTest();
        systemMimeDiag = buildSystemMimeDiag();
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

    private Button makeButton(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        return b;
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

        titleView = new TextView(this);
        titleView.setTextColor(Color.WHITE);
        titleView.setTextSize(15f);
        titleView.setSingleLine(true);
        titleView.setEllipsize(TextUtils.TruncateAt.END);
        titleView.setPadding(dp(16), dp(8), dp(16), dp(6));
        titleView.setText("DMC Native Reader");
        root.addView(titleView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        renderView = new DmcRenderView(this);
        root.addView(renderView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(8), dp(6), dp(8), dp(8));

        Button open = makeButton("Open DMC resource");
        open.setOnClickListener(v -> chooseFile());
        bar.addView(open, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1.25f));

        Button reset = makeButton("Reset");
        reset.setOnClickListener(v -> renderView.resetView());
        bar.addView(reset, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 0.70f));

        wireButton = makeButton("Wire: off");
        wireButton.setOnClickListener(v -> {
            renderView.toggleWireframe();
            wireButton.setText(renderView.isWireframe() ? "Wire: on" : "Wire: off");
        });
        bar.addView(wireButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 0.85f));

        Button info = makeButton("\u2139");
        info.setTextSize(22f);
        info.setMinWidth(0);
        info.setMinimumWidth(0);
        info.setPadding(0, 0, 0, 0);
        info.setContentDescription("Resource information");
        info.setOnClickListener(v -> showInfoDialog());
        LinearLayout.LayoutParams infoParams = new LinearLayout.LayoutParams(dp(48), dp(48));
        infoParams.setMarginStart(dp(4));
        bar.addView(info, infoParams);

        root.addView(bar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        setContentView(root);
    }

    private void setInfo(String text) {
        infoText = text == null ? "" : text;
    }

    private void showInfoDialog() {
        TextView details = new TextView(this);
        details.setText(infoText.isEmpty() ? "No resource information yet." : infoText);
        details.setTextColor(Color.WHITE);
        details.setTextSize(13f);
        details.setTextIsSelectable(true);
        details.setPadding(dp(16), dp(12), dp(16), dp(20));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(0xff141418);
        scroll.addView(details, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        new AlertDialog.Builder(this)
                .setTitle("Resource information")
                .setView(scroll)
                .setPositiveButton("Close", null)
                .show();
    }

    private void chooseFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/vnd.dmc.scm",
                "application/vnd.dmc.mod",
                "application/vnd.dmc.pac",
                "application/vnd.dmc.pnst",
                "application/vnd.dmc.hits",
                "application/vnd.dmc.dca",
                "application/vnd.dmc.efm",
                "application/vnd.dmc.mrp",
                "application/vnd.dmc.shw",
                "image/vnd-ms.dds",
                "application/octet-stream",
                "audio/mod",
                "audio/x-mod",
                "audio/ogg",
                "video/mp4",
                "video/x-ms-wmv",
                "*/*"
        });
        startActivityForResult(i, REQUEST_OPEN);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_OPEN && resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri uri = data.getData();
            final int flags = data.getFlags() &
                    (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            try {
                getContentResolver().takePersistableUriPermission(uri,
                        flags & Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException ignored) {}
            lastIntentDiag = describeIntent(data);
            openUri(uri);
        }
    }

    private void handleIncomingIntent(Intent intent) {
        if (intent == null) {
            showIdleStatus("intent=null");
            return;
        }
        lastIntentDiag = describeIntent(intent);
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

        if (uri != null) {
            openUri(uri);
        } else {
            showIdleStatus(lastIntentDiag);
        }
    }

    private void showIdleStatus(String diag) {
        titleView.setText("DMC Native Reader");
        setInfo("DMC Native Reader " + BuildConfig.VERSION_NAME + "\n"
                + "71 explicit DMC family modules. Promoted readers decode/inspect; recognition-only modules stay evidence-gated.\n"
                + routingSelfTest + "\n"
                + systemMimeDiag + "\n"
                + diag + "\n"
                + "Tap a DMC resource in My Files, or use Open DMC resource.");
    }

    private String displayName(Uri uri) {
        if ("content".equals(uri.getScheme())) {
            try (Cursor c = getContentResolver().query(uri,
                    new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (c != null && c.moveToFirst()) {
                    int idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (idx >= 0) return c.getString(idx);
                }
            } catch (RuntimeException ignored) {}
        }
        String p = uri.getPath();
        return p == null ? "resource.bin" : new File(p).getName();
    }

    private String describeProvider(Uri uri) {
        String type = null;
        try {
            type = getContentResolver().getType(uri);
        } catch (RuntimeException ignored) {}
        return "provider authority=" + safe(uri.getAuthority())
                + " type=" + safe(type)
                + " path=" + safe(uri.getPath());
    }

    private ParcelFileDescriptor openReadOnlyDescriptor(Uri uri) throws FileNotFoundException {
        if ("file".equals(uri.getScheme()) && uri.getPath() != null) {
            return ParcelFileDescriptor.open(new File(uri.getPath()), ParcelFileDescriptor.MODE_READ_ONLY);
        }
        return getContentResolver().openFileDescriptor(uri, "r");
    }

    private void openUri(Uri uri) {
        closeSession();
        String name = displayName(uri);
        titleView.setText(name);
        lastProviderDiag = describeProvider(uri);
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
            if (pfd == null) throw new FileNotFoundException("No file descriptor");
            session = NativeBridge.open(pfd.getFd(), name);
        } catch (Exception e) {
            setInfo(name + "\nOpen failed: " + e + "\n"
                    + routingSelfTest + "\n" + systemMimeDiag + "\n"
                    + lastProviderDiag + "\n" + lastIntentDiag);
            Toast.makeText(this, "Could not read file", Toast.LENGTH_LONG).show();
            return;
        }
        if (session == 0) {
            setInfo(name + "\nRejected by native reader\n"
                    + routingSelfTest + "\n" + systemMimeDiag + "\n"
                    + lastProviderDiag + "\n" + lastIntentDiag);
            Toast.makeText(this, "Native DMC reader rejected this file", Toast.LENGTH_LONG).show();
            return;
        }
        setInfo(name + "\n" + NativeBridge.info(session) + "\n"
                + routingSelfTest + "\n" + systemMimeDiag + "\n"
                + lastProviderDiag + "\n" + lastIntentDiag);
        renderView.setSession(session);
    }

    private String describeIntent(Intent intent) {
        if (intent == null) return "intent=null";
        Uri data = intent.getData();
        Set<String> categories = intent.getCategories();
        return "intent action=" + safe(intent.getAction())
                + " type=" + safe(intent.getType())
                + " scheme=" + (data == null ? "null" : safe(data.getScheme()))
                + " authority=" + (data == null ? "null" : safe(data.getAuthority()))
                + " categories=" + (categories == null ? "[]" : categories.toString())
                + " flags=0x" + Integer.toHexString(intent.getFlags());
    }

    private String safe(String s) {
        return s == null ? "null" : s;
    }

    private String buildSystemMimeDiag() {
        MimeTypeMap map = MimeTypeMap.getSingleton();
        return "system MIME samples: mod=" + safe(map.getMimeTypeFromExtension("mod"))
                + " scm=" + safe(map.getMimeTypeFromExtension("scm"))
                + " dds=" + safe(map.getMimeTypeFromExtension("dds"));
    }

    private String buildRoutingSelfTest() {
        Uri providerNumeric = Uri.parse("content://media/external/file/1000000849");
        Uri providerModPath = Uri.parse("content://com.sec.android.app.myfiles.FileProvider/storage/emulated/0/Download/probe.mod");
        Uri providerScmPath = Uri.parse("content://com.sec.android.app.myfiles.FileProvider/storage/emulated/0/Download/probe.scm");
        Uri fileModPath = Uri.parse("file://localhost/storage/emulated/0/Download/probe.mod");
        Uri fileScmPath = Uri.parse("file://localhost/storage/emulated/0/Download/probe.scm");

        Intent octet = viewIntent(providerNumeric, "application/octet-stream");
        Intent audioMod = viewIntent(providerNumeric, "audio/x-mod");
        Intent anyType = viewIntent(providerNumeric, "application/x-samsung-unknown");
        Intent untypedContent = viewIntent(providerNumeric, null);
        Intent pathContentMod = viewIntent(providerModPath, null);
        Intent pathContentScm = viewIntent(providerScmPath, null);
        Intent pathFileMod = viewIntent(fileModPath, null);
        Intent pathFileScm = viewIntent(fileScmPath, null);

        return "route real-handler: octet=" + mark(resolvesRealHandler(octet))
                + " audio/mod=" + mark(resolvesRealHandler(audioMod))
                + " unknown=" + mark(resolvesRealHandler(anyType))
                + " content-null=" + mark(resolvesRealHandler(untypedContent))
                + " path-mod=" + mark(resolvesRealHandler(pathContentMod)) + "/" + mark(resolvesRealHandler(pathFileMod))
                + " path-scm=" + mark(resolvesRealHandler(pathContentScm)) + "/" + mark(resolvesRealHandler(pathFileScm));
    }

    private Intent viewIntent(Uri uri, String mime) {
        Intent i = new Intent(Intent.ACTION_VIEW);
        if (mime == null) i.setData(uri); else i.setDataAndType(uri, mime);
        i.addCategory(Intent.CATEGORY_DEFAULT);
        i.addCategory(Intent.CATEGORY_BROWSABLE);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        return i;
    }

    private boolean resolvesRealHandler(Intent intent) {
        PackageManager pm = getPackageManager();
        List<ResolveInfo> results = pm.queryIntentActivities(intent, PackageManager.MATCH_DEFAULT_ONLY);
        String wanted = DmcOpenActivity.class.getName();
        for (ResolveInfo r : results) {
            if (r.activityInfo != null
                    && getPackageName().equals(r.activityInfo.packageName)
                    && wanted.equals(r.activityInfo.name)) {
                return true;
            }
        }
        return false;
    }

    private String mark(boolean ok) {
        return ok ? "OK" : "FAIL";
    }

    private void closeSession() {
        renderView.setSession(0);
        if (session != 0) {
            NativeBridge.close(session);
            session = 0;
        }
    }

    @Override protected void onDestroy() {
        closeSession();
        super.onDestroy();
    }
}
