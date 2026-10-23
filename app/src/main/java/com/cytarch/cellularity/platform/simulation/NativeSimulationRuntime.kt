package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.NativeSimulationFacade
import com.cytarch.cellularity.CellularityDataException
import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.files.SubstratePreviewEncoder
import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.platform.render.microscope.SubstratePreviewCaptureCoordinator
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.SimulationStats
import com.cytarch.cellularity.shared.simulation.ApplyResult
import com.cytarch.cellularity.shared.simulation.CellId
import com.cytarch.cellularity.shared.simulation.EnvironmentPatch
import com.cytarch.cellularity.shared.simulation.SaveResult
import com.cytarch.cellularity.shared.simulation.SimulationCommand
import com.cytarch.cellularity.shared.simulation.SimulationResult
import java.io.FileNotFoundException
import java.io.IOException

internal class NativeSimulationRuntime(
    private val simulation: NativeSimulationFacade,
    private val files: AndroidCellularityFileRepository,
    private val initialTarget: StoredSubstrateId?,
    private val substratePreviewCapture: SubstratePreviewCaptureCoordinator
) : AndroidSimulationRuntime {
    private var opened = false
    private var replacementGeneration = 1L
    private var detached = false

    private class PreparedNativeReplacement(
        override val target: StoredSubstrateId,
        val expectedGeneration: Long,
        var delegate: NativeSimulationFacade.PreparedControllerReplacement?
    ) : PreparedRuntimeReplacement {
        override fun close() {
            val current = synchronized(this) {
                delegate.also { delegate = null }
            }
            current?.close()
        }
    }

    private class RetiredNativeRuntime(
        private var delegate: NativeSimulationFacade.RetiredController?
    ) : RetiredRuntime {
        fun initialize(retired: NativeSimulationFacade.RetiredController) {
            synchronized(this) {
                check(delegate == null) { "Retired runtime was already initialized" }
                delegate = retired
            }
        }

        override fun close() {
            val current = synchronized(this) {
                delegate.also { delegate = null }
            }
            current?.close()
        }
    }

    override fun open() {
        check(!opened) { "Simulation runtime has already been opened" }
        opened = true
        if (initialTarget != null) {
            simulation.loadSubstratePath(
                files.substratePathOnSimulationWorker(initialTarget).value
            )
        }
        simulation.startMicroscopeRuntime()
    }

    override fun environment(): EnvironmentSnapshot =
        EnvironmentBatchCodec.decodeSnapshot(simulation.environmentStateBytes())

    override fun stats(): SimulationStats = simulation.stats()

    override fun execute(command: SimulationCommand): SimulationResult = try {
        when (command) {
            SimulationCommand.Reset -> {
                simulation.reset()
                SimulationResult.Queued
            }
            is SimulationCommand.Reload -> error(
                "Reload must use the two-phase replacement path"
            )
        }
    } catch (failure: Throwable) {
        SimulationResult.Failure(failure.toCellularityError())
    }

    override fun prepareReplacement(
        command: SimulationCommand.Reload
    ): PreparedRuntimeReplacement {
        check(opened && !detached) { "Simulation runtime is not available" }
        val path = files.substratePathOnSimulationWorker(command.source).value
        return PreparedNativeReplacement(
            target = command.source,
            expectedGeneration = replacementGeneration,
            delegate = simulation.prepareSubstrateReplacement(path)
        )
    }

    override fun commitReplacement(
        prepared: PreparedRuntimeReplacement,
        expectedGeneration: Long
    ): RuntimeReplacementCommit {
        check(opened && !detached) { "Simulation runtime is not available" }
        val nativePrepared = prepared as? PreparedNativeReplacement
            ?: throw IllegalArgumentException(
                "Prepared replacement belongs to another runtime"
            )
        check(
            expectedGeneration == replacementGeneration &&
                nativePrepared.expectedGeneration == replacementGeneration
        ) { "Prepared replacement generation is stale" }
        val nextGeneration = replacementGeneration.incrementGeneration()
        val retiredOwner = RetiredNativeRuntime(null)
        val committed = RuntimeReplacementCommit(retiredOwner)
        val delegate = synchronized(nativePrepared) {
            nativePrepared.delegate.also { nativePrepared.delegate = null }
        } ?: throw IllegalStateException("Prepared replacement was already consumed")

        try {
            val retired = simulation.commitSubstrateReplacement(delegate)
            retiredOwner.initialize(retired)
            replacementGeneration = nextGeneration
            return committed
        } finally {
            delegate.close()
        }
    }

    override fun applyEnvironment(patch: EnvironmentPatch): ApplyResult = try {
        EnvironmentBatchCodec.decodeApplyResult(
            bytes = simulation.applyEnvironmentPatch(EnvironmentBatchCodec.encodePatch(patch)),
            patch = patch
        )
    } catch (failure: Throwable) {
        ApplyResult.Failure(failure.toCellularityError())
    }

    override fun inspectCell(id: CellId): CellSnapshot? =
        NativeWireCodec.decodeCellSnapshot(simulation.cellSnapshotBytes(id.value))

    override fun save(target: StoredSubstrateId): SaveResult = try {
        files.saveSubstrateAtomicallyOnSimulationWorker(target) { temporaryPath ->
            val previewToken = simulation.writeSubstratePath(temporaryPath.value)
            try {
                val rendered = substratePreviewCapture.capture(previewToken)
                val webp = if (rendered == null) {
                    null
                } else try {
                    SubstratePreviewEncoder.encodeWebpLossy(
                        rgbaBytes = requireNotNull(rendered.payload),
                        pixelWidth = rendered.width,
                        pixelHeight = rendered.height,
                        rowStrideBytes = rendered.rowStrideBytes
                    )
                } catch (failure: Exception) {
                    AppLog.error("Could not encode the rendered substrate preview", failure)
                    null
                }
                if (webp != null) {
                    simulation.attachSubstratePreviewPath(temporaryPath.value, webp)
                }
            } finally {
                simulation.discardSubstratePreviewFrame(previewToken)
            }
        }
        SaveResult.Saved(target)
    } catch (failure: Throwable) {
        SaveResult.Failure(failure.toCellularityError())
    }

    override fun detachForClose(): RetiredRuntime {
        if (detached) return RetiredNativeRuntime(null)
        val retiredOwner = RetiredNativeRuntime(null)
        detached = true
        val retired = simulation.detachForClose()
        return try {
            retiredOwner.initialize(retired)
            retiredOwner
        } catch (failure: Throwable) {
            try {
                retired.close()
            } catch (cleanupFailure: Throwable) {
                failure.addSuppressed(cleanupFailure)
            }
            throw failure
        }
    }
}

private fun Long.incrementGeneration(): Long {
    check(this > 0L && this != Long.MAX_VALUE) {
        "Simulation runtime generation is exhausted"
    }
    return this + 1L
}

internal fun Throwable.toCellularityError(): CellularityError = when (this) {
    is CellularityDataException -> CellularityError(code, message.orEmpty())
    is OutOfMemoryError -> CellularityError(CellularityErrorCode.OUT_OF_MEMORY, message.orEmpty())
    is FileNotFoundException,
    is IOException -> CellularityError(CellularityErrorCode.IO_FAILURE, message.orEmpty())
    is IllegalArgumentException -> CellularityError(CellularityErrorCode.INVALID_VALUE, message.orEmpty())
    else -> CellularityError(CellularityErrorCode.INTERNAL_FAILURE, message.orEmpty())
}
