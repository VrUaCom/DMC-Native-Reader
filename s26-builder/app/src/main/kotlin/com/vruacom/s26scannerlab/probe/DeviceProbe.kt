package com.vruacom.s26scannerlab.probe

import android.content.Context
import android.os.Build
import org.json.JSONObject
import java.time.Instant

class DeviceProbe(private val context: Context) {
    fun collect(): JSONObject {
        val pm = context.packageManager
        val pkg = pm.getPackageInfo(context.packageName, 0)

        return JSONObject()
            .put("generated_at_utc", Instant.now().toString())
            .put("manufacturer", Build.MANUFACTURER)
            .put("brand", Build.BRAND)
            .put("model", Build.MODEL)
            .put("device", Build.DEVICE)
            .put("product", Build.PRODUCT)
            .put("board", Build.BOARD)
            .put("hardware", Build.HARDWARE)
            .put("soc_manufacturer", Build.SOC_MANUFACTURER)
            .put("soc_model", Build.SOC_MODEL)
            .put("android_release", Build.VERSION.RELEASE)
            .put("sdk_int", Build.VERSION.SDK_INT)
            .put("security_patch", Build.VERSION.SECURITY_PATCH)
            .put("fingerprint", Build.FINGERPRINT)
            .put("supported_abis", JsonUtil.value(Build.SUPPORTED_ABIS))
            .put("app_version", pkg.versionName ?: "unknown")
            .put("features", JSONObject()
                .put("camera_any", pm.hasSystemFeature("android.hardware.camera.any"))
                .put("camera_flash", pm.hasSystemFeature("android.hardware.camera.flash"))
                .put("camera_raw", pm.hasSystemFeature("android.hardware.camera.raw"))
                .put("camera_level_full", pm.hasSystemFeature("android.hardware.camera.level.full"))
                .put("sensor_accelerometer", pm.hasSystemFeature("android.hardware.sensor.accelerometer"))
                .put("sensor_gyroscope", pm.hasSystemFeature("android.hardware.sensor.gyroscope"))
                .put("sensor_compass", pm.hasSystemFeature("android.hardware.sensor.compass")))
    }
}
