package com.vruacom.s26scannerlab

import android.content.Context
import android.os.Build
import android.os.Process
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.time.Instant

object CrashReporter {
    private const val CRASH_DIR = "crash"
    private const val LAST_CRASH = "last_crash.txt"

    fun install(context: Context) {
        val appContext = context.applicationContext
        val previous = Thread.getDefaultUncaughtExceptionHandler()

        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                writeCrash(appContext, thread, throwable)
            } catch (_: Throwable) {
                // Never let crash reporting mask the original failure.
            } finally {
                if (previous != null) {
                    previous.uncaughtException(thread, throwable)
                } else {
                    Process.killProcess(Process.myPid())
                }
            }
        }
    }

    fun consumePreviousCrash(context: Context): String? {
        val file = File(File(context.filesDir, CRASH_DIR), LAST_CRASH)
        if (!file.exists()) return null

        return try {
            val text = file.readText(Charsets.UTF_8)
            file.delete()
            text
        } catch (_: Throwable) {
            null
        }
    }

    private fun writeCrash(context: Context, thread: Thread, throwable: Throwable) {
        val dir = File(context.filesDir, CRASH_DIR).apply { mkdirs() }
        val file = File(dir, LAST_CRASH)

        val runtime = Runtime.getRuntime()
        val stack = StringWriter()
        throwable.printStackTrace(PrintWriter(stack))

        val text = buildString(16 * 1024) {
            appendLine("S26 Scanner Lab crash report")
            appendLine("utc=${Instant.now()}")
            appendLine("thread=${thread.name}")
            appendLine("manufacturer=${Build.MANUFACTURER}")
            appendLine("model=${Build.MODEL}")
            appendLine("device=${Build.DEVICE}")
            appendLine("android=${Build.VERSION.RELEASE}")
            appendLine("sdk=${Build.VERSION.SDK_INT}")
            appendLine("free_memory=${runtime.freeMemory()}")
            appendLine("total_memory=${runtime.totalMemory()}")
            appendLine("max_memory=${runtime.maxMemory()}")
            appendLine("exception=${throwable.javaClass.name}")
            appendLine("message=${throwable.message ?: ""}")
            appendLine()
            append(stack.toString().take(256 * 1024))
        }

        file.writeText(text, Charsets.UTF_8)
    }
}
