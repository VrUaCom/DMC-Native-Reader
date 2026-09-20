package com.vruacom.s26scannerlab.probe

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

data class SensorSampleArtifact(
    val summary: JSONObject,
    val ndjson: String
)

class SensorProbe(private val context: Context) {
    private val manager = context.getSystemService(SensorManager::class.java)

    fun collectCensus(): JSONObject {
        val sensors = JSONArray()
        manager.getSensorList(Sensor.TYPE_ALL)
            .sortedWith(compareBy<Sensor> { it.type }.thenBy { it.name })
            .forEach { sensor ->
                sensors.put(JSONObject()
                    .put("name", sensor.name)
                    .put("vendor", sensor.vendor)
                    .put("version", sensor.version)
                    .put("type", sensor.type)
                    .put("string_type", sensor.stringType)
                    .put("id", sensor.id)
                    .put("max_range", sensor.maximumRange.toDouble())
                    .put("resolution", sensor.resolution.toDouble())
                    .put("power_ma", sensor.power.toDouble())
                    .put("min_delay_us", sensor.minDelay)
                    .put("max_delay_us", sensor.maxDelay)
                    .put("fifo_reserved_event_count", sensor.fifoReservedEventCount)
                    .put("fifo_max_event_count", sensor.fifoMaxEventCount)
                    .put("reporting_mode", sensor.reportingMode)
                    .put("is_wakeup", sensor.isWakeUpSensor)
                    .put("is_dynamic", sensor.isDynamicSensor)
                    .put("highest_direct_report_rate_level", sensor.highestDirectReportRateLevel)
                    .put("additional_info_supported", sensor.isAdditionalInfoSupported))
            }

        return JSONObject()
            .put("generated_at_utc", Instant.now().toString())
            .put("sensor_count", sensors.length())
            .put("sensors", sensors)
    }

    fun recordImu(durationMs: Long = 5000L): SensorSampleArtifact {
        val interestingTypes = listOf(
            Sensor.TYPE_ACCELEROMETER,
            Sensor.TYPE_ACCELEROMETER_UNCALIBRATED,
            Sensor.TYPE_GYROSCOPE,
            Sensor.TYPE_GYROSCOPE_UNCALIBRATED,
            Sensor.TYPE_MAGNETIC_FIELD,
            Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED,
            Sensor.TYPE_GRAVITY,
            Sensor.TYPE_LINEAR_ACCELERATION,
            Sensor.TYPE_ROTATION_VECTOR,
            Sensor.TYPE_GAME_ROTATION_VECTOR
        )

        val sensors = interestingTypes.mapNotNull { manager.getDefaultSensor(it) }
        val thread = HandlerThread("s26-imu-probe")
        thread.start()
        val handler = Handler(thread.looper)
        val lines = StringBuilder()
        val total = AtomicInteger(0)
        val counts = ConcurrentHashMap<String, AtomicInteger>()
        val maxEvents = 30000

        val listener = object : SensorEventListener {
            override fun onSensorChanged(event: SensorEvent) {
                if (total.incrementAndGet() > maxEvents) return
                val key = event.sensor.stringType
                counts.computeIfAbsent(key) { AtomicInteger(0) }.incrementAndGet()
                val obj = JSONObject()
                    .put("sensor_type", event.sensor.type)
                    .put("sensor_string_type", key)
                    .put("sensor_name", event.sensor.name)
                    .put("sensor_timestamp_ns", event.timestamp)
                    .put("receive_elapsed_realtime_ns", SystemClock.elapsedRealtimeNanos())
                    .put("accuracy", event.accuracy)
                    .put("values", JsonUtil.value(event.values.copyOf()))
                synchronized(lines) {
                    lines.append(obj.toString()).append('\n')
                }
            }

            override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
        }

        val registered = JSONArray()
        sensors.forEach { sensor ->
            val ok = manager.registerListener(
                listener,
                sensor,
                0,
                0,
                handler
            )
            registered.put(JSONObject()
                .put("type", sensor.type)
                .put("string_type", sensor.stringType)
                .put("name", sensor.name)
                .put("registered", ok))
        }

        val startNs = SystemClock.elapsedRealtimeNanos()
        try {
            Thread.sleep(durationMs)
        } finally {
            manager.unregisterListener(listener)
            thread.quitSafely()
        }
        val endNs = SystemClock.elapsedRealtimeNanos()

        val countJson = JSONObject()
        counts.toSortedMap().forEach { (name, count) ->
            countJson.put(name, count.get())
        }

        val summary = JSONObject()
            .put("generated_at_utc", Instant.now().toString())
            .put("requested_duration_ms", durationMs)
            .put("actual_elapsed_ns", endNs - startNs)
            .put("registered_sensors", registered)
            .put("event_count", total.get().coerceAtMost(maxEvents))
            .put("event_cap", maxEvents)
            .put("counts_by_sensor", countJson)
            .put("timestamp_note",
                "sensor_timestamp_ns is Android sensor event timebase; receive_elapsed_realtime_ns is captured at callback receipt")

        return SensorSampleArtifact(summary, synchronized(lines) { lines.toString() })
    }
}
