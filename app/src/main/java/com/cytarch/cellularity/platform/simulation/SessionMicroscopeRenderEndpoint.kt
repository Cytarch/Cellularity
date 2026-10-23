package com.cytarch.cellularity.platform.simulation

import android.content.Context
import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.microscope.MicroscopeInputIngressState
import com.cytarch.cellularity.platform.render.microscope.MicroscopeRenderEndpoint
import com.cytarch.cellularity.platform.render.microscope.NativeMicroscopeRenderAccess
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanRenderWorkerBridge
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanWorkerStatus
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

/** A surface-token-scoped view of the durable native render endpoint. */
internal class SessionMicroscopeRenderEndpoint(
    private val session: AndroidSimulationGateway,
    private val surfaceToken: Long,
    private val backendKind: RenderBackendKind,
    private val simulation: MicroscopeSimulationBackend,
    private val render: NativeMicroscopeRenderAccess
) : MicroscopeRenderEndpoint {
    private val inputResyncRequested = AtomicBoolean(false)

    private var workerIdentity = 0L
    private var workerBindingGeneration = 0L
    private var platformSurfaceGeneration = 0L
    private var surfaceWidth = 1
    private var surfaceHeight = 1

    @Volatile
    private var lifecyclePaused = false

    override val runtimePublicationSource: MicroscopeRuntimePublicationSource =
        object : MicroscopeRuntimePublicationSource {
            override val publicationIntervalMillis: Long
                get() = simulation.runtimePublicationSource.publicationIntervalMillis

            override fun tryStartPoll(
                viewGeneration: Long,
                completion: (Long, MicroscopeRuntimePublicationEnvelope) -> Unit
            ): Boolean = simulation.runtimePublicationSource.tryStartPoll(
                viewGeneration
            ) { generation, publication ->
                val ingress = inputIngressSnapshot()
                if (
                    ingress.state == MicroscopeInputIngressState.ACCEPTING ||
                    ingress.state == MicroscopeInputIngressState.CLOSING
                ) {
                    inputResyncRequested.set(false)
                }
                completion(generation, publication.copy(inputIngress = ingress))
            }
        }

    override fun createVulkanRenderWorker(
        context: Context,
        initialContinuousRendering: Boolean,
        statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit
    ): NativeVulkanRenderWorkerBridge = render.borrowRenderEndpointForWorker().use { endpoint ->
        val worker = NativeVulkanRenderWorkerBridge.createDormantMicroscope(
            context = context,
            initialContinuousRendering = initialContinuousRendering,
            statusReceiver = statusReceiver
        )
        val bound = session.beginVulkanWorkerBinding(surfaceToken, worker) {
            worker.authorizeMicroscope(endpoint.handle, surfaceToken)
        }
        if (!bound) {
            worker.closeAsync()
            error("Native Vulkan worker could not bind to the simulation session")
        }
        try {
            worker.installBeforeDestroy {
                check(session.completeVulkanWorkerUnbind(surfaceToken, worker)) {
                    "Native Vulkan worker could not complete its simulation-session unbind"
                }
            }
            worker.start()
            worker
        } catch (failure: Throwable) {
            worker.closeAsync()
            throw failure
        }
    }

    override fun onSurfaceCreated() {
        platformSurfaceGeneration = incrementOrExhaust(platformSurfaceGeneration)
        check(platformSurfaceGeneration != 0L) {
            "Render platform surface generation is exhausted"
        }
        if (
            session.attachSurface(
                token = surfaceToken,
                backendKind = backendKind,
                attachOperation = {
                    if (
                        backendKind == RenderBackendKind.OPEN_GL_ES &&
                        workerBindingGeneration == 0L
                    ) {
                        workerIdentity = surfaceToken
                        workerBindingGeneration = render.authorizeRenderWorker(
                            surfaceToken, workerIdentity
                        )
                    }
                    render.onSurfaceCreated()
                    render.activateRenderSurface(
                        surfaceToken,
                        workerIdentity,
                        workerBindingGeneration,
                        platformSurfaceGeneration,
                        surfaceWidth,
                        surfaceHeight
                    )
                },
                detachOperation = ::detachNativeSurface,
                requestInputResync = { render.requestInputResync(surfaceToken) },
                completeInputResync = { drained ->
                    render.completeInputResync(surfaceToken, drained)
                },
                activePointerCount = render::activePointerGenerationCount
            )
        ) {
            session.setLifecyclePaused(surfaceToken, lifecyclePaused) {
                simulation.setLifecyclePaused(lifecyclePaused)
            }
        }
    }

    override fun onSurfaceChanged(width: Int, height: Int) {
        surfaceWidth = width
        surfaceHeight = height
        session.withRenderEndpointAccess(Unit) {
            render.updateRenderSurfaceDimensions(surfaceToken, width, height)
        }
    }

    override fun onSurfaceDestroyed() {
        requestInputResync()
        session.detachSurface(surfaceToken)
    }

    override fun onVulkanSurfaceReady(): Boolean {
        check(backendKind == RenderBackendKind.VULKAN) {
            "Only a Vulkan attachment can publish native worker readiness"
        }
        val attached = session.attachSurface(
            token = surfaceToken,
            backendKind = backendKind,
            attachOperation = {
                render.activeInputEpoch(surfaceToken).takeIf { it != 0L }
            },
            detachOperation = {},
            requestInputResync = { render.requestInputResync(surfaceToken) },
            completeInputResync = { drained ->
                render.completeInputResync(surfaceToken, drained)
            },
            activePointerCount = render::activePointerGenerationCount
        )
        if (attached) {
            session.setLifecyclePaused(surfaceToken, lifecyclePaused) {
                simulation.setLifecyclePaused(lifecyclePaused)
            }
        }
        return attached
    }

    override fun onVulkanSurfaceUnavailable() {
        if (backendKind != RenderBackendKind.VULKAN) return
        requestInputResync()
        session.detachSurface(surfaceToken)
    }

    override fun draw(): Long = session.frameWithRenderAccess(surfaceToken) {
        render.draw(surfaceToken)
    }

    override fun clearWithoutRendering(): Long =
        session.frameWithRenderAccess(surfaceToken) {
            render.clearWithoutRendering(surfaceToken)
        }

    override fun enqueueSubstratePreviewCapture(
        requestId: Long,
        substratePreviewToken: Long
    ): NativePreviewCaptureRequest? = session.withRenderEndpointAccess(null) {
        try {
            render.enqueueSubstratePreviewCapture(requestId, substratePreviewToken)
        } catch (failure: Throwable) {
            AppLog.error("Could not enqueue the substrate preview capture", failure)
            null
        }
    }

    override fun setLifecyclePaused(paused: Boolean) {
        lifecyclePaused = paused
        session.setLifecyclePaused(surfaceToken, paused) {
            simulation.setLifecyclePaused(paused)
        }
    }

    override fun publishPlacementGenome(genome: GenomeDraft): Long =
        session.withRenderEndpointAccess(0L) {
            render.publishPlacementGenome(GenomeDraftWireCodec.encode(genome))
        }

    override fun submitMicroscopeInputBatch(
        expectedInputEpoch: Long,
        batchSequence: Long,
        input: ByteBuffer,
        result: ByteBuffer,
        expectedPlacementGenomeRevision: Long
    ) {
        session.submitMicroscopeInputBatch(
            token = surfaceToken,
            expectedInputEpoch = expectedInputEpoch,
            batchSequence = batchSequence,
            result = result
        ) {
            render.submitInputBatch(
                surfaceToken,
                input,
                result,
                expectedPlacementGenomeRevision
            )
        }
    }

    override fun readPresentationMotionSnapshot(result: ByteBuffer): Boolean =
        session.readMicroscopePresentationMotionSnapshot(surfaceToken, result) {
            render.readPresentationMotionSnapshot(result)
            true
        }

    override fun inputIngressSnapshot() = session.microscopeInputIngressSnapshot(
        token = surfaceToken,
        ensureResync = inputResyncRequested.get()
    ).also { snapshot ->
        if (
            snapshot.state == MicroscopeInputIngressState.ACCEPTING ||
            snapshot.state == MicroscopeInputIngressState.CLOSING
        ) {
            inputResyncRequested.set(false)
        }
    }

    override fun requestInputResync(): Boolean {
        inputResyncRequested.set(true)
        return session.requestMicroscopeInputResync(surfaceToken)
    }

    override fun setToolOrdinal(toolOrdinal: Int) =
        run { render.setToolOrdinal(toolOrdinal) }

    override fun setTemperatureMode(temperatureMode: Int) =
        run { render.setTemperatureMode(temperatureMode) }

    override fun setSlowMotionSpeed(speed: Float) =
        run { render.setSlowMotionSpeed(speed) }

    override fun setIncubateTargetUps(targetUps: Double) =
        run { render.setIncubateTargetUps(targetUps) }

    override fun setPerformanceHudEnabled(enabled: Boolean) =
        run { render.setPerformanceHudEnabled(enabled) }

    private fun run(operation: () -> Unit) {
        session.withRenderEndpointAccess(Unit, operation)
    }

    private fun detachNativeSurface() {
        var failure = runCatching {
            render.deactivateRenderSurface(surfaceToken)
        }.exceptionOrNull()
        try {
            render.onSurfaceDestroyed()
        } catch (surfaceFailure: Throwable) {
            failure = mergeFailure(failure, surfaceFailure)
        }
        if (
            backendKind == RenderBackendKind.OPEN_GL_ES &&
            workerBindingGeneration != 0L
        ) {
            val closingIdentity = workerIdentity
            val closingGeneration = workerBindingGeneration
            failure = closeWorkerBinding(
                closingIdentity, closingGeneration, failure
            )
            workerIdentity = 0L
            workerBindingGeneration = 0L
        }
        failure?.let { throw it }
    }

    private fun closeWorkerBinding(
        identity: Long,
        generation: Long,
        initialFailure: Throwable?,
        markClosing: Boolean = true
    ): Throwable? {
        var failure = initialFailure
        if (markClosing) {
            val markFailure = runCatching {
                render.markRenderWorkerClosing(
                    surfaceToken, identity, generation
                )
            }.exceptionOrNull()
            if (markFailure != null) failure = mergeFailure(failure, markFailure)
        }
        val finishFailure = runCatching {
            render.finishRenderWorkerClose(
                surfaceToken, identity, generation
            )
        }.exceptionOrNull()
        if (finishFailure != null) failure = mergeFailure(failure, finishFailure)
        return failure
    }

    private fun incrementOrExhaust(value: Long): Long =
        if (value == Long.MAX_VALUE) 0L else value + 1L

    private fun mergeFailure(current: Throwable?, next: Throwable): Throwable {
        if (current == null) return next
        if (current !== next) current.addSuppressed(next)
        return current
    }
}
