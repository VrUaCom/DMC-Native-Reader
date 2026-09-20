package com.vruacom.s26scannerlab.probe

import android.content.Context
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import org.json.JSONObject

class FlashController(context: Context) {
    private val manager = context.getSystemService(CameraManager::class.java)
    private var activeCameraId: String? = null
    private var level = 0

    fun nextLevel(): JSONObject {
        val id = findFlashCamera()
            ?: return JSONObject().put("status", "NO_FLASH_CAMERA")

        val c = manager.getCameraCharacteristics(id)
        val max = c.get(CameraCharacteristics.FLASH_INFO_STRENGTH_MAXIMUM_LEVEL) ?: 1
        val defaultLevel = c.get(CameraCharacteristics.FLASH_INFO_STRENGTH_DEFAULT_LEVEL) ?: 1
        level = if (activeCameraId != id || level <= 0 || level >= max) 1 else level + 1

        if (max > 1) {
            manager.turnOnTorchWithStrengthLevel(id, level)
        } else {
            manager.setTorchMode(id, true)
            level = defaultLevel
        }
        activeCameraId = id

        return JSONObject()
            .put("status", "TORCH_ON")
            .put("camera_id", id)
            .put("level", level)
            .put("max_level", max)
            .put("default_level", defaultLevel)
    }

    fun off(): JSONObject {
        val id = activeCameraId ?: findFlashCamera()
            ?: return JSONObject().put("status", "NO_FLASH_CAMERA")
        manager.setTorchMode(id, false)
        activeCameraId = null
        level = 0
        return JSONObject().put("status", "TORCH_OFF").put("camera_id", id)
    }

    private fun findFlashCamera(): String? =
        manager.cameraIdList.firstOrNull { id ->
            manager.getCameraCharacteristics(id)
                .get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
        }
}
