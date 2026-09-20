package com.vruacom.s26scannerlab.probe

import android.content.Context
import android.hardware.camera2.CameraManager
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant

data class SpectralPreset(val wavelengthNm: Int, val label: String)

class SpectralProbe(private val context: Context) {
    val presets: List<SpectralPreset> = listOf(
        SpectralPreset(365, "UV-A 365 nm"),
        SpectralPreset(405, "Violet 405 nm"),
        SpectralPreset(450, "Blue 450 nm"),
        SpectralPreset(470, "Blue 470 nm"),
        SpectralPreset(525, "Green 525 nm"),
        SpectralPreset(590, "Amber 590 nm"),
        SpectralPreset(625, "Red 625 nm"),
        SpectralPreset(660, "Deep red 660 nm"),
        SpectralPreset(730, "Far red 730 nm"),
        SpectralPreset(850, "Near-IR 850 nm"),
        SpectralPreset(940, "Near-IR 940 nm")
    )

    fun presetsJson(): JSONObject = JSONObject()
        .put("note",
            "These are experiment labels for an external narrow-band light source. The phone LED cannot select these wavelengths.")
        .put("presets", JSONArray().also { array ->
            presets.forEach { p ->
                array.put(JSONObject().put("wavelength_nm", p.wavelengthNm).put("label", p.label))
            }
        })

    fun capturePreset(session: ProbeSession, preset: SpectralPreset): JSONObject {
        val cameraManager = context.getSystemService(CameraManager::class.java)
        val probe = CameraProbe(context)
        val logicalResults = JSONArray()
        val physicalResults = JSONArray()
        val rawLogicalResults = JSONArray()
        val rawPhysicalResults = JSONArray()
        val rawProbe = RawProbe(context)
        val base = "spectral/${preset.wavelengthNm}nm"

        cameraManager.cameraIdList.forEach { logicalId ->
            val safeLogical = safe(logicalId)
            val logical = probe.captureOneLogicalCamera(logicalId)
            session.putJson("$base/logical_${safeLogical}_metadata.json", logical.report)
            logical.jpeg?.let { session.putBytes("$base/logical_${safeLogical}.jpg", it) }
            logicalResults.put(JSONObject()
                .put("camera_id", logicalId)
                .put("status", logical.report.optString("status", "UNKNOWN"))
                .put("jpeg_bytes", logical.jpeg?.size ?: 0))

            val rawLogical = rawProbe.captureLogical(logicalId)
            session.putJson("$base/raw_logical_${safeLogical}_report.json", rawLogical.report)
            rawLogical.dng?.let {
                session.putBytes("$base/raw_logical_${safeLogical}.dng", it)
            }
            rawLogicalResults.put(JSONObject()
                .put("camera_id", logicalId)
                .put("status", rawLogical.report.optString("status", "UNKNOWN"))
                .put("dng_bytes", rawLogical.dng?.size ?: 0))

            val c = cameraManager.getCameraCharacteristics(logicalId)
            c.physicalCameraIds.toList().sorted().forEach { physicalId ->
                val safePhysical = safe(physicalId)
                val physical = probe.captureOnePhysicalCamera(logicalId, physicalId)
                session.putJson(
                    "$base/logical_${safeLogical}_physical_${safePhysical}_metadata.json",
                    physical.report
                )
                physical.jpeg?.let {
                    session.putBytes(
                        "$base/logical_${safeLogical}_physical_${safePhysical}.jpg",
                        it
                    )
                }
                physicalResults.put(JSONObject()
                    .put("logical_camera_id", logicalId)
                    .put("physical_camera_id", physicalId)
                    .put("status", physical.report.optString("status", "UNKNOWN"))
                    .put("jpeg_bytes", physical.jpeg?.size ?: 0))

                val rawPhysical = rawProbe.capturePhysical(logicalId, physicalId)
                session.putJson(
                    "$base/raw_logical_${safeLogical}_physical_${safePhysical}_report.json",
                    rawPhysical.report
                )
                rawPhysical.dng?.let {
                    session.putBytes(
                        "$base/raw_logical_${safeLogical}_physical_${safePhysical}.dng",
                        it
                    )
                }
                rawPhysicalResults.put(JSONObject()
                    .put("logical_camera_id", logicalId)
                    .put("physical_camera_id", physicalId)
                    .put("status", rawPhysical.report.optString("status", "UNKNOWN"))
                    .put("dng_bytes", rawPhysical.dng?.size ?: 0))
            }
        }

        val summary = JSONObject()
            .put("generated_at_utc", Instant.now().toString())
            .put("wavelength_nm", preset.wavelengthNm)
            .put("label", preset.label)
            .put("external_illumination_required", true)
            .put("logical_camera_results", logicalResults)
            .put("physical_camera_results", physicalResults)
            .put("raw_logical_camera_results", rawLogicalResults)
            .put("raw_physical_camera_results", rawPhysicalResults)

        session.putJson("$base/summary.json", summary)
        session.putJson("spectral/presets.json", presetsJson())
        return summary
    }

    private fun safe(id: String): String = id.replace(Regex("[^A-Za-z0-9_.-]"), "_")
}
