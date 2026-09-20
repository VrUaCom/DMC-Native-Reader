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
import android.os.HandlerThread
import android.util.Size
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs

class FlashCaptureProbe(private val context: Context) {
    private val manager = context.getSystemService(CameraManager::class.java)

    fun run(session: ProbeSession): JSONObject {
        val cameraId = manager.cameraIdList.firstOrNull { id ->
            manager.getCameraCharacteristics(id)
                .get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
        } ?: return JSONObject().put("status", "NO_FLASH_CAMERA")

        val c = manager.getCameraCharacteristics(cameraId)
        val maxSingle = c.get(CameraCharacteristics.FLASH_SINGLE_STRENGTH_MAX_LEVEL) ?: 1
        val defaultSingle = c.get(CameraCharacteristics.FLASH_SINGLE_STRENGTH_DEFAULT_LEVEL) ?: 1

        val levels = linkedSetOf(1, defaultSingle, maxSingle)
            .filter { it in 1..maxSingle }
            .sorted()

        val attempts = JSONArray()
        levels.forEach { level ->
            val artifact = captureSingle(cameraId, level)
            val safe = "single_level_${level}"
            session.putJson("flash/${safe}_report.json", artifact.report)
            artifact.jpeg?.let { session.putBytes("flash/${safe}.jpg", it) }
            attempts.put(artifact.report)
            Thread.sleep(350L)
        }

        val summary = JSONObject()
            .put("generated_at_utc", Instant.now().toString())
            .put("camera_id", cameraId)
            .put("single_strength_max_level", maxSingle)
            .put("single_strength_default_level", defaultSingle)
            .put("tested_levels", JsonUtil.value(levels))
            .put("attempts", attempts)
            .put("safety_note",
                "Only min/default/max SINGLE levels are tested to reduce repeated high-power flash heating.")

        session.putJson("flash/single_flash_summary.json", summary)
        return summary
    }

    private fun captureSingle(cameraId: String, strength: Int): CameraCaptureArtifact {
        if (context.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            return CameraCaptureArtifact(
                JSONObject().put("status", "PERMISSION_DENIED").put("camera_id", cameraId),
                null
            )
        }

        val c = manager.getCameraCharacteristics(cameraId)
        val size = selectJpegSize(c)
        val imageThread = HandlerThread("s26-flash-image").also { it.start() }
        val reader = ImageReader.newInstance(size.width, size.height, ImageFormat.JPEG, 2)
        val imageDone = CountDownLatch(1)
        val jpegRef = AtomicReference<ByteArray?>(null)

        reader.setOnImageAvailableListener({ source ->
            val image = try { source.acquireNextImage() } catch (_: Throwable) { null }
            if (image != null) {
                try {
                    val buffer = image.planes[0].buffer
                    val bytes = ByteArray(buffer.remaining())
                    buffer.get(bytes)
                    jpegRef.compareAndSet(null, bytes)
                } finally {
                    image.close()
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
            manager.openCamera(cameraId, executor, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    cameraRef.set(camera)
                    val config = SessionConfiguration(
                        SessionConfiguration.SESSION_REGULAR,
                        listOf(OutputConfiguration(reader.surface)),
                        executor,
                        object : CameraCaptureSession.StateCallback() {
                            override fun onConfigured(session: CameraCaptureSession) {
                                sessionRef.set(session)
                                try {
                                    val request = camera.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE)
                                        .apply {
                                            addTarget(reader.surface)
                                            set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_ON)
                                            set(CaptureRequest.FLASH_MODE, CaptureRequest.FLASH_MODE_SINGLE)
                                            set(CaptureRequest.FLASH_STRENGTH_LEVEL, strength)
                                        }
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
            val result = resultRef.get()
            val jpeg = jpegRef.get()

            val report = JSONObject()
                .put("generated_at_utc", Instant.now().toString())
                .put("camera_id", cameraId)
                .put("status",
                    if (resultOk && imageOk && result != null && jpeg != null) "PASS"
                    else if (!resultOk || !imageOk) "TIMEOUT"
                    else "FAILED")
                .put("requested_mode", "SINGLE")
                .put("requested_strength_level", strength)
                .put("result_flash_mode", JsonUtil.value(result?.get(CaptureResult.FLASH_MODE)))
                .put("result_flash_strength_level", JsonUtil.value(result?.get(CaptureResult.FLASH_STRENGTH_LEVEL)))
                .put("result_flash_state", JsonUtil.value(result?.get(CaptureResult.FLASH_STATE)))
                .put("exposure_time_ns", JsonUtil.value(result?.get(CaptureResult.SENSOR_EXPOSURE_TIME)))
                .put("sensitivity_iso", JsonUtil.value(result?.get(CaptureResult.SENSOR_SENSITIVITY)))
                .put("jpeg_size", JsonUtil.value(size))
                .put("jpeg_bytes", jpeg?.size ?: 0)
                .put("error", errorRef.get() ?: JSONObject.NULL)

            return CameraCaptureArtifact(report, jpeg)
        } catch (t: Throwable) {
            return CameraCaptureArtifact(
                JSONObject()
                    .put("status", "EXCEPTION")
                    .put("camera_id", cameraId)
                    .put("requested_strength_level", strength)
                    .put("exception", t.javaClass.name)
                    .put("message", t.message ?: JSONObject.NULL),
                null
            )
        } finally {
            try { sessionRef.get()?.close() } catch (_: Throwable) {}
            try { cameraRef.get()?.close() } catch (_: Throwable) {}
            try { reader.close() } catch (_: Throwable) {}
            imageThread.quitSafely()
        }
    }

    private fun selectJpegSize(c: CameraCharacteristics): Size {
        val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val sizes = map?.getOutputSizes(ImageFormat.JPEG)?.toList().orEmpty()
        if (sizes.isEmpty()) return Size(1920, 1080)
        val target = 1920L * 1080L
        return sizes.minByOrNull { abs(it.width.toLong() * it.height.toLong() - target) }
            ?: sizes.first()
    }
}
