package com.cytarch.cellularity.platform.simulation

import android.content.Context
import com.cytarch.cellularity.NativeSimulationFacade
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.microscope.AndroidMicroscopeSurfaceFactory
import com.cytarch.cellularity.platform.render.microscope.SubstratePreviewCaptureCoordinator
import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellularityResult
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.simulation.SimulationGateway
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext

/** Platform composition result: one retained session plus disposable render attachments. */
class AndroidSimulationConnection internal constructor(
    val gateway: SimulationGateway,
    val microscope: AndroidMicroscopeSurfaceFactory,
    private val requestClose: () -> Unit
) : AutoCloseable {
    /** Starts ordered shutdown without blocking the Android main thread. */
    override fun close() = requestClose()
}

object AndroidSimulationFactory {
    suspend fun create(
        context: Context,
        fileRepository: AndroidCellularityFileRepository,
        initialConfiguration: SimulationConfiguration,
        initialTarget: StoredSubstrateId?
    ): AndroidSimulationConnection = createForBackend(
        context = context,
        fileRepository = fileRepository,
        initialConfiguration = initialConfiguration,
        initialTarget = initialTarget,
        backendKind = RenderBackendKind.OPEN_GL_ES
    )

    internal suspend fun createForBackend(
        context: Context,
        fileRepository: AndroidCellularityFileRepository,
        initialConfiguration: SimulationConfiguration,
        initialTarget: StoredSubstrateId?,
        backendKind: RenderBackendKind
    ): AndroidSimulationConnection {
        val facade = try {
            NativeSimulationFacade(
                context.applicationContext,
                false,
                initialConfiguration.maxCellCount,
                initialConfiguration.maxFoodCount,
                initialConfiguration.radius,
                initialConfiguration.innerRadius
            )
        } catch (failure: Throwable) {
            throw IllegalStateException("Failed to create simulation", failure)
        }
        val substratePreviewCapture = SubstratePreviewCaptureCoordinator()
        val runtime = NativeSimulationRuntime(
            simulation = facade,
            files = fileRepository,
            initialTarget = initialTarget,
            substratePreviewCapture = substratePreviewCapture
        )
        val gateway = try {
            AndroidSimulationGateway(runtime, initialTarget)
        } catch (failure: Throwable) {
            try {
                runtime.detachForClose().close()
            } catch (cleanupFailure: Throwable) {
                failure.addSuppressed(cleanupFailure)
            }
            throw IllegalStateException("Failed to initialize simulation session", failure)
        }

        try {
            when (val result = gateway.open()) {
                is CellularityResult.Success -> Unit
                is CellularityResult.Failure -> throw IllegalStateException(
                    "Failed to open simulation: ${result.error.message}"
                )
            }
        } catch (failure: Throwable) {
            withContext(NonCancellable) { gateway.close() }
            throw failure
        }

        val nativeMicroscope = NativeMicroscopeSimulationBackend(
            simulation = facade,
            publishFrame = gateway::publishFrame,
            publicationExecutor = gateway
        )
        val microscope = AndroidMicroscopeSurfaceFactory(
            simulationFactory = { attachmentBackend ->
                gateway.createRenderAttachment(
                    nativeMicroscope,
                    facade.microscopeRenderEndpoint(),
                    attachmentBackend
                )
            },
            selectedGenomeSource = {
                gateway.withRuntimeAccess(fallback = null, nativeMicroscope::selectedGenome)
            },
            substratePreviewCapture = substratePreviewCapture,
            backendKind = backendKind
        )
        return AndroidSimulationConnection(
            gateway = gateway,
            microscope = microscope,
            requestClose = gateway::requestClose
        )
    }
}
