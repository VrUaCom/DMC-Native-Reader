package com.vruacom.s26scannerlab.probe

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant

class ProbeRunner(private val context: Context) {
    private fun runStage(
        session: ProbeSession,
        stageName: String,
        block: () -> JSONObject
    ): JSONObject {
        val started = Instant.now().toString()
        session.putJson(
            "checkpoints/${stageName}_started.json",
            JSONObject().put("stage", stageName).put("status", "STARTED").put("utc", started)
        )

        return try {
            val result = block()
            session.putJson(
                "checkpoints/${stageName}_finished.json",
                JSONObject()
                    .put("stage", stageName)
                    .put("status", "FINISHED")
                    .put("started_at_utc", started)
                    .put("finished_at_utc", Instant.now().toString())
                    .put("result", result)
            )
            result
        } catch (t: Throwable) {
            val error = JSONObject()
                .put("stage", stageName)
                .put("status", "EXCEPTION")
                .put("started_at_utc", started)
                .put("failed_at_utc", Instant.now().toString())
                .put("exception", t.javaClass.name)
                .put("message", t.message ?: JSONObject.NULL)
            session.putJson("checkpoints/${stageName}_failed.json", error)
            error
        }
    }

    fun runCensus(session: ProbeSession): JSONObject {
        val device = DeviceProbe(context).collect()
        val cameras = CameraProbe(context).collectCensus()
        val sensors = SensorProbe(context).collectCensus()

        session.putJson("device/device_report.json", device)
        session.putJson("camera/camera_capabilities.json", cameras)
        session.putJson("sensors/sensor_capabilities.json", sensors)

        val summary = JSONObject()
            .put("probe", "S26 Scanner Lab v0.1 hardware census")
            .put("generated_at_utc", Instant.now().toString())
            .put("device_file", "device/device_report.json")
            .put("camera_file", "camera/camera_capabilities.json")
            .put("sensor_file", "sensors/sensor_capabilities.json")
            .put("artifact_count", session.size())

        session.putJson("probe_summary.json", summary)
        return summary
    }

    fun runCameraMetadataCapture(
        session: ProbeSession,
        includePhysical: Boolean = true
    ): JSONObject {
        val probe = CameraProbe(context)
        val ids = context.getSystemService(android.hardware.camera2.CameraManager::class.java).cameraIdList
        val results = JSONArray()

        val physicalResults = JSONArray()

        ids.forEach { id ->
            val artifact = probe.captureOneLogicalCamera(id)
            val safe = id.replace(Regex("[^A-Za-z0-9_.-]"), "_")
            session.putJson("camera/captures/camera_${safe}_metadata.json", artifact.report)
            artifact.jpeg?.let {
                session.putBytes("camera/captures/camera_${safe}_sample.jpg", it)
            }
            results.put(JSONObject()
                .put("camera_id", id)
                .put("status", artifact.report.optString("status", "UNKNOWN"))
                .put("jpeg_bytes", artifact.jpeg?.size ?: 0))

            val characteristics = context
                .getSystemService(android.hardware.camera2.CameraManager::class.java)
                .getCameraCharacteristics(id)

            if (includePhysical) {
                characteristics.physicalCameraIds.toList().sorted().forEach { physicalId ->
                    val physicalArtifact = probe.captureOnePhysicalCamera(id, physicalId)
                    val safePhysical = physicalId.replace(Regex("[^A-Za-z0-9_.-]"), "_")
                    session.putJson(
                        "camera/physical/logical_${safe}_physical_${safePhysical}_metadata.json",
                        physicalArtifact.report
                    )
                    physicalArtifact.jpeg?.let {
                        session.putBytes(
                            "camera/physical/logical_${safe}_physical_${safePhysical}_sample.jpg",
                            it
                        )
                    }
                    physicalResults.put(JSONObject()
                        .put("logical_camera_id", id)
                        .put("physical_camera_id", physicalId)
                        .put("status", physicalArtifact.report.optString("status", "UNKNOWN"))
                        .put("jpeg_bytes", physicalArtifact.jpeg?.size ?: 0))
                }
            }
        }

        val summary = JSONObject()
            .put("generated_at_utc", Instant.now().toString())
            .put("logical_camera_results", results)
            .put("physical_camera_results", physicalResults)
        session.putJson("camera/captures/summary.json", summary)
        return summary
    }

    fun runBasicScan(session: ProbeSession): JSONObject =
        BasicScanRecorder(context).record(session)

    fun runDepthProbe(session: ProbeSession): JSONObject =
        DepthProbe(context).run(session)

    fun runFlashCaptureProbe(session: ProbeSession): JSONObject =
        FlashCaptureProbe(context).run(session)

    fun runRawProbe(session: ProbeSession): JSONObject {
        val manager = context.getSystemService(android.hardware.camera2.CameraManager::class.java)
        val probe = RawProbe(context)
        val logicalResults = JSONArray()
        val physicalResults = JSONArray()

        manager.cameraIdList.toList().sorted().forEach { logicalId ->
            val safeLogical = logicalId.replace(Regex("[^A-Za-z0-9_.-]"), "_")
            val logical = probe.captureLogical(logicalId)
            session.putJson("raw/logical_${safeLogical}_report.json", logical.report)
            logical.dng?.let { session.putBytes("raw/logical_${safeLogical}.dng", it) }
            logicalResults.put(JSONObject()
                .put("camera_id", logicalId)
                .put("status", logical.report.optString("status", "UNKNOWN"))
                .put("dng_bytes", logical.dng?.size ?: 0))

            val c = manager.getCameraCharacteristics(logicalId)
            c.physicalCameraIds.toList().sorted().forEach { physicalId ->
                val safePhysical = physicalId.replace(Regex("[^A-Za-z0-9_.-]"), "_")
                val physical = probe.capturePhysical(logicalId, physicalId)
                session.putJson(
                    "raw/physical/logical_${safeLogical}_physical_${safePhysical}_report.json",
                    physical.report
                )
                physical.dng?.let {
                    session.putBytes(
                        "raw/physical/logical_${safeLogical}_physical_${safePhysical}.dng",
                        it
                    )
                }
                physicalResults.put(JSONObject()
                    .put("logical_camera_id", logicalId)
                    .put("physical_camera_id", physicalId)
                    .put("status", physical.report.optString("status", "UNKNOWN"))
                    .put("dng_bytes", physical.dng?.size ?: 0))
            }
        }

        val summary = JSONObject()
            .put("generated_at_utc", Instant.now().toString())
            .put("logical_camera_results", logicalResults)
            .put("physical_camera_results", physicalResults)
        session.putJson("raw/summary.json", summary)
        return summary
    }

    fun runLaserAfProbe(session: ProbeSession, targetDistanceMm: Int?): JSONObject {
        val artifact = LaserAfProbe(context).captureTrace(targetDistanceMm)
        val label = targetDistanceMm?.let { "${it}mm" } ?: "baseline"
        session.putJson("laser_af/${label}_summary.json", artifact.summary)
        session.putText("laser_af/${label}_trace.ndjson", artifact.ndjson)
        return artifact.summary
    }

    fun runConcurrentCameraProbe(session: ProbeSession): JSONObject {
        val result = ConcurrentCameraProbe(context).run()
        session.putJson("camera/concurrency_matrix.json", result)
        return result
    }

    fun runImuCapture(session: ProbeSession, durationMs: Long = 5000L): JSONObject {
        val artifact = SensorProbe(context).recordImu(durationMs)
        session.putJson("sensors/imu_summary.json", artifact.summary)
        session.putText("sensors/imu_samples.ndjson", artifact.ndjson)
        return artifact.summary
    }

    fun runSafe(session: ProbeSession): JSONObject {
        val start = Instant.now().toString()
        val census = runStage(session, "01_census") { runCensus(session) }
        val camera = runStage(session, "02_logical_camera") {
            runCameraMetadataCapture(session, includePhysical = false)
        }
        val imu = runStage(session, "03_imu") { runImuCapture(session, 3000L) }

        val summary = JSONObject()
            .put("probe", "SAFE_DEVICE_PROBE")
            .put("started_at_utc", start)
            .put("finished_at_utc", Instant.now().toString())
            .put("census", census)
            .put("logical_camera_capture", camera)
            .put("imu", imu)
            .put("risky_physical_routes_included", false)
            .put("raw_probe_included", false)
            .put("depth_probe_included", false)
            .put("concurrency_probe_included", false)
            .put("laser_af_probe_included", false)
            .put("session", session.summary())

        session.putJson("safe_probe_result.json", summary)
        return summary
    }

    fun runFull(session: ProbeSession): JSONObject {
        val start = Instant.now().toString()
        val safe = runSafe(session)
        val physical = runStage(session, "04_physical_camera") {
            runCameraMetadataCapture(session, includePhysical = true)
        }
        val depth = runStage(session, "05_depth") { runDepthProbe(session) }
        val concurrency = runStage(session, "06_concurrency") { runConcurrentCameraProbe(session) }
        val laserAf = runStage(session, "07_laser_af") { runLaserAfProbe(session, null) }
        val raw = runStage(session, "08_raw") { runRawProbe(session) }

        val summary = JSONObject()
            .put("probe", "FULL_DEVICE_PROBE")
            .put("started_at_utc", start)
            .put("finished_at_utc", Instant.now().toString())
            .put("safe_probe", safe)
            .put("physical_camera_probe", physical)
            .put("depth_probe", depth)
            .put("camera_concurrency", concurrency)
            .put("laser_af_baseline", laserAf)
            .put("raw_probe", raw)
            .put("session", session.summary())

        session.putJson("full_probe_result.json", summary)
        return summary
    }
}
