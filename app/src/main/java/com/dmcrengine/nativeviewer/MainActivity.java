package com.dmcrengine.nativeviewer;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.List;
import java.util.Set;

public final class MainActivity extends Activity {
    private static final int REQUEST_OPEN = 1001;
    private DmcRenderView renderView;
    private TextView statusView;
    private Button wireButton;
    private long session;
    private String routingSelfTest = "";
    private String lastIntentDiag = "";

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        routingSelfTest = buildRoutingSelfTest();
        handleIncomingIntent(getIntent());
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIncomingIntent(intent);
    }

    private Button makeButton(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        return b;
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff0b0b0e);

        statusView = new TextView(this);
        statusView.setTextColor(Color.WHITE);
        statusView.setTextSize(13f);
        statusView.setPadding(24, 20, 24, 14);
        root.addView(statusView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        renderView = new DmcRenderView(this);
        root.addView(renderView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER);
        bar.setPadding(8, 8, 8, 12);

        Button open = makeButton("Open SCM/MOD");
        open.setOnClickListener(v -> chooseFile());
        bar.addView(open, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1.25f));

        Button reset = makeButton("Reset");
        reset.setOnClickListener(v -> renderView.resetView());
        bar.addView(reset, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 0.75f));

        wireButton = makeButton("Wire: off");
        wireButton.setOnClickListener(v -> {
            renderView.toggleWireframe();
            wireButton.setText(renderView.isWireframe() ? "Wire: on" : "Wire: off");
        });
        bar.addView(wireButton, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 0.9f));

        root.addView(bar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        setContentView(root);
    }

    private void chooseFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/vnd.dmc.scm",
                "application/vnd.dmc.mod",
                "application/octet-stream",
                "audio/x-mod",
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
        statusView.setText("DMC Native Viewer v0.4\n"
                + routingSelfTest + "\n"
                + diag + "\n"
                + "Tap a .scm/.mod file in My Files, or use Open SCM/MOD.");
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

    private ParcelFileDescriptor openReadOnlyDescriptor(Uri uri) throws FileNotFoundException {
        if ("file".equals(uri.getScheme()) && uri.getPath() != null) {
            return ParcelFileDescriptor.open(new File(uri.getPath()), ParcelFileDescriptor.MODE_READ_ONLY);
        }
        return getContentResolver().openFileDescriptor(uri, "r");
    }

    private void openUri(Uri uri) {
        closeSession();
        String name = displayName(uri);
        try (ParcelFileDescriptor pfd = openReadOnlyDescriptor(uri)) {
            if (pfd == null) throw new FileNotFoundException("No file descriptor");
            session = NativeBridge.open(pfd.getFd(), name);
        } catch (Exception e) {
            statusView.setText(name + "\nOpen failed: " + e + "\n"
                    + routingSelfTest + "\n" + lastIntentDiag);
            Toast.makeText(this, "Could not read file", Toast.LENGTH_LONG).show();
            return;
        }
        if (session == 0) {
            statusView.setText(name + "\nRejected by native decoder\n"
                    + routingSelfTest + "\n" + lastIntentDiag);
            Toast.makeText(this, "SCM/MOD decoder rejected this file", Toast.LENGTH_LONG).show();
            return;
        }
        statusView.setText(name + "\n" + NativeBridge.info(session) + "\n"
                + routingSelfTest + "\n" + lastIntentDiag);
        renderView.setSession(session);
    }

    private String describeIntent(Intent intent) {
        if (intent == null) return "intent=null";
        Uri data = intent.getData();
        Set<String> categories = intent.getCategories();
        return "intent action=" + safe(intent.getAction())
                + " type=" + safe(intent.getType())
                + " scheme=" + (data == null ? "null" : safe(data.getScheme()))
                + " categories=" + (categories == null ? "[]" : categories.toString())
                + " flags=0x" + Integer.toHexString(intent.getFlags());
    }

    private String safe(String s) {
        return s == null ? "null" : s;
    }

    private String buildRoutingSelfTest() {
        Uri content = Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADownload%2Fprobe.mod");
        Intent octet = viewIntent(content, "application/octet-stream");
        Intent audioMod = viewIntent(content, "audio/x-mod");
        Intent anyType = viewIntent(content, "application/x-samsung-unknown");
        Intent untypedContent = viewIntent(content, null);
        Intent untypedFile = viewIntent(Uri.parse("file:///storage/emulated/0/Download/probe.mod"), null);
        return "route self-test: octet=" + mark(resolvesToSelf(octet))
                + " audio/mod=" + mark(resolvesToSelf(audioMod))
                + " provider=" + mark(resolvesToSelf(anyType))
                + " content-null=" + mark(resolvesToSelf(untypedContent))
                + " file-null=" + mark(resolvesToSelf(untypedFile));
    }

    private Intent viewIntent(Uri uri, String mime) {
        Intent i = new Intent(Intent.ACTION_VIEW);
        if (mime == null) i.setData(uri); else i.setDataAndType(uri, mime);
        i.addCategory(Intent.CATEGORY_DEFAULT);
        i.addCategory(Intent.CATEGORY_BROWSABLE);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        return i;
    }

    private boolean resolvesToSelf(Intent intent) {
        PackageManager pm = getPackageManager();
        List<ResolveInfo> results = pm.queryIntentActivities(intent, PackageManager.MATCH_DEFAULT_ONLY);
        for (ResolveInfo r : results) {
            if (r.activityInfo != null && getPackageName().equals(r.activityInfo.packageName)) {
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
