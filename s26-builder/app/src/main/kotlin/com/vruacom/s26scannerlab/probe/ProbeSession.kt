package com.vruacom.s26scannerlab.probe

import org.json.JSONObject
import java.io.File
import java.nio.charset.StandardCharsets
import java.time.Instant
import java.util.LinkedHashMap

data class ProbeArtifactRef(
    val path: String,
    val file: File
)

class ProbeSession(cacheDir: File) {
    private val root = File(cacheDir, "s26-scanner-lab-session").apply {
        mkdirs()
    }
    private val artifacts = LinkedHashMap<String, File>()

    init {
        root.mkdirs()
        indexExistingArtifacts()
    }

    @Synchronized
    fun putText(path: String, text: String) {
        writeFile(path, text.toByteArray(StandardCharsets.UTF_8), append = false)
    }

    @Synchronized
    fun putJson(path: String, json: JSONObject) {
        putText(path, json.toString(2))
    }

    @Synchronized
    fun appendText(path: String, text: String) {
        writeFile(path, text.toByteArray(StandardCharsets.UTF_8), append = true)
    }

    @Synchronized
    fun putBytes(path: String, bytes: ByteArray) {
        writeFile(path, bytes, append = false)
    }

    @Synchronized
    fun snapshot(): List<ProbeArtifactRef> =
        artifacts.entries.map { (path, file) -> ProbeArtifactRef(path, file) }

    @Synchronized
    fun clear() {
        if (root.exists()) {
            root.listFiles()?.forEach { child -> child.deleteRecursively() }
        }
        root.mkdirs()
        artifacts.clear()
    }

    @Synchronized
    fun size(): Int = artifacts.size

    @Synchronized
    fun summary(): JSONObject = JSONObject()
        .put("artifact_count", artifacts.size)
        .put("artifact_names", JsonUtil.array(artifacts.keys))
        .put("total_bytes", artifacts.values.sumOf { if (it.exists()) it.length() else 0L })
        .put("updated_at_utc", Instant.now().toString())

    private fun indexExistingArtifacts() {
        if (!root.exists()) return
        root.walkTopDown()
            .filter { it.isFile }
            .forEach { file ->
                val relative = file.relativeTo(root).invariantSeparatorsPath
                artifacts[relative] = file
            }
    }

    private fun writeFile(path: String, bytes: ByteArray, append: Boolean) {
        require(path.isNotBlank()) { "Artifact path must not be blank" }
        require(!path.startsWith("/")) { "Artifact path must be relative" }
        require(!path.split('/').contains("..")) { "Artifact path must not escape session root" }

        val file = File(root, path)
        file.parentFile?.mkdirs()

        if (append) {
            file.appendBytes(bytes)
        } else {
            file.writeBytes(bytes)
        }
        artifacts[path] = file
    }
}
