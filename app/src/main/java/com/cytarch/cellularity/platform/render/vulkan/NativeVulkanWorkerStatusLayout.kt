package com.cytarch.cellularity.platform.render.vulkan

import com.cytarch.cellularity.platform.render.RenderFrameResultCodec
import java.nio.ByteBuffer
import java.nio.ByteOrder

internal enum class VulkanClearFramePhase {
    STARTING,
    WAITING_FOR_SURFACE,
    READY,
    PAUSED,
    SURFACE_LOST,
    FAILED,
    STOPPED
}

internal enum class VulkanRuntimeFailureCode(val code: Int) {
    NONE(0),
    STARTUP(1),
    SURFACE_ATTACH(2),
    FRAME_TRANSACTION(3),
    WORKER_CONTRACT(4)
}

internal enum class VulkanBackendAbiStatus {
    NOT_CHECKED,
    READY,
    UNAVAILABLE,
    REJECTED,
    FAILED
}

internal data class VulkanClearFrameStatus(
    val phase: VulkanClearFramePhase = VulkanClearFramePhase.STARTING,
    val terminalFailure: Boolean = false,
    val frameOutcome: Int = -1,
    val presentedFrames: Long = 0,
    val swapchainRecreations: Long = 0,
    val failureCode: VulkanRuntimeFailureCode = VulkanRuntimeFailureCode.NONE,
    val diagnosticsRevision: Long = 0,
    val backendAbiVersion: Int = 1,
    val backendApiRequiredSize: Int = 0,
    val backendAbiStatus: VulkanBackendAbiStatus = VulkanBackendAbiStatus.NOT_CHECKED,
    val error: String = ""
)

internal data class NativeVulkanWorkerStatus(
    val revision: Long,
    val phase: Phase,
    val lastFrameOutcome: Int?,
    val flags: Int,
    val failure: Failure,
    val presentedFrames: Long,
    val swapchainRecreations: Long,
    val failedFrameAttempts: Long,
    val desiredSurfaceGeneration: Long,
    val appliedSurfaceGeneration: Long,
    val diagnosticsRevision: Long,
    val desiredWidth: Int,
    val desiredHeight: Int
) {
    enum class Phase(val wireValue: Int) {
        DORMANT(0),
        STARTING(1),
        WAITING_FOR_SURFACE(2),
        PAUSED(3),
        READY(4),
        SURFACE_LOST(5),
        RECOVERABLE_FAILURE(6),
        TERMINAL_FAILURE(7),
        STOP_REQUESTED(8),
        STOPPED(9)
    }

    enum class Failure(val wireValue: Int) {
        NONE(0),
        API_UNAVAILABLE(1),
        ABI_MISMATCH(2),
        ASSET_UNAVAILABLE(3),
        THREAD_START_FAILED(4),
        SESSION_CREATE_FAILED(5),
        SURFACE_ATTACH_FAILED(6),
        SURFACE_RESIZE_FAILED(7),
        SOURCE_SURFACE_FAILED(8),
        FRAME_FAILED(9),
        SOURCE_FRAME_FAILED(10),
        BACKEND_CONTROL_FAILED(11),
        DIAGNOSTICS_FAILED(12),
        INVARIANT_VIOLATION(13),
        WORKER_OUT_OF_MEMORY(14),
        WORKER_UNHANDLED_EXCEPTION(15),
        STATUS_SIGNAL_TRANSPORT_FAILED(16)
    }

    fun toLegacyStatus(error: String = ""): VulkanClearFrameStatus {
        val legacyPhase = when (phase) {
            Phase.DORMANT,
            Phase.STARTING -> VulkanClearFramePhase.STARTING
            Phase.WAITING_FOR_SURFACE -> VulkanClearFramePhase.WAITING_FOR_SURFACE
            Phase.PAUSED -> VulkanClearFramePhase.PAUSED
            Phase.READY -> VulkanClearFramePhase.READY
            Phase.SURFACE_LOST -> VulkanClearFramePhase.SURFACE_LOST
            Phase.RECOVERABLE_FAILURE,
            Phase.TERMINAL_FAILURE -> VulkanClearFramePhase.FAILED
            Phase.STOP_REQUESTED,
            Phase.STOPPED -> VulkanClearFramePhase.STOPPED
        }
        val legacyFailure = when (failure) {
            Failure.NONE -> VulkanRuntimeFailureCode.NONE
            Failure.SURFACE_ATTACH_FAILED,
            Failure.SURFACE_RESIZE_FAILED -> VulkanRuntimeFailureCode.SURFACE_ATTACH
            Failure.FRAME_FAILED -> VulkanRuntimeFailureCode.FRAME_TRANSACTION
            Failure.API_UNAVAILABLE,
            Failure.ABI_MISMATCH,
            Failure.ASSET_UNAVAILABLE,
            Failure.THREAD_START_FAILED,
            Failure.SESSION_CREATE_FAILED -> VulkanRuntimeFailureCode.STARTUP
            else -> VulkanRuntimeFailureCode.WORKER_CONTRACT
        }
        return VulkanClearFrameStatus(
            phase = legacyPhase,
            terminalFailure = phase == Phase.TERMINAL_FAILURE,
            frameOutcome = lastFrameOutcome ?: -1,
            presentedFrames = presentedFrames,
            swapchainRecreations = swapchainRecreations,
            failureCode = legacyFailure,
            diagnosticsRevision = diagnosticsRevision,
            backendAbiVersion = 1,
            backendApiRequiredSize = 0,
            backendAbiStatus = when (phase) {
                Phase.DORMANT,
                Phase.STARTING -> VulkanBackendAbiStatus.NOT_CHECKED
                else -> VulkanBackendAbiStatus.READY
            },
            error = error
        )
    }
}

internal object NativeVulkanWorkerStatusLayout {
    const val SIZE = 96
    private const val MAGIC = 0x31535756
    private const val VERSION = 1
    private const val HAS_FRAME_OUTCOME = 1 shl 6
    private const val KNOWN_FLAGS = (1 shl 9) - 1

    fun allocateBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(SIZE).order(ByteOrder.LITTLE_ENDIAN)

    fun decode(source: ByteBuffer): NativeVulkanWorkerStatus {
        val buffer = source.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        require(buffer.capacity() >= SIZE) { "VWS1 buffer is too short" }
        require(buffer.getInt(0) == MAGIC) { "VWS1 magic is invalid" }
        require(buffer.getShort(4).toInt() and 0xffff == VERSION) {
            "VWS1 version is unsupported"
        }
        require(buffer.getShort(6).toInt() and 0xffff == SIZE) {
            "VWS1 size is invalid"
        }
        val revision = buffer.getLong(8)
        require(revision != 0L) { "VWS1 revision is zero" }
        val phaseValue = buffer.getInt(16)
        val phase = NativeVulkanWorkerStatus.Phase.entries.singleOrNull {
            it.wireValue == phaseValue
        } ?: error("VWS1 phase is invalid: $phaseValue")
        val outcome = buffer.getInt(20)
        val flags = buffer.getInt(24)
        require(flags and KNOWN_FLAGS.inv() == 0) { "VWS1 flags are invalid" }
        val failureValue = buffer.getInt(28)
        val failure = NativeVulkanWorkerStatus.Failure.entries.singleOrNull {
            it.wireValue == failureValue
        } ?: error("VWS1 failure is invalid: $failureValue")
        val decodedOutcome = if (flags and HAS_FRAME_OUTCOME != 0) {
            require(
                outcome in RenderFrameResultCodec.OUTCOME_PRESENTED..RenderFrameResultCodec.OUTCOME_FAILED
            ) { "VWS1 frame outcome is invalid: $outcome" }
            outcome
        } else {
            null
        }
        require(buffer.getLong(88) == 0L) { "VWS1 reserved bytes are nonzero" }
        return NativeVulkanWorkerStatus(
            revision = revision,
            phase = phase,
            lastFrameOutcome = decodedOutcome,
            flags = flags,
            failure = failure,
            presentedFrames = buffer.getLong(32),
            swapchainRecreations = buffer.getLong(40),
            failedFrameAttempts = buffer.getLong(48),
            desiredSurfaceGeneration = buffer.getLong(56),
            appliedSurfaceGeneration = buffer.getLong(64),
            diagnosticsRevision = buffer.getLong(72),
            desiredWidth = buffer.getInt(80),
            desiredHeight = buffer.getInt(84)
        )
    }
}
