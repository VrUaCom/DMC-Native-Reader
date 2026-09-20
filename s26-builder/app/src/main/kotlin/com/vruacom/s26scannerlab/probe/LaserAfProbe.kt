package com.vruacom.s26scannerlab.probe

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.TotalCaptureResult
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.ImageReader
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Size
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs

data class LaserAfTraceArtifact(
    val summary: JSONObject,
    val ndjson: String
)

class LaserAfProbe(private val context: Context) {
    private val manager = context.getSystemService(CameraManager::class.java)
    val targetDistanceMmPresets = listOf(200, 300, 500, 1000, 2000)

    fun captureTrace(targetDistanceMm: Int?, frameTarget: Int = 40): LaserAfTraceArtifact {
        if (context.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            return LaserAfTraceArtifact(
                JSONObject().put("status", "PERMISSION_DENIED"),
                ""
            )
        }

        val cameraId = chooseRearAfCamera()
            ?: return LaserAfTraceArtifact(
                JSONObject().put("status", "NO_REAR_AF_CAMERA"),
                ""
            )

        val characteristics = manager.getCameraCharacteristics(cameraId)
        val size = selectYuvSize(characteristics)
        val reader = ImageReader.newInstance(size.width, size.height, ImageFormat.YUV_420_888, 3)
        reader.setOnImageAvailableListener({ source ->
            try { source.acquireLatestImage()?.close() } catch (_: Throwable) {}
        }, Handler(Looper.getMainLooper()))

        val executor = CameraCallbackExecutor.executor
        val done = CountDownLatch(1)
        val cameraRef = AtomicReference<CameraDevice?>(null)
        val sessionRef = AtomicReference<CameraCaptureSession?>(null)
        val errorRef = AtomicReference<String?>(null)
        val lines = Collections.synchronizedList(mutableListOf<String>())
        val candidateKeyNames = Collections.synchronizedSet(sortedSetOf<String>())
        val seenFrames = Collections.synchronizedSet(mutableSetOf<Long>())

        val callback = object : CameraCaptureSession.CaptureCallback() {
            override fun onCaptureCompleted(
                session: CameraCaptureSession,
                request: CaptureRequest,
                result: TotalCaptureResult
            ) {
                if (!seenFrames.add(result.frameNumber)) return

                val row = JSONObject()
                    .put("frame_number", result.frameNumber)
                    .put("sequence_id", result.sequenceId)
                    .put("camera_id", result.cameraId)
                    .put("sensor_timestamp_ns", JsonUtil.value(result.get(CaptureResult.SENSOR_TIMESTAMP)))
                    .put("receive_elapsed_realtime_ns", SystemClock.elapsedRealtimeNanos())
                    .put("af_mode", JsonUtil.value(result.get(CaptureResult.CONTROL_AF_MODE)))
                    .put("af_state", JsonUtil.value(result.get(CaptureResult.CONTROL_AF_STATE)))
                    .put("lens_focus_distance_diopters", JsonUtil.value(result.get(CaptureResult.LENS_FOCUS_DISTANCE)))
                    .put("lens_state", JsonUtil.value(result.get(CaptureResult.LENS_STATE)))

                val candidateValues = JSONObject()
                result.keys
                    .filter { isAfCandidateKey(it.name) }
                    .sortedBy { it.name }
                    .forEach { key ->
                        candidateKeyNames.add(key.name)
                        candidateValues.put(key.name, JsonUtil.value(readResult(result, key)))
                    }
                row.put("candidate_vendor_or_af_values", candidateValues)

                val physical = JSONObject()
                result.physicalCameraTotalResults.toSortedMap().forEach { (id, physicalResult) ->
                    val values = JSONObject()
                    physicalResult.keys
                        .filter { isAfCandidateKey(it.name) }
                        .sortedBy { it.name }
                        .forEach { key ->
                            candidateKeyNames.add(key.name)
                            values.put(key.name, JsonUtil.value(readResult(physicalResult, key)))
                        }
                    physical.put(id, JSONObject()
                        .put("focus_distance_diopters",
                            JsonUtil.value(physicalResult.get(CaptureResult.LENS_FOCUS_DISTANCE)))
                        .put("af_state", JsonUtil.value(physicalResult.get(CaptureResult.CONTROL_AF_STATE)))
                        .put("candidate_values", values))
                }
                row.put("physical_camera_results", physical)
                lines.add(row.toString())

                if (seenFrames.size >= frameTarget) done.countDown()
            }
        }

        try {
            manager.openCamera(cameraId, executor, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    cameraRef.set(camera)
                    val output = OutputConfiguration(reader.surface)
                    val config = SessionConfiguration(
                        SessionConfiguration.SESSION_REGULAR,
                        listOf(output),
                        executor,
                        object : CameraCaptureSession.StateCallback() {
                            override fun onConfigured(session: CameraCaptureSession) {
                                sessionRef.set(session)
                                try {
                                    val repeating = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)
                                        .apply {
                                            addTarget(reader.surface)
                                            set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_AUTO)
                                        }
                                    session.setSingleRepeatingRequest(repeating.build(), executor, callback)

                                    val trigger = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)
                                        .apply {
                                            addTarget(reader.surface)
                                            set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_AUTO)
                                            set(CaptureRequest.CONTROL_AF_TRIGGER, CaptureRequest.CONTROL_AF_TRIGGER_START)
                                        }
                                    session.captureSingleRequest(trigger.build(), executor, callback)
                                } catch (t: Throwable) {
                                    errorRef.set("AF_START_ERROR_${t.javaClass.simpleName}: ${t.message}")
                                    done.countDown()
                                }
                            }

                            override fun onConfigureFailed(session: CameraCaptureSession) {
                                errorRef.set("SESSION_CONFIGURE_FAILED")
                                done.countDown()
                            }
                        }
                    )
                    camera.createCaptureSession(config)
                }

                override fun onDisconnected(camera: CameraDevice) {
                    errorRef.set("CAMERA_DISCONNECTED")
                    done.countDown()
                    camera.close()
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    errorRef.set("CAMERA_ERROR_$error")
                    done.countDown()
                    camera.close()
                }
            })

            val completed = done.await(10, TimeUnit.SECONDS)
            val status = when {
                errorRef.get() != null -> "FAILED"
                seenFrames.size >= frameTarget -> "PASS"
                completed -> "PARTIAL"
                seenFrames.isNotEmpty() -> "PARTIAL_TIMEOUT"
                else -> "TIMEOUT"
            }

            val characteristicCandidates = characteristics.keys
                .map { it.name }
                .filter { isAfCandidateKey(it) }
                .sorted()

            val summary = JSONObject()
                .put("generated_at_utc", Instant.now().toString())
                .put("status", status)
                .put("camera_id", cameraId)
                .put("target_distance_mm_user_label", targetDistanceMm ?: JSONObject.NULL)
                .put("target_distance_is_measured_by_phone", false)
                .put("requested_frames", frameTarget)
                .put("captured_frames", seenFrames.size)
                .put("yuv_size", JsonUtil.value(size))
                .put("minimum_focus_distance_diopters",
                    JsonUtil.value(characteristics.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE)))
                .put("af_modes",
                    JsonUtil.value(characteristics.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)))
                .put("candidate_characteristic_keys", JsonUtil.value(characteristicCandidates))
                .put("candidate_result_keys_seen", JsonUtil.value(candidateKeyNames.toList().sorted()))
                .put("error", errorRef.get() ?: JSONObject.NULL)
                .put("interpretation_note",
                    "Laser AF is not assumed to expose a direct range. Candidate vendor/AF metadata is collected so device traces can prove what Samsung exposes.")

            return LaserAfTraceArtifact(summary, lines.joinToString(separator = "\n", postfix = if (lines.isEmpty()) "" else "\n"))
        } catch (t: Throwable) {
            return LaserAfTraceArtifact(
                JSONObject()
                    .put("status", "EXCEPTION")
                    .put("camera_id", cameraId)
                    .put("exception", t.javaClass.name)
                    .put("message", t.message ?: JSONObject.NULL),
                lines.joinToString(separator = "\n")
            )
        } finally {
            try { sessionRef.get()?.stopRepeating() } catch (_: Throwable) {}
            try { sessionRef.get()?.close() } catch (_: Throwable) {}
            try { cameraRef.get()?.close() } catch (_: Throwable) {}
            try { reader.close() } catch (_: Throwable) {}
        }
    }

    private fun chooseRearAfCamera(): String? =
        manager.cameraIdList.firstOrNull { id ->
            val c = manager.getCameraCharacteristics(id)
            val facing = c.get(CameraCharacteristics.LENS_FACING)
            val modes = c.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES) ?: intArrayOf()
            facing == CameraCharacteristics.LENS_FACING_BACK &&
                modes.any { it != CaptureRequest.CONTROL_AF_MODE_OFF }
        }

    private fun selectYuvSize(c: CameraCharacteristics): Size {
        val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val sizes = map?.getOutputSizes(ImageFormat.YUV_420_888)?.toList().orEmpty()
        if (sizes.isEmpty()) return Size(640, 480)
        val target = 640L * 480L
        return sizes.minByOrNull { abs(it.width.toLong() * it.height.toLong() - target) }
            ?: sizes.first()
    }

    private fun isAfCandidateKey(name: String): Boolean {
        val n = name.lowercase()
        return listOf(
            "laser", "tof", "depth", "distance", "range", "focus", "af", "pdaf", "phase"
        ).any { n.contains(it) }
    }

    @Suppress("UNCHECKED_CAST")
    private fun readResult(result: CaptureResult, key: CaptureResult.Key<*>): Any? = try {
        result.get(key as CaptureResult.Key<Any>)
    } catch (t: Throwable) {
        "<read-error:${t.javaClass.simpleName}>"
    }
}
