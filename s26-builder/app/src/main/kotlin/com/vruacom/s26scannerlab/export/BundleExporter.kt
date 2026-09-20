package com.vruacom.s26scannerlab.export

import com.vruacom.s26scannerlab.probe.ProbeArtifactRef
import com.vruacom.s26scannerlab.probe.ProbeSession
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.OutputStream
import java.security.MessageDigest
import java.time.Instant
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

object BundleExporter {
    fun write(session: ProbeSession, output: OutputStream): JSONObject {
        val snapshot = session.snapshot()
        val files = JSONArray()

        snapshot.forEach { artifact ->
            files.put(JSONObject()
                .put("path", artifact.path)
                .put("size_bytes", artifact.file.length())
                .put("sha256", sha256(artifact.file)))
        }

        val manifest = JSONObject()
            .put("format", "s26-scanner-lab-bundle")
            .put("format_version", 1)
            .put("generated_at_utc", Instant.now().toString())
            .put("artifact_count", snapshot.size)
            .put("files", files)

        ZipOutputStream(output.buffered()).use { zip ->
            zip.putNextEntry(ZipEntry("manifest.json"))
            zip.write(manifest.toString(2).toByteArray(Charsets.UTF_8))
            zip.closeEntry()

            snapshot.forEach { artifact ->
                zip.putNextEntry(ZipEntry(artifact.path))
                artifact.file.inputStream().buffered().use { input ->
                    input.copyTo(zip, bufferSize = 256 * 1024)
                }
                zip.closeEntry()
            }
        }
        return manifest
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(256 * 1024)
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                if (read > 0) digest.update(buffer, 0, read)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }
}
