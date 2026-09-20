package com.vruacom.s26scannerlab.probe

import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.ThreadFactory
import java.util.concurrent.atomic.AtomicInteger

/**
 * Process-wide Camera2 callback executor.
 *
 * CameraDevice/CameraCaptureSession may enqueue terminal callbacks (especially
 * CameraDevice.StateCallback.onClosed) after close() returns. Shutting down a
 * per-probe executor immediately after close() races those callbacks and can
 * crash the process with RejectedExecutionException.
 *
 * Keep one callback executor alive for the process lifetime instead. Cached
 * threads time out automatically when idle, so this does not permanently pin
 * a worker thread.
 */
object CameraCallbackExecutor {
    private val threadNumber = AtomicInteger(1)

    val executor: ExecutorService = Executors.newCachedThreadPool(
        ThreadFactory { runnable ->
            Thread(
                runnable,
                "S26CameraCallback-${threadNumber.getAndIncrement()}"
            ).apply {
                isDaemon = true
            }
        }
    )
}
