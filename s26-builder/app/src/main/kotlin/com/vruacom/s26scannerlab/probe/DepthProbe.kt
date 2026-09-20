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
import android.hardware.camera2.TotalCaptureResult
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.ImageReader
import android.os.Handler
import android.os.HandlerThread
import org.json.JSONArray
import org.json.JSONObject
import java.nio.ByteOrder
import java.time.Instant
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

data class DepthCaptureArtifact(
    val report: JSONObject,
    val rawBytes: ByteArray?
)

class DepthProbe(private val context: Context) {
    private val manager = context.getSystemService(CameraManager::class.java)

    fun run(session: ProbeSession): JSONObject {
        val census = JSONArray()
        val captures = JSONArray()

        manager.cameraIdList.toList().sorted().forEach { logicalId ->
            val logical = manager.getCameraCharacteristics(logicalId)
            census.put(describeCamera(logicalId, null, logical))

            val logicalDepth = capture(logicalId, null)
            logicalDepth?.let { artifact ->
                val safe = safe(logicalId)
                session.putJson("depth/logical_${safe}_report.json", artifact.report)
                artifact.rawBytes?.let { bytes ->
                    session.putBytes("depth/logical_${safe}_depth16.raw", bytes)
                }
                captures.put(artifact.report)
            }

            logical.physicalCameraIds.toList().sorted().forEach { physicalId ->
                val physical = manager.getCameraCharacteristics(physicalId)
                census.put(describeCamera(logicalId, physicalId, physical))

                val physicalDepth = capture(logicalId, physicalId)
                physicalDepth?.let { artifact ->
                    val safeLogical = safe(logicalId)
                    val safePhysical = safe(physicalId)
                    session.putJson(
                        "depth/logical_${safeLogical}_physical_${safePhysical}_report.json",
                        artifact.report
                    )
                    artifact.rawBytes?.let { bytes ->
                        session.putBytes(
                            "depth/logical_${safeLogical}_physical_${safePhysical}_depth16.raw",
                            bytes
                        )
                    }
                    captures.put(artifact.report)
                }
            }
        }

        val result = JSONObject()
            .put("generated_at_utc", Instant.now().toString())
            .put("camera_depth_census", census)
            .put("capture_attempts", captures)
            .put("note",
                "This probes Camera2 hardware depth outputs (DEPTH16/point-cloud advertisement). It does not claim ARCore software depth support.")

        session.putJson("depth/summary.json", result)
        return result
    }

    private fun describeCamera(
        logicalId: String,
        physicalId: String?,
        c: CameraCharacteristics
    ): JSONObject {
        val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val depth16 = map?.getOutputSizes(ImageFormat.DEPTH16)?.toList().orEmpty()
        val pointCloud = map?.getOutputSizes(ImageFormat.DEPTH_POINT_CLOUD)?.toList().orEmpty()
        val caps = c.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES) ?: intArrayOf()

        return JSONObject()
            .put("logical_camera_id", logicalId)
            .put("physical_camera_id", physicalId ?: JSONObject.NULL)
            .put("depth_output_capability",
                caps.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_DEPTH_OUTPUT))
            .put("depth16_sizes", JsonUtil.value(depth16))
            .put("depth_point_cloud_sizes", JsonUtil.value(pointCloud))
            .put("depth_is_exclusive", JsonUtil.value(c.get(CameraCharacteristics.DEPTH_DEPTH_IS_EXCLUSIVE)))
            .put("lens_pose_rotation", JsonUtil.value(c.get(CameraCharacteristics.LENS_POSE_ROTATION)))
            .put("lens_pose_translation_m", JsonUtil.value(c.get(CameraCharacteristics.LENS_POSE_TRANSLATION)))
            .put("lens_intrinsic_calibration", JsonUtil.value(c.get(CameraCharacteristics.LENS_INTRINSIC_CALIBRATION)))
    }

    private fun capture(
        logicalId: String,
        physicalId: String?
    ): DepthCaptureArtifact? {
        val characteristics = manager.getCameraCharacteristics(physicalId ?: logicalId)
        val map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val sizes = map?.getOutputSizes(ImageFormat.DEPTH16)?.toList().orEmpty()
        if (sizes.isEmpty()) return null

        if (context.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            return DepthCaptureArtifact(
                JSONObject()
                    .put("status", "PERMISSION_DENIED")
                    .put("logical_camera_id", logicalId)
                    .put("physical_camera_id", physicalId ?: JSONObject.NULL),
                null
            )
        }

        val size = sizes.minByOrNull { it.width.toLong() * it.height.toLong() }!!
        val thread = HandlerThread("s26-depth").also { it.start() }
        val reader = ImageReader.newInstance(size.width, size.height, ImageFormat.DEPTH16, 2)
        val imageDone = CountDownLatch(1)
        val bytesRef = AtomicReference<ByteArray?>(null)
        val imageTimestampRef = AtomicReference<Long?>(null)

        reader.setOnImageAvailableListener({ source ->
            val image = try { source.acquireNextImage() } catch (_: Throwable) { null }
            if (image != null) {
                try {
                    val plane = image.planes.firstOrNull()
                    if (plane != null) {
                        val buffer = plane.buffer
                        val bytes = ByteArray(buffer.remaining())
                        buffer.get(bytes)
                        bytesRef.compareAndSet(null, bytes)
                        imageTimestampRef.compareAndSet(null, image.timestamp)
                    }
                } finally {
                    image.close()
                    imageDone.countDown()
                }
            }
        }, Handler(thread.looper))

        val executor = CameraCallbackExecutor.executor
        val cameraRef = AtomicReference<CameraDevice?>(null)
        val sessionRef = AtomicReference<CameraCaptureSession?>(null)
        val resultRef = AtomicReference<TotalCaptureResult?>(null)
        val resultDone = CountDownLatch(1)
        val errorRef = AtomicReference<String?>(null)

        try {
            manager.openCamera(logicalId, executor, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    cameraRef.set(camera)
                    val output = OutputConfiguration(reader.surface)
                    if (physicalId != null) output.setPhysicalCameraId(physicalId)

                    val config = SessionConfiguration(
                        SessionConfiguration.SESSION_REGULAR,
                        listOf(output),
                        executor,
                        object : CameraCaptureSession.StateCallback() {
                            override fun onConfigured(session: CameraCaptureSession) {
                                sessionRef.set(session)
                                try {
                                    val request = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)
                                        .apply { addTarget(reader.surface) }
                                        .build()
                                    session.captureSingleRequest(
                                        request,
                                        executor,
                                        object : CameraCaptureSession.CaptureCallback() {
                                            override fun onCaptureCompleted(
                                                session: CameraCaptureSession,
                                                request: CaptureRequest,
                                                result: TotalCaptureResult
                                            ) {
                                                resultRef.set(result)
                                                resultDone.countDown()
                                            }

                                            override fun onCaptureFailed(
                                                session: CameraCaptureSession,
                                                request: CaptureRequest,
                                                failure: android.hardware.camera2.CaptureFailure
                                            ) {
                                                errorRef.set("CAPTURE_FAILED_${failure.reason}")
                                                resultDone.countDown()
                                                imageDone.countDown()
                                            }
                                        }
                                    )
                                } catch (t: Throwable) {
                                    errorRef.set("CAPTURE_SUBMIT_${t.javaClass.simpleName}: ${t.message}")
                                    resultDone.countDown()
                                    imageDone.countDown()
                                }
                            }

                            override fun onConfigureFailed(session: CameraCaptureSession) {
                                errorRef.set("SESSION_CONFIGURE_FAILED")
                                resultDone.countDown()
                                imageDone.countDown()
                            }
                        }
                    )
                    camera.createCaptureSession(config)
                }

                override fun onDisconnected(camera: CameraDevice) {
                    errorRef.set("CAMERA_DISCONNECTED")
                    resultDone.countDown()
                    imageDone.countDown()
                    camera.close()
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    errorRef.set("CAMERA_ERROR_$error")
                    resultDone.countDown()
                    imageDone.countDown()
                    camera.close()
                }
            })

            val resultOk = resultDone.await(10, TimeUnit.SECONDS)
            val imageOk = imageDone.await(5, TimeUnit.SECONDS)
            val bytes = bytesRef.get()
            val result = resultRef.get()

            val report = JSONObject()
                .put("generated_at_utc", Instant.now().toString())
                .put("status",
                    if (resultOk && imageOk && bytes != null) "PASS"
                    else if (!resultOk || !imageOk) "TIMEOUT"
                    else "FAILED")
                .put("logical_camera_id", logicalId)
                .put("physical_camera_id", physicalId ?: JSONObject.NULL)
                .put("depth16_size", JsonUtil.value(size))
                .put("byte_order", ByteOrder.nativeOrder().toString())
                .put("raw_bytes", bytes?.size ?: 0)
                .put("image_timestamp_ns", imageTimestampRef.get() ?: JSONObject.NULL)
                .put("capture_result_received", result != null)
                .put("error", errorRef.get() ?: JSONObject.NULL)
                .put("encoding_note",
                    "DEPTH16 pixels are Android depth16 packed values; this file preserves raw plane bytes for offline decoding.")

            return DepthCaptureArtifact(report, bytes)
        } catch (t: Throwable) {
            return DepthCaptureArtifact(
                JSONObject()
                    .put("status", "EXCEPTION")
                    .put("logical_camera_id", logicalId)
                    .put("physical_camera_id", physicalId ?: JSONObject.NULL)
                    .put("exception", t.javaClass.name)
                    .put("message", t.message ?: JSONObject.NULL),
                null
            )
        } finally {
            try { sessionRef.get()?.close() } catch (_: Throwable) {}
            try { cameraRef.get()?.close() } catch (_: Throwable) {}
            try { reader.close() } catch (_: Throwable) {}
            thread.quitSafely()
        }
    }

    private fun safe(id: String): String = id.replace(Regex("[^A-Za-z0-9_.-]"), "_")
}
