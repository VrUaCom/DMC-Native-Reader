package com.vruacom.s26scannerlab.probe

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureFailure
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
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs

data class CameraCaptureArtifact(
    val report: JSONObject,
    val jpeg: ByteArray?
)

class CameraProbe(private val context: Context) {
    private val manager = context.getSystemService(CameraManager::class.java)

    fun collectCensus(): JSONObject {
        val root = JSONObject()
            .put("generated_at_utc", Instant.now().toString())

        val advertisedConcurrent = JSONArray()
        manager.concurrentCameraIds
            .map { it.toList().sorted() }
            .sortedBy { it.joinToString(",") }
            .forEach { set -> advertisedConcurrent.put(JsonUtil.value(set)) }
        root.put("advertised_concurrent_camera_sets", advertisedConcurrent)

        val cameras = JSONArray()
        manager.cameraIdList.forEach { id ->
            val c = manager.getCameraCharacteristics(id)
            val caps = c.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES) ?: intArrayOf()
            val camera = JSONObject()
                .put("camera_id", id)
                .put("lens_facing", JsonUtil.value(c.get(CameraCharacteristics.LENS_FACING)))
                .put("hardware_level", JsonUtil.value(c.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)))
                .put("physical_camera_ids", JsonUtil.value(c.physicalCameraIds.toList().sorted()))
                .put("capabilities", JsonUtil.value(caps))
                .put("is_logical_multi_camera",
                    caps.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA))
                .put("raw_supported",
                    caps.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_RAW))
                .put("depth_output_supported",
                    caps.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_DEPTH_OUTPUT))
                .put("manual_sensor_supported",
                    caps.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR))
                .put("sensor_orientation", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_ORIENTATION)))
                .put("sensor_timestamp_source", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE)))
                .put("pixel_array_size", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE)))
                .put("active_array_size", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE)))
                .put("physical_sensor_size", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE)))
                .put("color_filter_arrangement", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_INFO_COLOR_FILTER_ARRANGEMENT)))
                .put("white_level", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_INFO_WHITE_LEVEL)))
                .put("black_level_pattern", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_BLACK_LEVEL_PATTERN)))
                .put("sensitivity_range", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE)))
                .put("exposure_time_range_ns", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE)))
                .put("max_frame_duration_ns", JsonUtil.value(c.get(CameraCharacteristics.SENSOR_INFO_MAX_FRAME_DURATION)))
                .put("focal_lengths_mm", JsonUtil.value(c.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)))
                .put("apertures", JsonUtil.value(c.get(CameraCharacteristics.LENS_INFO_AVAILABLE_APERTURES)))
                .put("minimum_focus_distance_diopters", JsonUtil.value(c.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE)))
                .put("lens_intrinsic_calibration", JsonUtil.value(c.get(CameraCharacteristics.LENS_INTRINSIC_CALIBRATION)))
                .put("lens_distortion", JsonUtil.value(c.get(CameraCharacteristics.LENS_DISTORTION)))
                .put("lens_pose_rotation", JsonUtil.value(c.get(CameraCharacteristics.LENS_POSE_ROTATION)))
                .put("lens_pose_translation_m", JsonUtil.value(c.get(CameraCharacteristics.LENS_POSE_TRANSLATION)))
                .put("available_ois_modes", JsonUtil.value(c.get(CameraCharacteristics.LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION)))
                .put("available_video_stabilization_modes", JsonUtil.value(c.get(CameraCharacteristics.CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES)))
                .put("af_modes", JsonUtil.value(c.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)))
                .put("ae_modes", JsonUtil.value(c.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_MODES)))
                .put("awb_modes", JsonUtil.value(c.get(CameraCharacteristics.CONTROL_AWB_AVAILABLE_MODES)))
                .put("max_digital_zoom", JsonUtil.value(c.get(CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM)))
                .put("zoom_ratio_range", JsonUtil.value(c.get(CameraCharacteristics.CONTROL_ZOOM_RATIO_RANGE)))
                .put("flash_available", JsonUtil.value(c.get(CameraCharacteristics.FLASH_INFO_AVAILABLE)))
                .put("torch_strength_max", JsonUtil.value(c.get(CameraCharacteristics.FLASH_INFO_STRENGTH_MAXIMUM_LEVEL)))
                .put("torch_strength_default", JsonUtil.value(c.get(CameraCharacteristics.FLASH_INFO_STRENGTH_DEFAULT_LEVEL)))
                .put("manual_single_flash_strength_max", JsonUtil.value(c.get(CameraCharacteristics.FLASH_SINGLE_STRENGTH_MAX_LEVEL)))
                .put("manual_single_flash_strength_default", JsonUtil.value(c.get(CameraCharacteristics.FLASH_SINGLE_STRENGTH_DEFAULT_LEVEL)))
                .put("manual_torch_strength_max", JsonUtil.value(c.get(CameraCharacteristics.FLASH_TORCH_STRENGTH_MAX_LEVEL)))
                .put("manual_torch_strength_default", JsonUtil.value(c.get(CameraCharacteristics.FLASH_TORCH_STRENGTH_DEFAULT_LEVEL)))

            val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
            val streams = JSONArray()
            map?.outputFormats?.sorted()?.forEach { format ->
                val sizes = try {
                    map.getOutputSizes(format)?.sortedBy { it.width.toLong() * it.height.toLong() } ?: emptyList()
                } catch (_: Throwable) {
                    emptyList()
                }
                streams.put(JSONObject()
                    .put("format", format)
                    .put("format_name", formatName(format))
                    .put("sizes", JsonUtil.value(sizes)))
            }
            camera.put("output_streams", streams)

            val allCharacteristicKeys = c.keys.map { it.name }.sorted()
            val requestKeys = c.availableCaptureRequestKeys.map { it.name }.sorted()
            val resultKeys = c.availableCaptureResultKeys.map { it.name }.sorted()
            camera.put("characteristic_key_names", JsonUtil.value(allCharacteristicKeys))
            camera.put("capture_request_key_names", JsonUtil.value(requestKeys))
            camera.put("capture_result_key_names", JsonUtil.value(resultKeys))

            val vendorCharacteristics = JSONObject()
            c.keys
                .filter { !it.name.startsWith("android.") }
                .sortedBy { it.name }
                .forEach { key ->
                    val value = tryReadCharacteristic(c, key)
                    vendorCharacteristics.put(key.name, JsonUtil.value(value))
                }
            camera.put("vendor_characteristics", vendorCharacteristics)
            camera.put("vendor_capture_request_key_names",
                JsonUtil.value(requestKeys.filter { !it.startsWith("android.") }))
            camera.put("vendor_capture_result_key_names",
                JsonUtil.value(resultKeys.filter { !it.startsWith("android.") }))

            val physicalDetails = JSONArray()
            c.physicalCameraIds.toList().sorted().forEach { physicalId ->
                val pc = manager.getCameraCharacteristics(physicalId)
                val pcMap = pc.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                val jpegSizes = pcMap?.getOutputSizes(ImageFormat.JPEG)?.toList().orEmpty()
                    .sortedBy { it.width.toLong() * it.height.toLong() }
                physicalDetails.put(JSONObject()
                    .put("physical_camera_id", physicalId)
                    .put("lens_facing", JsonUtil.value(pc.get(CameraCharacteristics.LENS_FACING)))
                    .put("sensor_orientation", JsonUtil.value(pc.get(CameraCharacteristics.SENSOR_ORIENTATION)))
                    .put("pixel_array_size", JsonUtil.value(pc.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE)))
                    .put("active_array_size", JsonUtil.value(pc.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE)))
                    .put("physical_sensor_size", JsonUtil.value(pc.get(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE)))
                    .put("focal_lengths_mm", JsonUtil.value(pc.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)))
                    .put("apertures", JsonUtil.value(pc.get(CameraCharacteristics.LENS_INFO_AVAILABLE_APERTURES)))
                    .put("minimum_focus_distance_diopters", JsonUtil.value(pc.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE)))
                    .put("intrinsic_calibration", JsonUtil.value(pc.get(CameraCharacteristics.LENS_INTRINSIC_CALIBRATION)))
                    .put("distortion", JsonUtil.value(pc.get(CameraCharacteristics.LENS_DISTORTION)))
                    .put("jpeg_sizes", JsonUtil.value(jpegSizes))
                    .put("characteristic_key_names", JsonUtil.value(pc.keys.map { it.name }.sorted())))
            }
            camera.put("physical_camera_characteristics", physicalDetails)

            cameras.put(camera)
        }

        root.put("logical_camera_count", manager.cameraIdList.size)
        root.put("cameras", cameras)
        return root
    }

    fun captureOneLogicalCamera(cameraId: String): CameraCaptureArtifact =
        captureCamera(cameraId, null)

    fun captureOnePhysicalCamera(
        logicalCameraId: String,
        physicalCameraId: String
    ): CameraCaptureArtifact = captureCamera(logicalCameraId, physicalCameraId)

    private fun captureCamera(
        cameraId: String,
        physicalCameraId: String?
    ): CameraCaptureArtifact {
        if (context.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            return CameraCaptureArtifact(
                JSONObject()
                    .put("camera_id", cameraId)
                    .put("physical_camera_id", physicalCameraId ?: JSONObject.NULL)
                    .put("status", "PERMISSION_DENIED"),
                null
            )
        }

        val c = manager.getCameraCharacteristics(physicalCameraId ?: cameraId)
        val jpegSize = selectJpegSize(c)
        val reader = ImageReader.newInstance(jpegSize.width, jpegSize.height, ImageFormat.JPEG, 2)
        val executor = CameraCallbackExecutor.executor
        val captureDone = CountDownLatch(1)
        val imageDone = CountDownLatch(1)
        val reportRef = AtomicReference<JSONObject?>(null)
        val errorRef = AtomicReference<String?>(null)
        val jpegRef = AtomicReference<ByteArray?>(null)
        val cameraRef = AtomicReference<CameraDevice?>(null)
        val sessionRef = AtomicReference<CameraCaptureSession?>(null)

        reader.setOnImageAvailableListener({ source ->
            val image = try { source.acquireLatestImage() } catch (_: Throwable) { null }
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
        }, Handler(Looper.getMainLooper()))

        try {
            manager.openCamera(cameraId, executor, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    cameraRef.set(camera)
                    val output = OutputConfiguration(reader.surface)
                    if (physicalCameraId != null) {
                        output.setPhysicalCameraId(physicalCameraId)
                    }
                    val outputs = listOf(output)
                    val config = SessionConfiguration(
                        SessionConfiguration.SESSION_REGULAR,
                        outputs,
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
                                                reportRef.set(captureResultToJson(cameraId, physicalCameraId, jpegSize, result))
                                                captureDone.countDown()
                                            }

                                            override fun onCaptureFailed(
                                                session: CameraCaptureSession,
                                                request: CaptureRequest,
                                                failure: CaptureFailure
                                            ) {
                                                errorRef.set("CAPTURE_FAILED reason=${failure.reason} sequence=${failure.sequenceId}")
                                                captureDone.countDown()
                                                imageDone.countDown()
                                            }
                                        }
                                    )
                                } catch (t: Throwable) {
                                    errorRef.set("CAPTURE_SUBMIT_ERROR ${t.javaClass.simpleName}: ${t.message}")
                                    captureDone.countDown()
                                    imageDone.countDown()
                                }
                            }

                            override fun onConfigureFailed(session: CameraCaptureSession) {
                                errorRef.set("SESSION_CONFIGURE_FAILED")
                                captureDone.countDown()
                                imageDone.countDown()
                            }
                        }
                    )
                    camera.createCaptureSession(config)
                }

                override fun onDisconnected(camera: CameraDevice) {
                    errorRef.set("CAMERA_DISCONNECTED")
                    captureDone.countDown()
                    imageDone.countDown()
                    camera.close()
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    errorRef.set("CAMERA_ERROR code=$error")
                    captureDone.countDown()
                    imageDone.countDown()
                    camera.close()
                }
            })

            val completed = captureDone.await(12, TimeUnit.SECONDS)
            if (completed) imageDone.await(3, TimeUnit.SECONDS)

            val report = reportRef.get() ?: JSONObject()
                .put("camera_id", cameraId)
                .put("physical_camera_id", physicalCameraId ?: JSONObject.NULL)
                .put("status", if (completed) "FAILED" else "TIMEOUT")
                .put("error", errorRef.get() ?: JSONObject.NULL)
                .put("requested_jpeg_size", JsonUtil.value(jpegSize))
                .put("callback_elapsed_realtime_ns", SystemClock.elapsedRealtimeNanos())

            report.put("jpeg_received", jpegRef.get() != null)
            report.put("jpeg_bytes", jpegRef.get()?.size ?: 0)
            if (errorRef.get() != null) report.put("error", errorRef.get())
            return CameraCaptureArtifact(report, jpegRef.get())
        } catch (t: Throwable) {
            return CameraCaptureArtifact(
                JSONObject()
                    .put("camera_id", cameraId)
                    .put("physical_camera_id", physicalCameraId ?: JSONObject.NULL)
                    .put("status", "OPEN_OR_SESSION_EXCEPTION")
                    .put("exception", t.javaClass.name)
                    .put("message", t.message ?: JSONObject.NULL),
                null
            )
        } finally {
            try { sessionRef.get()?.close() } catch (_: Throwable) {}
            try { cameraRef.get()?.close() } catch (_: Throwable) {}
            try { reader.close() } catch (_: Throwable) {}
        }
    }

    private fun captureResultToJson(
        cameraId: String,
        physicalCameraId: String?,
        jpegSize: Size,
        result: TotalCaptureResult
    ): JSONObject {
        val all = JSONObject()
        val vendor = JSONObject()

        result.keys.sortedBy { it.name }.forEach { key ->
            val value = tryReadCaptureResult(result, key)
            all.put(key.name, JsonUtil.value(value))
            if (!key.name.startsWith("android.")) {
                vendor.put(key.name, JsonUtil.value(value))
            }
        }

        val physicalResults = JSONObject()
        result.physicalCameraTotalResults.toSortedMap().forEach { (id, physicalResult) ->
            val metadata = JSONObject()
            physicalResult.keys.sortedBy { it.name }.forEach { key ->
                metadata.put(key.name, JsonUtil.value(tryReadCaptureResult(physicalResult, key)))
            }
            physicalResults.put(id, JSONObject()
                .put("camera_id", physicalResult.cameraId)
                .put("sensor_timestamp_ns", JsonUtil.value(physicalResult.get(CaptureResult.SENSOR_TIMESTAMP)))
                .put("metadata", metadata))
        }

        return JSONObject()
            .put("camera_id", cameraId)
            .put("physical_camera_id", physicalCameraId ?: JSONObject.NULL)
            .put("status", "PASS")
            .put("requested_jpeg_size", JsonUtil.value(jpegSize))
            .put("callback_elapsed_realtime_ns", SystemClock.elapsedRealtimeNanos())
            .put("sensor_timestamp_ns", JsonUtil.value(result.get(CaptureResult.SENSOR_TIMESTAMP)))
            .put("frame_number", result.frameNumber)
            .put("sequence_id", result.sequenceId)
            .put("metadata", all)
            .put("vendor_result_values", vendor)
            .put("physical_camera_total_results", physicalResults)
    }

    private fun selectJpegSize(c: CameraCharacteristics): Size {
        val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        val sizes = map?.getOutputSizes(ImageFormat.JPEG)?.toList().orEmpty()
        if (sizes.isEmpty()) return Size(1920, 1080)
        val targetArea = 1920L * 1080L
        return sizes.minByOrNull { abs(it.width.toLong() * it.height.toLong() - targetArea) }
            ?: sizes.first()
    }

    @Suppress("UNCHECKED_CAST")
    private fun tryReadCharacteristic(
        c: CameraCharacteristics,
        key: CameraCharacteristics.Key<*>
    ): Any? = try {
        c.get(key as CameraCharacteristics.Key<Any>)
    } catch (t: Throwable) {
        "<read-error:${t.javaClass.simpleName}>"
    }

    @Suppress("UNCHECKED_CAST")
    private fun tryReadCaptureResult(
        result: CaptureResult,
        key: CaptureResult.Key<*>
    ): Any? = try {
        result.get(key as CaptureResult.Key<Any>)
    } catch (t: Throwable) {
        "<read-error:${t.javaClass.simpleName}>"
    }

    private fun formatName(format: Int): String = when (format) {
        ImageFormat.JPEG -> "JPEG"
        ImageFormat.YUV_420_888 -> "YUV_420_888"
        ImageFormat.RAW_SENSOR -> "RAW_SENSOR"
        ImageFormat.RAW10 -> "RAW10"
        ImageFormat.RAW12 -> "RAW12"
        ImageFormat.DEPTH16 -> "DEPTH16"
        ImageFormat.DEPTH_POINT_CLOUD -> "DEPTH_POINT_CLOUD"
        ImageFormat.PRIVATE -> "PRIVATE"
        ImageFormat.HEIC -> "HEIC"
        else -> "FORMAT_$format"
    }
}
