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
import android.hardware.camera2.DngCreator
import android.hardware.camera2.TotalCaptureResult
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.Image
import android.media.ImageReader
import android.os.Handler
import android.os.HandlerThread
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.time.Instant
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

data class RawCaptureArtifact(
    val report: JSONObject,
    val dng: ByteArray?
)

class RawProbe(private val context: Context) {
    private val manager = context.getSystemService(CameraManager::class.java)

    fun captureLogical(cameraId: String): RawCaptureArtifact =
        capture(cameraId, null)

    fun capturePhysical(logicalCameraId: String, physicalCameraId: String): RawCaptureArtifact =
        capture(logicalCameraId, physicalCameraId)

    private fun capture(logicalCameraId: String, physicalCameraId: String?): RawCaptureArtifact {
        if (context.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            return RawCaptureArtifact(
                JSONObject().put("status", "PERMISSION_DENIED"),
                null
            )
        }

        val characteristicsId = physicalCameraId ?: logicalCameraId
        val characteristics = manager.getCameraCharacteristics(characteristicsId)
        val streamMap = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val rawSizes = streamMap?.getOutputSizes(ImageFormat.RAW_SENSOR)?.toList().orEmpty()
        if (rawSizes.isEmpty()) {
            return RawCaptureArtifact(
                JSONObject()
                    .put("status", "RAW_SENSOR_NOT_ADVERTISED")
                    .put("logical_camera_id", logicalCameraId)
                    .put("physical_camera_id", physicalCameraId ?: JSONObject.NULL),
                null
            )
        }

        val rawSize = rawSizes.minByOrNull { it.width.toLong() * it.height.toLong() }!!
        val imageThread = HandlerThread("s26-raw-image").also { it.start() }
        val reader = ImageReader.newInstance(rawSize.width, rawSize.height, ImageFormat.RAW_SENSOR, 2)
        val imageRef = AtomicReference<Image?>(null)
        val imageDone = CountDownLatch(1)

        reader.setOnImageAvailableListener({ source ->
            val image = try { source.acquireNextImage() } catch (_: Throwable) { null }
            if (image != null) {
                if (!imageRef.compareAndSet(null, image)) {
                    image.close()
                } else {
                    imageDone.countDown()
                }
            }
        }, Handler(imageThread.looper))

        val executor = CameraCallbackExecutor.executor
        val cameraRef = AtomicReference<CameraDevice?>(null)
        val sessionRef = AtomicReference<CameraCaptureSession?>(null)
        val resultRef = AtomicReference<TotalCaptureResult?>(null)
        val resultDone = CountDownLatch(1)
        val errorRef = AtomicReference<String?>(null)

        try {
            manager.openCamera(logicalCameraId, executor, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    cameraRef.set(camera)
                    val output = OutputConfiguration(reader.surface)
                    if (physicalCameraId != null) output.setPhysicalCameraId(physicalCameraId)

                    val config = SessionConfiguration(
                        SessionConfiguration.SESSION_REGULAR,
                        listOf(output),
                        executor,
                        object : CameraCaptureSession.StateCallback() {
                            override fun onConfigured(session: CameraCaptureSession) {
                                sessionRef.set(session)
                                try {
                                    val request = camera.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE)
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

            val resultOk = resultDone.await(12, TimeUnit.SECONDS)
            val imageOk = imageDone.await(6, TimeUnit.SECONDS)
            val result = resultRef.get()
            val image = imageRef.get()

            if (!resultOk || result == null || !imageOk || image == null) {
                return RawCaptureArtifact(
                    JSONObject()
                        .put("status", if (!resultOk || !imageOk) "TIMEOUT" else "FAILED")
                        .put("logical_camera_id", logicalCameraId)
                        .put("physical_camera_id", physicalCameraId ?: JSONObject.NULL)
                        .put("raw_size", JsonUtil.value(rawSize))
                        .put("error", errorRef.get() ?: JSONObject.NULL)
                        .put("capture_result_received", result != null)
                        .put("raw_image_received", image != null),
                    null
                )
            }

            val dngResult = if (physicalCameraId == null) {
                result
            } else {
                result.physicalCameraTotalResults[physicalCameraId]
            }

            if (dngResult == null) {
                return RawCaptureArtifact(
                    JSONObject()
                        .put("status", "PHYSICAL_RESULT_MISSING")
                        .put("logical_camera_id", logicalCameraId)
                        .put("physical_camera_id", physicalCameraId)
                        .put("raw_size", JsonUtil.value(rawSize)),
                    null
                )
            }

            val dngBytes = try {
                val out = ByteArrayOutputStream()
                DngCreator(characteristics, dngResult).use { creator ->
                    creator.writeImage(out, image)
                }
                out.toByteArray()
            } catch (t: Throwable) {
                return RawCaptureArtifact(
                    JSONObject()
                        .put("status", "DNG_WRITE_FAILED")
                        .put("logical_camera_id", logicalCameraId)
                        .put("physical_camera_id", physicalCameraId ?: JSONObject.NULL)
                        .put("exception", t.javaClass.name)
                        .put("message", t.message ?: JSONObject.NULL),
                    null
                )
            }

            return RawCaptureArtifact(
                JSONObject()
                    .put("generated_at_utc", Instant.now().toString())
                    .put("status", "PASS")
                    .put("logical_camera_id", logicalCameraId)
                    .put("physical_camera_id", physicalCameraId ?: JSONObject.NULL)
                    .put("raw_size", JsonUtil.value(rawSize))
                    .put("dng_bytes", dngBytes.size)
                    .put("sensor_timestamp_ns",
                        JsonUtil.value(dngResult.get(android.hardware.camera2.CaptureResult.SENSOR_TIMESTAMP)))
                    .put("color_filter_arrangement",
                        JsonUtil.value(characteristics.get(CameraCharacteristics.SENSOR_INFO_COLOR_FILTER_ARRANGEMENT)))
                    .put("white_level",
                        JsonUtil.value(characteristics.get(CameraCharacteristics.SENSOR_INFO_WHITE_LEVEL)))
                    .put("black_level_pattern",
                        JsonUtil.value(characteristics.get(CameraCharacteristics.SENSOR_BLACK_LEVEL_PATTERN))),
                dngBytes
            )
        } catch (t: Throwable) {
            return RawCaptureArtifact(
                JSONObject()
                    .put("status", "EXCEPTION")
                    .put("logical_camera_id", logicalCameraId)
                    .put("physical_camera_id", physicalCameraId ?: JSONObject.NULL)
                    .put("exception", t.javaClass.name)
                    .put("message", t.message ?: JSONObject.NULL),
                null
            )
        } finally {
            try { imageRef.getAndSet(null)?.close() } catch (_: Throwable) {}
            try { sessionRef.get()?.close() } catch (_: Throwable) {}
            try { cameraRef.get()?.close() } catch (_: Throwable) {}
            try { reader.close() } catch (_: Throwable) {}
            imageThread.quitSafely()
        }
    }
}
