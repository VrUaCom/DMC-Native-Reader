package com.vruacom.s26scannerlab

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import com.vruacom.s26scannerlab.export.BundleExporter
import com.vruacom.s26scannerlab.probe.FlashController
import com.vruacom.s26scannerlab.probe.LaserAfProbe
import com.vruacom.s26scannerlab.probe.ProbeRunner
import com.vruacom.s26scannerlab.probe.ProbeSession
import com.vruacom.s26scannerlab.probe.SpectralProbe
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.concurrent.Executors

class MainActivity : Activity() {
    private val worker = Executors.newSingleThreadExecutor()
    private lateinit var session: ProbeSession
    private lateinit var status: TextView
    private lateinit var log: TextView
    private lateinit var flash: FlashController
    private var busy = false
    private var spectralIndex = 0
    private var laserDistanceIndex = 0

    companion object {
        private const val CAMERA_PERMISSION = 100
        private const val EXPORT_REQUEST = 200
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        CrashReporter.install(this)
        session = ProbeSession(cacheDir)
        CrashReporter.consumePreviousCrash(this)?.let { previous ->
            session.putText("crash/previous_crash.txt", previous)
        }
        flash = FlashController(this)
        buildUi()

        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION)
        }
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 48)
        }

        root.addView(TextView(this).apply {
            text = "S26 Scanner Lab — v0.1"
            textSize = 24f
            setTypeface(typeface, Typeface.BOLD)
        })

        root.addView(TextView(this).apply {
            text = "Start with SAFE DEVICE PROBE. Risky RAW/physical/depth/concurrency tests are separated to reduce camera HAL crashes."
            textSize = 14f
            setPadding(0, 12, 0, 20)
        })

        status = TextView(this).apply {
            text = "Ready"
            textSize = 16f
            setPadding(0, 0, 0, 20)
        }
        root.addView(status)

        if (session.snapshot().any { it.path == "crash/previous_crash.txt" }) {
            status.text = "Recovered previous crash report — export bundle when convenient"
        }

        root.addView(button("RUN SAFE DEVICE PROBE") {
            requireCameraThen {
                runTask("Safe device probe") {
                    ProbeRunner(this).runSafe(session).toString(2)
                }
            }
        })

        root.addView(button("RUN FULL DEVICE PROBE + RAW") {
            requireCameraThen {
                runTask("Full probe + RAW") {
                    ProbeRunner(this).runFull(session).toString(2)
                }
            }
        })

        root.addView(button("Hardware census only") {
            runTask("Hardware census") {
                ProbeRunner(this).runCensus(session).toString(2)
            }
        })

        root.addView(button("Camera metadata + JPEG samples") {
            requireCameraThen {
                runTask("Camera metadata capture") {
                    ProbeRunner(this).runCameraMetadataCapture(session).toString(2)
                }
            }
        })

        root.addView(button("RAW / DNG camera probe") {
            requireCameraThen {
                runTask("RAW / DNG probe") {
                    ProbeRunner(this).runRawProbe(session).toString(2)
                }
            }
        })

        root.addView(button("DEPTH hardware probe") {
            requireCameraThen {
                runTask("Depth hardware probe") {
                    ProbeRunner(this).runDepthProbe(session).toString(2)
                }
            }
        })

        root.addView(button("BASIC 3D SCAN DATASET — 16 FRAMES") {
            requireCameraThen {
                runTask("Basic scan dataset") {
                    ProbeRunner(this).runBasicScan(session).toString(2)
                }
            }
        })

        root.addView(button("Laser/AF — select next target distance") {
            val laser = LaserAfProbe(this)
            laserDistanceIndex = (laserDistanceIndex + 1) % laser.targetDistanceMmPresets.size
            val distance = laser.targetDistanceMmPresets[laserDistanceIndex]
            appendLog("Laser/AF target label selected: ${distance} mm. Place/measure a target at approximately this distance, then press CAPTURE LASER/AF TRACE.")
            status.text = "Laser/AF target: ${distance} mm"
        })

        root.addView(button("CAPTURE LASER/AF TRACE") {
            requireCameraThen {
                val laser = LaserAfProbe(this)
                val distance = laser.targetDistanceMmPresets[laserDistanceIndex]
                runTask("Laser/AF trace ${distance} mm") {
                    ProbeRunner(this).runLaserAfProbe(session, distance).toString(2)
                }
            }
        })

        root.addView(button("Concurrent camera matrix") {
            requireCameraThen {
                runTask("Concurrent camera matrix") {
                    ProbeRunner(this).runConcurrentCameraProbe(session).toString(2)
                }
            }
        })

        root.addView(button("Spectral — select next external LED preset") {
            val spectral = SpectralProbe(this)
            spectralIndex = (spectralIndex + 1) % spectral.presets.size
            val preset = spectral.presets[spectralIndex]
            appendLog("Selected ${preset.label} (${preset.wavelengthNm} nm). Set the external narrow-band source, then press CAPTURE CURRENT SPECTRAL PRESET.")
            status.text = "Spectral preset: ${preset.label}"
        })

        root.addView(button("CAPTURE CURRENT SPECTRAL PRESET") {
            requireCameraThen {
                val spectral = SpectralProbe(this)
                val preset = spectral.presets[spectralIndex]
                runTask("Spectral ${preset.wavelengthNm} nm") {
                    spectral.capturePreset(session, preset).toString(2)
                }
            }
        })

        root.addView(button("Record IMU — 5 seconds") {
            runTask("IMU capture") {
                ProbeRunner(this).runImuCapture(session, 5000L).toString(2)
            }
        })

        root.addView(button("SINGLE FLASH strength capture probe") {
            requireCameraThen {
                runTask("Single flash capture probe") {
                    ProbeRunner(this).runFlashCaptureProbe(session).toString(2)
                }
            }
        })

        root.addView(button("Torch — next hardware level") {
            runTask("Torch level") {
                val event = flash.nextLevel().put("event_utc", Instant.now().toString())
                session.appendText("flash/flash_events.ndjson", event.toString() + "\n")
                event.toString(2)
            }
        })

        root.addView(button("Torch OFF") {
            runTask("Torch off") {
                val event = flash.off().put("event_utc", Instant.now().toString())
                session.appendText("flash/flash_events.ndjson", event.toString() + "\n")
                event.toString(2)
            }
        })

        root.addView(button("EXPORT LAB BUNDLE (.zip)") {
            if (session.size() == 0) {
                appendLog("Nothing to export. Run at least one probe first.")
            } else {
                launchExport()
            }
        })

        root.addView(button("Clear current session") {
            session.clear()
            status.text = "Session cleared"
            log.text = ""
        })

        log = TextView(this).apply {
            textSize = 12f
            typeface = Typeface.MONOSPACE
            setPadding(0, 24, 0, 0)
            setTextIsSelectable(true)
        }
        root.addView(log)

        setContentView(ScrollView(this).apply {
            addView(root, ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ))
        })
    }

    private fun button(label: String, action: () -> Unit): Button =
        Button(this).apply {
            text = label
            setOnClickListener { action() }
        }

    private fun runTask(label: String, task: () -> String) {
        if (busy) {
            appendLog("Busy: another probe is still running.")
            return
        }
        busy = true
        status.text = "$label — running…"
        appendLog("[${Instant.now()}] START $label")

        worker.execute {
            val result = try {
                task()
            } catch (t: Throwable) {
                "ERROR ${t.javaClass.name}: ${t.message}"
            }
            runOnUiThread {
                busy = false
                status.text = "$label — finished | artifacts=${session.size()}"
                appendLog(result)
                appendLog("[${Instant.now()}] END $label")
            }
        }
    }

    private fun requireCameraThen(action: () -> Unit) {
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            action()
        } else {
            appendLog("Camera permission is required. Grant it, then press the probe button again.")
            requestPermissions(arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION)
        }
    }

    private fun launchExport() {
        val stamp = DateTimeFormatter.ofPattern("yyyyMMdd_HHmmss")
            .withZone(ZoneOffset.UTC)
            .format(Instant.now())
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/zip"
            putExtra(Intent.EXTRA_TITLE, "s26_scanner_lab_$stamp.zip")
        }
        startActivityForResult(intent, EXPORT_REQUEST)
    }

    @Deprecated("Legacy result API is intentionally used to avoid external AndroidX dependencies in v0.1.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != EXPORT_REQUEST || resultCode != RESULT_OK) return
        val uri: Uri = data?.data ?: return

        runTask("Export bundle") {
            contentResolver.openOutputStream(uri, "w")!!.use { out ->
                BundleExporter.write(session, out).toString(2)
            }
        }
    }

    private fun appendLog(message: String) {
        val maxIncoming = 6_000
        val maxTotal = 24_000
        val clipped = if (message.length > maxIncoming) {
            message.take(maxIncoming) + "\n…[UI log clipped; full data is preserved in the export bundle]"
        } else {
            message
        }

        val current = log.text?.toString().orEmpty()
        val combined = buildString {
            append(current)
            append(clipped)
            if (!clipped.endsWith("\n")) append('\n')
        }

        log.text = if (combined.length > maxTotal) {
            "…[older UI log clipped]\n" + combined.takeLast(maxTotal)
        } else {
            combined
        }
    }

    override fun onDestroy() {
        try { flash.off() } catch (_: Throwable) {}
        worker.shutdownNow()
        super.onDestroy()
    }
}
