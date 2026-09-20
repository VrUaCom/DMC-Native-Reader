package com.vruacom.s26scannerlab.probe

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
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
import android.os.SystemClock
import android.util.Size
import org.json.JSONObject
import java.time.Instant
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs

private data class ScanImage(
    val timestampNs: Long,
    val jpeg: ByteArray
)

class BasicScanRecorder(private val context: Context) {
    private val cameraManager = context.getSystemService(CameraManager::class.java)
    private val sensorManager = context.getSystemService(SensorManager::class.java)

    fun record(
        session: ProbeSession,
        frameCount: Int = 16,
        frameIntervalMs: Long = 450L
    ): JSONObject {
        if (context.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            return JSONObject().put("status", "PERMISSION_DENIED")
        }

        val cameraId = chooseRearCamera()
            ?: return JSONObject().put("status", "NO_REAR_CAMERA")
        val characteristics = cameraManager.getCameraCharacteristics(cameraId)
        val size = selectScanJpegSize(characteristics)

        val imageThread = HandlerThread("s26-scan-images").also { it.start() }
        val sensorThread = HandlerThread("s26-scan-imu").also { it.start() }
        val reader = ImageReader.newInstance(size.width, size.height, ImageFormat.JPEG, 3)
        val imageQueue = LinkedBlockingQueue<ScanImage>(6)

        reader.setOnImageAvailableListener({ source ->
            val image = try { source.acquireNextImage() } catch (_: Throwable) { null }
            if (image != null) {
                try {
                    val buffer = image.planes[0].buffer
                    val bytes = ByteArray(buffer.remaining())
                    buffer.get(bytes)
                    imageQueue.offer(ScanImage(image.timestamp, bytes))
                } finally {
                    image.close()
                }
            }
        }, Handler(imageThread.looper))

        val imuLines = StringBuilder()
        val imuListener = object : SensorEventListener {
            override fun onSensorChanged(event: SensorEvent) {
                val row = JSONObject()
                    .put("sensor_type", event.sensor.type)
                    .put("sensor_string_type", event.sensor.stringType)
                    .put("sensor_timestamp_ns", event.timestamp)
                    .put("receive_elapsed_realtime_ns", SystemClock.elapsedRealtimeNanos())
                    .put("accuracy", event.accuracy)
                    .put("values", JsonUtil.value(event.values.copyOf()))
                synchronized(imuLines) {
                    imuLines.append(row.toString()).append('\n')
                }
            }
            override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
        }

        val imuTypes = listOf(
            Sensor.TYPE_ACCELEROMETER,
            Sensor.TYPE_ACCELEROMETER_UNCALIBRATED,
            Sensor.TYPE_GYROSCOPE,
            Sensor.TYPE_GYROSCOPE_UNCALIBRATED,
            Sensor.TYPE_MAGNETIC_FIELD,
            Sensor.TYPE_ROTATION_VECTOR
        )
        val imuSensors = imuTypes.mapNotNull { sensorManager.getDefaultSensor(it) }
        imuSensors.forEach { sensor ->
            sensorManager.registerListener(
                imuListener,
                sensor,
                0,
                0,
                Handler(sensorThread.looper)
            )
        }

        val cameraExecutor = CameraCallbackExecutor.executor
        val cameraRef = AtomicReference<CameraDevice?>(null)
        val captureSessionRef = AtomicReference<CameraCaptureSession?>(null)
        val cameraOpen = CountDownLatch(1)
        val sessionReady = CountDownLatch(1)
        val setupError = AtomicReference<String?>(null)

        try {
            cameraManager.openCamera(cameraId, cameraExecutor, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    cameraRef.set(camera)
                    cameraOpen.countDown()
                    val config = SessionConfiguration(
                        SessionConfiguration.SESSION_REGULAR,
                        listOf(OutputConfiguration(reader.surface)),
                        cameraExecutor,
                        object : CameraCaptureSession.StateCallback() {
                            override fun onConfigured(session: CameraCaptureSession) {
                                captureSessionRef.set(session)
                                sessionReady.countDown()
                            }

                            override fun onConfigureFailed(session: CameraCaptureSession) {
                                setupError.set("SESSION_CONFIGURE_FAILED")
                                sessionReady.countDown()
                            }
                        }
                    )
                    try {
                        camera.createCaptureSession(config)
                    } catch (t: Throwable) {
                        setupError.set("SESSION_EXCEPTION_${t.javaClass.simpleName}: ${t.message}")
                        sessionReady.countDown()
                    }
                }

                override fun onDisconnected(camera: CameraDevice) {
                    setupError.set("CAMERA_DISCONNECTED")
                    cameraOpen.countDown()
                    sessionReady.countDown()
                    camera.close()
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    setupError.set("CAMERA_ERROR_$error")
                    cameraOpen.countDown()
                    sessionReady.countDown()
                    camera.close()
                }
            })

            if (!cameraOpen.await(8, TimeUnit.SECONDS)) {
                return JSONObject().put("status", "CAMERA_OPEN_TIMEOUT")
            }
            if (!sessionReady.await(8, TimeUnit.SECONDS)) {
                return JSONObject().put("status", "SESSION_TIMEOUT")
            }
            setupError.get()?.let {
                return JSONObject().put("status", "SETUP_FAILED").put("error", it)
            }

            val camera = cameraRef.get()
                ?: return JSONObject().put("status", "CAMERA_MISSING")
            val captureSession = captureSessionRef.get()
                ?: return JSONObject().put("status", "SESSION_MISSING")

            val requestBuilder = camera.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE)
                .apply {
                    addTarget(reader.surface)
                    set(CaptureRequest.JPEG_QUALITY, 92.toByte())
                    val afModes = characteristics.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)
                        ?: intArrayOf()
                    if (afModes.contains(CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)) {
                        set(
                            CaptureRequest.CONTROL_AF_MODE,
                            CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE
                        )
                    }
                    val zoomRange = characteristics.get(CameraCharacteristics.CONTROL_ZOOM_RATIO_RANGE)
                    if (zoomRange != null && zoomRange.contains(1.0f)) {
                        set(CaptureRequest.CONTROL_ZOOM_RATIO, 1.0f)
                    }
                }

            val metadataLines = StringBuilder()
            val startElapsed = SystemClock.elapsedRealtimeNanos()
            val startUtc = Instant.now().toString()
            var captured = 0

            for (index in 0 until frameCount) {
                imageQueue.clear()
                val resultLatch = CountDownLatch(1)
                val resultRef = AtomicReference<TotalCaptureResult?>(null)
                val errorRef = AtomicReference<String?>(null)

                try {
                    captureSession.captureSingleRequest(
                        requestBuilder.build(),
                        cameraExecutor,
                        object : CameraCaptureSession.CaptureCallback() {
                            override fun onCaptureCompleted(
                                session: CameraCaptureSession,
                                request: CaptureRequest,
                                result: TotalCaptureResult
                            ) {
                                resultRef.set(result)
                                resultLatch.countDown()
                            }

                            override fun onCaptureFailed(
                                session: CameraCaptureSession,
                                request: CaptureRequest,
                                failure: android.hardware.camera2.CaptureFailure
                            ) {
                                errorRef.set("CAPTURE_FAILED_${failure.reason}")
                                resultLatch.countDown()
                            }
                        }
                    )
                } catch (t: Throwable) {
                    errorRef.set("CAPTURE_EXCEPTION_${t.javaClass.simpleName}: ${t.message}")
                    resultLatch.countDown()
                }

                val resultOk = resultLatch.await(6, TimeUnit.SECONDS)
                val image = imageQueue.poll(4, TimeUnit.SECONDS)
                val result = resultRef.get()

                val row = JSONObject()
                    .put("index", index)
                    .put("result_completed", resultOk && result != null)
                    .put("error", errorRef.get() ?: JSONObject.NULL)
                    .put("callback_elapsed_realtime_ns", SystemClock.elapsedRealtimeNanos())

                if (result != null) {
                    row.put("frame_number", result.frameNumber)
                    row.put("sensor_timestamp_ns", JsonUtil.value(result.get(CaptureResult.SENSOR_TIMESTAMP)))
                    row.put("exposure_time_ns", JsonUtil.value(result.get(CaptureResult.SENSOR_EXPOSURE_TIME)))
                    row.put("sensitivity_iso", JsonUtil.value(result.get(CaptureResult.SENSOR_SENSITIVITY)))
                    row.put("focus_distance_diopters", JsonUtil.value(result.get(CaptureResult.LENS_FOCUS_DISTANCE)))
                    row.put("focal_length_mm", JsonUtil.value(result.get(CaptureResult.LENS_FOCAL_LENGTH)))
                    row.put("af_state", JsonUtil.value(result.get(CaptureResult.CONTROL_AF_STATE)))
                }

                if (image != null) {
                    val fileName = "scan/frames/frame_${index.toString().padStart(3, '0')}.jpg"
                    session.putBytes(fileName, image.jpeg)
                    row.put("image_file", fileName)
                    row.put("image_timestamp_ns", image.timestampNs)
                    row.put("image_bytes", image.jpeg.size)
                    captured++
                } else {
                    row.put("image_file", JSONObject.NULL)
                }

                metadataLines.append(row.toString()).append('\n')
                if (index + 1 < frameCount) Thread.sleep(frameIntervalMs)
            }

            val endElapsed = SystemClock.elapsedRealtimeNanos()
            session.putText("scan/frame_metadata.ndjson", metadataLines.toString())
            session.putText("scan/imu_samples.ndjson", synchronized(imuLines) { imuLines.toString() })

            val summary = JSONObject()
                .put("status", if (captured == frameCount) "PASS" else "PARTIAL")
                .put("started_at_utc", startUtc)
                .put("finished_at_utc", Instant.now().toString())
                .put("camera_id", cameraId)
                .put("image_size", JsonUtil.value(size))
                .put("requested_frames", frameCount)
                .put("captured_frames", captured)
                .put("frame_interval_ms", frameIntervalMs)
                .put("elapsed_ns", endElapsed - startElapsed)
                .put("imu_sensor_count", imuSensors.size)
                .put("note",
                    "This is a synchronized capture dataset for reconstruction R&D, not yet a finished 3D mesh.")

            session.putJson("scan/summary.json", summary)
            return summary
        } catch (t: Throwable) {
            return JSONObject()
                .put("status", "EXCEPTION")
                .put("exception", t.javaClass.name)
                .put("message", t.message ?: JSONObject.NULL)
        } finally {
            sensorManager.unregisterListener(imuListener)
            try { captureSessionRef.get()?.close() } catch (_: Throwable) {}
            try { cameraRef.get()?.close() } catch (_: Throwable) {}
            try { reader.close() } catch (_: Throwable) {}
            imageThread.quitSafely()
            sensorThread.quitSafely()
        }
    }

    private fun chooseRearCamera(): String? =
        cameraManager.cameraIdList.firstOrNull { id ->
            cameraManager.getCameraCharacteristics(id)
                .get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK
        }

    private fun selectScanJpegSize(c: CameraCharacteristics): Size {
        val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val sizes = map?.getOutputSizes(ImageFormat.JPEG)?.toList().orEmpty()
        if (sizes.isEmpty()) return Size(3840, 2160)
        val target = 3840L * 2160L
        return sizes.minByOrNull { abs(it.width.toLong() * it.height.toLong() - target) }
            ?: sizes.first()
    }
}
