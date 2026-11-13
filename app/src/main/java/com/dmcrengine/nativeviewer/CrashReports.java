package com.dmcrengine.nativeviewer;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;

/**
 * What is left when the reader closes by itself, and what the next start does
 * with it. Three things are kept in the app's own storage: the stack of an
 * uncaught Java exception, the note of a fatal native signal
 * (crash_recorder.h), and the name of the file that was being opened. The
 * next start joins them into one report the user can share, and remembers
 * the file so it opens as its description instead of closing the reader again.
 */
final class CrashReports {
    private static final String PREFS = "crash";
    private static final String KEY_OPENING = "opening";
    private static final String KEY_CLOSED_ON = "closedOn";
    private static final String JAVA_FILE = "crash_java.txt";
    private static final String NATIVE_FILE = "crash_native.txt";

    private static boolean installed;

    private CrashReports() {}

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** Once per process: record uncaught Java exceptions and fatal native signals. */
    static void install(Context context) {
        if (installed) return;
        installed = true;
        final File javaFile = new File(context.getFilesDir(), JAVA_FILE);
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try (FileOutputStream out = new FileOutputStream(javaFile)) {
                final StringWriter text = new StringWriter();
                text.append("Java exception on thread ").append(thread.getName()).append("\n");
                error.printStackTrace(new PrintWriter(text, true));
                out.write(text.toString().getBytes(StandardCharsets.UTF_8));
            } catch (Throwable ignored) {
                // Nothing more can be done here; the system handler still runs.
            }
            if (previous != null) previous.uncaughtException(thread, error);
        });
        try {
            NativeBridge.installCrashRecorder(new File(context.getFilesDir(), NATIVE_FILE).getAbsolutePath());
        } catch (Throwable ignored) {
            // Without the native library there is nothing native to record.
        }
    }

    /** A file starts to open: remembered at once, so a crash right after is its. */
    static void opening(Context context, String name) {
        prefs(context).edit().putString(KEY_OPENING, name).commit();
        try {
            NativeBridge.setCrashContext("opening " + name);
        } catch (Throwable ignored) {}
    }

    /** `name` is on the screen (or its open ended): no longer "being opened". A later file's mark stays. */
    static void shown(Context context, String name) {
        if (name == null || !name.equals(prefs(context).getString(KEY_OPENING, null))) return;
        prefs(context).edit().remove(KEY_OPENING).apply();
        try {
            NativeBridge.setCrashContext("viewing " + name);
        } catch (Throwable ignored) {}
    }

    /** The user left while a file was opening: that is not the reader closing by itself. */
    static void left(Context context) {
        if (prefs(context).contains(KEY_OPENING)) prefs(context).edit().remove(KEY_OPENING).commit();
    }

    /** True while `name` is the file the reader closed on; it opens as a description. */
    static boolean closedOn(Context context, String name) {
        return name != null && name.equals(prefs(context).getString(KEY_CLOSED_ON, null));
    }

    /** The user asked to draw the file again: it opens normally from now on. */
    static void forgive(Context context) {
        prefs(context).edit().remove(KEY_CLOSED_ON).commit();
    }

    /**
     * The report of the last run that closed by itself, or null when it did
     * not. Read once: the notes are removed, the file's name is kept for
     * {@link #closedOn}.
     */
    static String collect(Context context, String version, String build, List<String> recentDiagnostics) {
        final File javaFile = new File(context.getFilesDir(), JAVA_FILE);
        final File nativeFile = new File(context.getFilesDir(), NATIVE_FILE);
        final String opening = prefs(context).getString(KEY_OPENING, null);
        final String javaNote = read(javaFile);
        final String nativeNote = read(nativeFile);
        if (opening == null && javaNote == null && nativeNote == null) return null;

        final StringBuilder report = new StringBuilder();
        report.append("DMC Native Reader: the reader closed by itself\n")
                .append("Version: ").append(version).append(", build ").append(build).append("\n")
                .append("Device: ").append(Build.MANUFACTURER).append(" ").append(Build.MODEL)
                .append(", Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT)
                .append("), ").append(Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "?").append("\n");
        report.append(opening != null ? "While opening: " + opening + "\n" : "Not while opening a file.\n");
        if (javaNote != null) report.append("\n").append(javaNote.trim()).append("\n");
        if (nativeNote != null) report.append("\n").append(nativeNote.trim()).append("\n");
        if (javaNote == null && nativeNote == null) {
            report.append("\nNo stack was recorded: the system ended the process (out of memory, or stopped from outside).\n");
        }
        if (recentDiagnostics != null && !recentDiagnostics.isEmpty()) {
            report.append("\nLast opens:\n");
            for (String line : recentDiagnostics) report.append(line).append("\n");
        }

        //noinspection ResultOfMethodCallIgnored
        javaFile.delete();
        //noinspection ResultOfMethodCallIgnored
        nativeFile.delete();
        final SharedPreferences.Editor edit = prefs(context).edit().remove(KEY_OPENING);
        if (opening != null) edit.putString(KEY_CLOSED_ON, opening);
        edit.commit();
        return report.toString();
    }

    private static String read(File file) {
        if (!file.isFile()) return null;
        try {
            final byte[] bytes = Files.readAllBytes(file.toPath());
            // A note is a few kilobytes; anything larger is not one.
            if (bytes.length == 0 || bytes.length > 256 * 1024) return null;
            return new String(bytes, StandardCharsets.UTF_8);
        } catch (Throwable ignored) {
            return null;
        }
    }
}
