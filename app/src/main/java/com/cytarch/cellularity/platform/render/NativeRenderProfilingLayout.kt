package com.cytarch.cellularity.platform.render

import java.nio.ByteBuffer
import java.nio.ByteOrder

internal data class NativeRenderProfilingDistribution(
    val sampleCount: Long,
    val p50Nanos: Long,
    val p95Nanos: Long,
    val p99Nanos: Long,
    val maximumNanos: Long
)

internal data class NativeRenderProfilingSnapshot(
    val resetGeneration: Long,
    val enabled: Boolean,
    val callbackCount: Long,
    val presentedCount: Long,
    val deferredCount: Long,
    val failedCount: Long,
    val elapsedNanos: Long,
    val cpuFrame: NativeRenderProfilingDistribution,
    val frameInterval: NativeRenderProfilingDistribution,
    val cameraLatency: NativeRenderProfilingDistribution
)

internal object NativeRenderProfilingLayout {
    const val SIZE = 184
    const val RESULT_READY = 0
    const val RESULT_INVALID_BUFFER = 1
    const val RESULT_UNAVAILABLE = 2
    const val PUBLISH_APPLIED = 0
    const val PUBLISH_DUPLICATE = 1

    private const val MAGIC = 0x31505256
    private const val VERSION = 1

    fun allocateBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(SIZE).order(ByteOrder.LITTLE_ENDIAN)

    fun decode(source: ByteBuffer): NativeRenderProfilingSnapshot {
        val buffer = source.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        require(buffer.capacity() >= SIZE) { "VRP1 buffer is too short" }
        require(buffer.getInt(0) == MAGIC) { "VRP1 magic is invalid" }
        require(buffer.getShort(4).toInt() and 0xffff == VERSION) {
            "VRP1 version is unsupported"
        }
        require(buffer.getShort(6).toInt() and 0xffff == SIZE) {
            "VRP1 size is invalid"
        }
        val generation = buffer.getLong(8)
        require(generation >= 0L) { "VRP1 generation is invalid" }
        val enabledValue = buffer.getInt(16)
        require(enabledValue == 0 || enabledValue == 1) {
            "VRP1 enabled flag is invalid"
        }
        require(buffer.getInt(20) == 0) { "VRP1 reserved field is nonzero" }
        return NativeRenderProfilingSnapshot(
            resetGeneration = generation,
            enabled = enabledValue == 1,
            callbackCount = buffer.getLong(24),
            presentedCount = buffer.getLong(32),
            deferredCount = buffer.getLong(40),
            failedCount = buffer.getLong(48),
            elapsedNanos = buffer.getLong(56),
            cpuFrame = decodeDistribution(buffer, 64),
            frameInterval = decodeDistribution(buffer, 104),
            cameraLatency = decodeDistribution(buffer, 144)
        )
    }

    private fun decodeDistribution(
        buffer: ByteBuffer,
        offset: Int
    ): NativeRenderProfilingDistribution = NativeRenderProfilingDistribution(
        sampleCount = buffer.getLong(offset),
        p50Nanos = buffer.getLong(offset + 8),
        p95Nanos = buffer.getLong(offset + 16),
        p99Nanos = buffer.getLong(offset + 24),
        maximumNanos = buffer.getLong(offset + 32)
    )
}
