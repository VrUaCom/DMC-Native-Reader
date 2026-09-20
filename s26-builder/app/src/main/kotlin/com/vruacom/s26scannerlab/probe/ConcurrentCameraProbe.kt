package com.vruacom.s26scannerlab.probe

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.ImageReader
import android.util.Size
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.math.abs

class ConcurrentCameraProbe(private val context: Context) {
    private val manager = context.getSystemService(CameraManager::class.java)

    fun run(): JSONObject {
        val advertised = manager.concurrentCameraIds
            .map { it.toSortedSet() }
            .sortedBy { it.joinToString(",") }

        val testSets = linkedSetOf<Set<String>>()
        advertised.forEach { set ->
            testSets.add(set)
            val ids = set.toList()
            for (i in ids.indices) {
                for (j in i + 1 until ids.size) {
                    testSets.add(sortedSetOf(ids[i], ids[j]))
                }
            }
        }

        val tests = JSONArray()
        val byKey = LinkedHashMap<String, JSONObject>()
        testSets.sortedBy { it.joinToString(",") }.forEach { ids ->
            val result = testSet(ids)
            val key = ids.toList().sorted().joinToString("+")
            byKey[key] = result
            tests.put(result)
        }

        val ids = manager.cameraIdList.toList().sorted()
        val pairMatrix = JSONArray()
        for (i in ids.indices) {
            for (j in i + 1 until ids.size) {
                val pair = sortedSetOf(ids[i], ids[j])
                val key = pair.joinToString("+")
                val advertisedTogether = advertised.any { set -> set.containsAll(pair) }
                pairMatrix.put(JSONObject()
                    .put("camera_a", ids[i])
                    .put("camera_b", ids[j])
                    .put("advertised_together", advertisedTogether)
                    .put("runtime_test",
                        byKey[key] ?: JSONObject()
                            .put("status", "NOT_ADVERTISED")
                            .put("camera_ids", JsonUtil.value(pair))))
            }
        }

        return JSONObject()
            .put("generated_at_utc", Instant.now().toString())
            .put("advertised_sets", JsonUtil.value(advertised))
            .put("runtime_tests", tests)
            .put("pair_matrix", pairMatrix)
    }

    private fun testSet(ids: Set<String>): JSONObject {
        val sortedIds = ids.toList().sorted()
        if (context.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            return JSONObject()
                .put("camera_ids", JsonUtil.value(sortedIds))
                .put("status", "PERMISSION_DENIED")
        }

        val executor = CameraCallbackExecutor.executor
        val readers = ConcurrentHashMap<String, ImageReader>()
        val cameras = ConcurrentHashMap<String, CameraDevice>()
        val sessions = ConcurrentHashMap<String, CameraCaptureSession>()
        val errors = ConcurrentHashMap<String, String>()
        val sizes = ConcurrentHashMap<String, Size>()

        try {
            sortedIds.forEach { id ->
                val size = selectYuvSize(id)
                sizes[id] = size
                readers[id] = ImageReader.newInstance(
                    size.width,
                    size.height,
                    ImageFormat.YUV_420_888,
                    2
                )
            }

            val openLatch = CountDownLatch(sortedIds.size)
            sortedIds.forEach { id ->
                try {
                    manager.openCamera(id, executor, object : CameraDevice.StateCallback() {
                        override fun onOpened(camera: CameraDevice) {
                            cameras[id] = camera
                            openLatch.countDown()
                        }

                        override fun onDisconnected(camera: CameraDevice) {
                            errors[id] = "DISCONNECTED"
                            camera.close()
                            openLatch.countDown()
                        }

                        override fun onError(camera: CameraDevice, error: Int) {
                            errors[id] = "OPEN_ERROR_$error"
                            camera.close()
                            openLatch.countDown()
                        }
                    })
                } catch (t: Throwable) {
                    errors[id] = "OPEN_EXCEPTION_${t.javaClass.simpleName}: ${t.message}"
                    openLatch.countDown()
                }
            }

            val opensCompleted = openLatch.await(10, TimeUnit.SECONDS)
            if (!opensCompleted || cameras.size != sortedIds.size) {
                return report(
                    sortedIds,
                    if (!opensCompleted) "OPEN_TIMEOUT" else "OPEN_FAILED",
                    sizes,
                    errors,
                    cameras.keys,
                    sessions.keys
                )
            }

            val sessionLatch = CountDownLatch(sortedIds.size)
            sortedIds.forEach { id ->
                val camera = cameras[id] ?: run {
                    errors[id] = "CAMERA_MISSING_AFTER_OPEN"
                    sessionLatch.countDown()
                    return@forEach
                }
                val reader = readers[id]!!
                try {
                    val config = SessionConfiguration(
                        SessionConfiguration.SESSION_REGULAR,
                        listOf(OutputConfiguration(reader.surface)),
                        executor,
                        object : CameraCaptureSession.StateCallback() {
                            override fun onConfigured(session: CameraCaptureSession) {
                                sessions[id] = session
                                sessionLatch.countDown()
                            }

                            override fun onConfigureFailed(session: CameraCaptureSession) {
                                errors[id] = "SESSION_CONFIGURE_FAILED"
                                sessionLatch.countDown()
                            }
                        }
                    )
                    camera.createCaptureSession(config)
                } catch (t: Throwable) {
                    errors[id] = "SESSION_EXCEPTION_${t.javaClass.simpleName}: ${t.message}"
                    sessionLatch.countDown()
                }
            }

            val sessionsCompleted = sessionLatch.await(10, TimeUnit.SECONDS)
            val status = when {
                !sessionsCompleted -> "SESSION_TIMEOUT"
                sessions.size == sortedIds.size -> "PASS"
                else -> "SESSION_FAILED"
            }
            return report(
                sortedIds,
                status,
                sizes,
                errors,
                cameras.keys,
                sessions.keys
            )
        } finally {
            sessions.values.forEach { try { it.close() } catch (_: Throwable) {} }
            cameras.values.forEach { try { it.close() } catch (_: Throwable) {} }
            readers.values.forEach { try { it.close() } catch (_: Throwable) {} }
        }
    }

    private fun report(
        ids: List<String>,
        status: String,
        sizes: Map<String, Size>,
        errors: Map<String, String>,
        opened: Set<String>,
        configured: Set<String>
    ): JSONObject {
        val sizesJson = JSONObject()
        sizes.toSortedMap().forEach { (id, size) -> sizesJson.put(id, JsonUtil.value(size)) }
        val errorsJson = JSONObject()
        errors.toSortedMap().forEach { (id, error) -> errorsJson.put(id, error) }

        return JSONObject()
            .put("camera_ids", JsonUtil.value(ids))
            .put("status", status)
            .put("requested_yuv_sizes", sizesJson)
            .put("opened_camera_ids", JsonUtil.value(opened.toList().sorted()))
            .put("configured_session_ids", JsonUtil.value(configured.toList().sorted()))
            .put("errors", errorsJson)
    }

    private fun selectYuvSize(cameraId: String): Size {
        val c = manager.getCameraCharacteristics(cameraId)
        val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val sizes = map?.getOutputSizes(ImageFormat.YUV_420_888)?.toList().orEmpty()
        if (sizes.isEmpty()) return Size(640, 480)
        val target = 640L * 480L
        return sizes.minByOrNull { abs(it.width.toLong() * it.height.toLong() - target) }
            ?: sizes.first()
    }
}
