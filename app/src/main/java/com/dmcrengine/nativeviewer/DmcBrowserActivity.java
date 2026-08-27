package com.dmcrengine.nativeviewer;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Deque;
import java.util.List;
import java.util.Locale;

/**
 * Built-in DMC resource browser.
 *
 * OEM file managers (notably Samsung My Files) may refuse to route unknown
 * extensions such as {@code .scm} / {@code .mod} to any installed app, so the
 * reader cannot depend on an external file manager to reach a file.  This
 * activity finds DMC resources itself through two independent sources:
 *
 * <ul>
 *   <li>a direct filesystem scan, when the user grants all-files access
 *       (API 30+) or legacy read access (API 29 and below);</li>
 *   <li>a Storage Access Framework tree the user picks once, which needs no
 *       runtime permission at all and is remembered across launches.</li>
 * </ul>
 *
 * The selected resource is returned to the caller as the intent data URI.
 */
public final class DmcBrowserActivity extends Activity {
    /** Absolute filesystem path of the chosen resource, when not a document URI. */
    public static final String EXTRA_PATH = "com.dmcrengine.nativeviewer.extra.PATH";

    private static final int REQUEST_TREE = 2001;
    private static final int REQUEST_ALL_FILES = 2002;
    private static final int REQUEST_LEGACY_READ = 2003;

    private static final String PREFS = "dmc_browser";
    private static final String KEY_TREE_URI = "tree_uri";

    /** Guards against pathological trees; a DMC corpus is far smaller than this. */
    private static final int MAX_RESULTS = 4000;
    private static final int MAX_DEPTH = 16;

    private TextView statusView;
    private ResultAdapter adapter;
    private Thread scanThread;
    private int scanGeneration;

    /**
     * One browsable DMC resource.  Filesystem hits carry an absolute path
     * rather than a {@code file://} URI: a file URI placed in an Intent trips
     * StrictMode's {@code FileUriExposedException} on API 24+, even when the
     * receiving activity belongs to this same package.
     */
    private static final class Entry {
        final String name;
        final String location;
        final long size;
        final Uri documentUri;
        final String path;

        Entry(String name, String location, long size, Uri documentUri, String path) {
            this.name = name;
            this.location = location;
            this.size = size;
            this.documentUri = documentUri;
            this.path = path;
        }
    }

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        startScan();
    }

    @Override protected void onDestroy() {
        scanGeneration++;
        super.onDestroy();
    }

    private Button makeButton(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(13f);
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

        adapter = new ResultAdapter();
        ListView list = new ListView(this);
        list.setAdapter(adapter);
        list.setDivider(null);
        list.setBackgroundColor(0xff0b0b0e);
        list.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                deliver(adapter.getItem(position));
            }
        });
        root.addView(list, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER);
        bar.setPadding(8, 8, 8, 12);

        Button grant = makeButton("Grant file access");
        grant.setOnClickListener(v -> requestStorageAccess());
        bar.addView(grant, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        Button pick = makeButton("Pick folder");
        pick.setOnClickListener(v -> pickTree());
        bar.addView(pick, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        Button rescan = makeButton("Rescan");
        rescan.setOnClickListener(v -> startScan());
        bar.addView(rescan, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 0.8f));

        root.addView(bar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        setContentView(root);
    }

    private void deliver(Entry entry) {
        if (entry == null) return;
        Intent result = new Intent();
        if (entry.documentUri != null) {
            result.setData(entry.documentUri);
            result.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } else {
            result.putExtra(EXTRA_PATH, entry.path);
        }
        setResult(RESULT_OK, result);
        finish();
    }

    // ---------------------------------------------------------------- access

    private boolean hasDirectFileAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    private void requestStorageAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Environment.isExternalStorageManager()) {
                startScan();
                return;
            }
            // "All files access" is the only permission that exposes arbitrary
            // non-media files such as .scm / .mod to a direct filesystem scan.
            Intent settings = new Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            try {
                startActivityForResult(settings, REQUEST_ALL_FILES);
                return;
            } catch (RuntimeException ignored) {
                // Fall through to the generic settings screen below.
            }
            try {
                startActivityForResult(
                        new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION),
                        REQUEST_ALL_FILES);
            } catch (RuntimeException e) {
                Toast.makeText(this, "All-files access unavailable; use Pick folder",
                        Toast.LENGTH_LONG).show();
            }
            return;
        }
        requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE},
                REQUEST_LEGACY_READ);
    }

    private void pickTree() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(i, REQUEST_TREE);
        } catch (RuntimeException e) {
            Toast.makeText(this, "No folder picker available", Toast.LENGTH_LONG).show();
        }
    }

    @Override public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                                     int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_LEGACY_READ) startScan();
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_ALL_FILES) {
            startScan();
            return;
        }
        if (requestCode == REQUEST_TREE && resultCode == RESULT_OK
                && data != null && data.getData() != null) {
            Uri tree = data.getData();
            try {
                getContentResolver().takePersistableUriPermission(tree,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException ignored) {}
            prefs().edit().putString(KEY_TREE_URI, tree.toString()).apply();
            startScan();
        }
    }

    private SharedPreferences prefs() {
        return getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    private Uri savedTree() {
        String stored = prefs().getString(KEY_TREE_URI, null);
        if (stored == null) return null;
        Uri tree = Uri.parse(stored);
        // A persisted grant can be revoked by the user or lost on reinstall.
        for (android.content.UriPermission p : getContentResolver().getPersistedUriPermissions()) {
            if (p.isReadPermission() && tree.equals(p.getUri())) return tree;
        }
        prefs().edit().remove(KEY_TREE_URI).apply();
        return null;
    }

    // ------------------------------------------------------------------ scan

    private void startScan() {
        final int generation = ++scanGeneration;
        final boolean direct = hasDirectFileAccess();
        final Uri tree = savedTree();

        if (!direct && tree == null) {
            adapter.replace(new ArrayList<>());
            statusView.setText("No storage access yet.\n"
                    + "Grant file access to scan the whole device, "
                    + "or use Pick folder to choose the folder holding your DMC files.");
            return;
        }

        statusView.setText("Scanning for .scm / .mod / .hits / .ukn ...");
        if (scanThread != null) scanThread.interrupt();
        scanThread = new Thread(() -> {
            final List<Entry> found = new ArrayList<>();
            final String source;
            if (direct) {
                source = "filesystem scan";
                for (File root : scanRoots()) scanFiles(root, 0, found);
            } else {
                source = "picked folder";
                scanTree(tree, found);
            }
            Collections.sort(found, new Comparator<Entry>() {
                @Override public int compare(Entry a, Entry b) {
                    return a.name.compareToIgnoreCase(b.name);
                }
            });
            runOnUiThread(() -> {
                if (generation != scanGeneration) return;
                adapter.replace(found);
                if (found.isEmpty()) {
                    statusView.setText("No DMC resources found (" + source + ").\n"
                            + (direct
                            ? "Copy your DMC files to internal storage, or use Pick folder."
                            : "Pick the folder that actually contains the files."));
                } else {
                    statusView.setText(found.size() + " DMC resource(s) found (" + source + ").\n"
                            + "Tap one to decode and render it.");
                }
            });
        }, "dmc-browser-scan");
        scanThread.start();
    }

    private List<File> scanRoots() {
        List<File> roots = new ArrayList<>();
        File primary = Environment.getExternalStorageDirectory();
        if (primary != null && primary.isDirectory()) roots.add(primary);
        // Removable volumes: the parent of the app-private dirs is the volume root.
        for (File appDir : getExternalFilesDirs(null)) {
            if (appDir == null) continue;
            File volume = appDir;
            for (int i = 0; i < 4 && volume != null; i++) volume = volume.getParentFile();
            if (volume != null && volume.isDirectory() && !roots.contains(volume)) roots.add(volume);
        }
        return roots;
    }

    private void scanFiles(File dir, int depth, List<Entry> out) {
        if (depth > MAX_DEPTH || out.size() >= MAX_RESULTS || Thread.currentThread().isInterrupted()) {
            return;
        }
        // Android/data and Android/obb are unreadable to apps and only waste time.
        String path = dir.getAbsolutePath();
        if (path.endsWith("/Android/data") || path.endsWith("/Android/obb")) return;

        File[] children = dir.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (out.size() >= MAX_RESULTS || Thread.currentThread().isInterrupted()) return;
            if (child.isDirectory()) {
                scanFiles(child, depth + 1, out);
            } else if (isDmcName(child.getName())) {
                out.add(new Entry(child.getName(), child.getParent(), child.length(),
                        null, child.getAbsolutePath()));
            }
        }
    }

    private void scanTree(Uri tree, List<Entry> out) {
        String rootId;
        try {
            rootId = DocumentsContract.getTreeDocumentId(tree);
        } catch (RuntimeException e) {
            return;
        }
        Deque<String> pending = new ArrayDeque<>();
        pending.add(rootId);
        int visited = 0;

        while (!pending.isEmpty() && out.size() < MAX_RESULTS
                && !Thread.currentThread().isInterrupted()) {
            String parentId = pending.poll();
            if (++visited > MAX_RESULTS) return;
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
            try (Cursor c = getContentResolver().query(children, new String[]{
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE,
                    DocumentsContract.Document.COLUMN_SIZE
            }, null, null, null)) {
                if (c == null) continue;
                while (c.moveToNext()) {
                    String docId = c.getString(0);
                    String name = c.getString(1);
                    String mime = c.getString(2);
                    long size = c.isNull(3) ? -1 : c.getLong(3);
                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                        pending.add(docId);
                    } else if (name != null && isDmcName(name)) {
                        out.add(new Entry(name, treeLabel(tree), size,
                                DocumentsContract.buildDocumentUriUsingTree(tree, docId), null));
                    }
                }
            } catch (RuntimeException ignored) {
                // A single unreadable subtree must not abort the whole scan.
            }
        }
    }

    private String treeLabel(Uri tree) {
        String id = null;
        try {
            id = DocumentsContract.getTreeDocumentId(tree);
        } catch (RuntimeException ignored) {}
        return id == null ? String.valueOf(tree) : id;
    }

    /**
     * `.ukn` is listed because a HITS collision resource is routinely shipped
     * under that name; the native probe matches on the four-byte magic, so a
     * `.ukn` holding anything else is still rejected on open.
     */
    private static boolean isDmcName(String name) {
        String lower = name.toLowerCase(Locale.US);
        return lower.endsWith(".scm") || lower.endsWith(".mod")
                || lower.endsWith(".hits") || lower.endsWith(".ukn");
    }

    private static String humanSize(long bytes) {
        if (bytes < 0) return "size unknown";
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024 * 1024) return (bytes / 1024) + " KB";
        return String.format(Locale.US, "%.1f MB", bytes / (1024.0 * 1024.0));
    }

    // --------------------------------------------------------------- adapter

    private final class ResultAdapter extends BaseAdapter {
        private List<Entry> items = new ArrayList<>();

        void replace(List<Entry> next) {
            items = next;
            notifyDataSetChanged();
        }

        @Override public int getCount() {
            return items.size();
        }

        @Override public Entry getItem(int position) {
            return position >= 0 && position < items.size() ? items.get(position) : null;
        }

        @Override public long getItemId(int position) {
            return position;
        }

        @Override public View getView(int position, View convertView, ViewGroup parent) {
            TextView view = convertView instanceof TextView
                    ? (TextView) convertView
                    : new TextView(DmcBrowserActivity.this);
            view.setPadding(24, 18, 24, 18);
            view.setTextColor(Color.WHITE);
            view.setTextSize(14f);
            Entry entry = items.get(position);
            view.setText(entry.name + "\n" + humanSize(entry.size) + "  ·  " + entry.location);
            return view;
        }
    }
}
