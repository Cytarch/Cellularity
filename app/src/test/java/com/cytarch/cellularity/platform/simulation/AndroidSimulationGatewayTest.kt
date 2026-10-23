package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.microscope.MicroscopeInputBatchLayout
import com.cytarch.cellularity.platform.render.microscope.MicroscopeInputIngressState
import com.cytarch.cellularity.platform.render.microscope.MicroscopeInputIngressStatus
import com.cytarch.cellularity.platform.render.microscope.NativeMicroscopeRenderAccess
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.CellularityResult
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.ScalarFieldSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.SimulationStats
import com.cytarch.cellularity.shared.model.VectorFieldSnapshot
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.simulation.ApplyResult
import com.cytarch.cellularity.shared.simulation.CellId
import com.cytarch.cellularity.shared.simulation.EnvironmentBooleanProperty
import com.cytarch.cellularity.shared.simulation.EnvironmentChange
import com.cytarch.cellularity.shared.simulation.EnvironmentPatchRejection
import com.cytarch.cellularity.shared.simulation.EnvironmentPatchRejectionReason
import com.cytarch.cellularity.shared.simulation.EnvironmentPatch
import com.cytarch.cellularity.shared.simulation.SaveResult
import com.cytarch.cellularity.shared.simulation.SimulationCommand
import com.cytarch.cellularity.shared.simulation.SimulationContentState
import com.cytarch.cellularity.shared.simulation.SimulationResult
import com.cytarch.cellularity.shared.simulation.SimulationSessionState
import java.util.Collections
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

private const val TIMEOUT_SECONDS = 5L

class AndroidSimulationGatewayTest {
    @Test
    fun gatewayRoutesOperationsAndPublishesFrameSnapshots() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val initialTarget = StoredSubstrateId("existing")
        val gateway = AndroidSimulationGateway(runtime, initialTarget)
        val patch = EnvironmentPatch.single(
            EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, false),
            baseRevision = 3
        )

        assertEquals(SimulationSessionState.New, gateway.state.value)
        val beforeOpen = gateway.execute(SimulationCommand.Reset)
        assertTrue(beforeOpen is SimulationResult.Failure)
        assertEquals(
            CellularityErrorCode.SESSION_NOT_READY,
            (beforeOpen as SimulationResult.Failure).error.code
        )
        assertTrue(gateway.open() is CellularityResult.Success)
        assertEquals(SimulationSessionState.Ready, gateway.state.value)
        assertEquals(SimulationResult.Queued, gateway.execute(SimulationCommand.Reset))
        assertEquals(ApplyResult.Applied(4), gateway.applyEnvironment(patch))
        assertEquals(null, gateway.inspectCell(CellId(7)))
        assertEquals(SaveResult.Saved(initialTarget), gateway.save())

        val nextStats = SimulationStats(cellCount = 2, foodMass = 4.0, time = 8.0, updateCount = 9)
        val nextEnvironment = environment(revision = 4)
        gateway.publishFrame(nextStats, nextEnvironment)

        assertEquals(listOf(SimulationCommand.Reset), runtime.commands)
        assertEquals(listOf(patch), runtime.patches)
        assertEquals(listOf(CellId(7)), runtime.inspections)
        assertEquals(listOf(initialTarget), runtime.savedTargets)
        assertEquals(nextStats, gateway.stats.value)
        assertEquals(4, gateway.environment.value.revision)
        gateway.close()
    }

    @Test
    fun closedGatewayRejectsNewWorkAndClosesRuntimeOnce() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)

        gateway.close()
        gateway.close()

        assertEquals(1, runtime.closeCount)
        assertEquals(SimulationSessionState.Closed, gateway.state.value)
        assertTrue(gateway.execute(SimulationCommand.Reset) is SimulationResult.Failure)
        assertTrue(
            gateway.applyEnvironment(
                EnvironmentPatch.single(
                    EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, true),
                    baseRevision = 3
                )
            ) is ApplyResult.Failure
        )
        assertTrue(gateway.save(StoredSubstrateId("after-close")) is SaveResult.Failure)
    }

    @Test
    fun lifecyclePublishesOpeningPauseResumeAndSavingStates() = runBlocking {
        val runtime = FakeAndroidRuntime().apply {
            openGate = CountDownLatch(1)
            saveGate = CountDownLatch(1)
        }
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("plate"))

        val opening = async(start = CoroutineStart.UNDISPATCHED) { gateway.open() }
        assertTrue(runtime.openStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertEquals(SimulationSessionState.Opening, gateway.state.value)
        runtime.openGate?.countDown()
        assertTrue(opening.await() is CellularityResult.Success)
        assertEquals(SimulationSessionState.Ready, gateway.state.value)

        val renderer = FakeMicroscopeBackend()
        val firstSurface = gateway.createRenderAttachment(renderer)
        firstSurface.onSurfaceCreated()
        firstSurface.setLifecyclePaused(true)
        firstSurface.onSurfaceDestroyed()
        assertEquals(SimulationSessionState.Paused, gateway.state.value)

        val secondSurface = gateway.createRenderAttachment(renderer)
        secondSurface.onSurfaceCreated()
        assertEquals(SimulationSessionState.Ready, gateway.state.value)

        val saving = async(start = CoroutineStart.UNDISPATCHED) { gateway.save() }
        assertTrue(runtime.saveStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertEquals(SimulationSessionState.Saving, gateway.state.value)
        runtime.saveGate?.countDown()
        assertEquals(SaveResult.Saved(StoredSubstrateId("plate")), saving.await())
        assertEquals(SimulationSessionState.Ready, gateway.state.value)

        gateway.close()
        assertEquals(SimulationSessionState.Closed, gateway.state.value)
    }

    @Test
    fun closeWhileOpeningWaitsForRuntimeAndNeverPublishesReady() = runBlocking {
        val runtime = FakeAndroidRuntime().apply { openGate = CountDownLatch(1) }
        val gateway = AndroidSimulationGateway(runtime, null)
        val opening = async(start = CoroutineStart.UNDISPATCHED) { gateway.open() }
        assertTrue(runtime.openStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))

        gateway.requestClose()
        assertEquals(SimulationSessionState.Closing, gateway.state.value)
        runtime.openGate?.countDown()

        val openResult = opening.await()
        assertTrue(openResult is CellularityResult.Failure)
        assertEquals(
            CellularityErrorCode.SESSION_CLOSED,
            (openResult as CellularityResult.Failure).error.code
        )
        gateway.close()
        assertEquals(1, runtime.openCount)
        assertEquals(1, runtime.closeCount)
        assertEquals(SimulationSessionState.Closed, gateway.state.value)
    }

    @Test
    fun surfaceReplacementReusesWorldAndStaleDetachCannotPauseReplacement() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()

        val first = gateway.createRenderAttachment(renderer)
        first.onSurfaceCreated()
        first.onSurfaceDestroyed()
        val second = gateway.createRenderAttachment(renderer)
        second.onSurfaceCreated()
        val replacement = gateway.createRenderAttachment(renderer)
        replacement.onSurfaceCreated()

        second.onSurfaceDestroyed()
        assertEquals(SimulationSessionState.Ready, gateway.state.value)
        assertEquals(2, renderer.surfaceDestroyedCount)
        replacement.draw()
        assertEquals(1, renderer.drawCount)

        replacement.onSurfaceDestroyed()
        assertEquals(SimulationSessionState.Ready, gateway.state.value)
        assertEquals(3, renderer.surfaceDestroyedCount)
        assertEquals(1, runtime.openCount)
        assertEquals(3, renderer.surfaceCreatedCount)
        gateway.close()
    }

    @Test
    fun controlsReachDurableEndpointWithoutWaitingForSurfaceAttachment() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        val surface = gateway.createRenderAttachment(renderer)

        surface.setToolOrdinal(4)
        surface.setTemperatureMode(2)
        surface.setSlowMotionSpeed(0.25f)
        surface.setIncubateTargetUps(2_500.0)
        surface.setPerformanceHudEnabled(true)

        assertEquals(
            listOf(
                "tool=4",
                "temperature=2",
                "slowMotion=0.25",
                "targetUps=2500.0",
                "performanceHud=true"
            ),
            renderer.controlChanges
        )
        surface.onSurfaceCreated()

        // Surface recreation does not replay a managed shadow copy.
        assertEquals(
            listOf(
                "tool=4",
                "temperature=2",
                "slowMotion=0.25",
                "targetUps=2500.0",
                "performanceHud=true"
            ),
            renderer.controlChanges
        )

        surface.setPerformanceHudEnabled(false)
        assertEquals("performanceHud=false", renderer.controlChanges.last())
        surface.onSurfaceDestroyed()
        gateway.close()
    }

    @Test
    fun inputEpochRejectsStaleBatchesAndAdvancesOnlyAfterPointerDrain() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        val surface = gateway.createRenderAttachment(renderer)
        surface.onSurfaceCreated()

        val initial = surface.inputIngressSnapshot()
        assertEquals(MicroscopeInputIngressState.ACCEPTING, initial.state)
        assertTrue(initial.inputEpoch != 0L)

        val input = ByteBuffer.allocateDirect(1)
        val acceptedResult = MicroscopeInputBatchLayout.allocateResultBuffer()
        surface.submitMicroscopeInputBatch(
            initial.inputEpoch,
            11L,
            input,
            acceptedResult,
            0L
        )
        assertEquals(1, renderer.submittedInputCount)
        assertEquals(
            MicroscopeInputIngressStatus.APPLIED.wireValue,
            acceptedResult.order(ByteOrder.LITTLE_ENDIAN).getInt(52)
        )

        val staleResult = MicroscopeInputBatchLayout.allocateResultBuffer()
        surface.submitMicroscopeInputBatch(
            initial.inputEpoch + 100L,
            12L,
            input,
            staleResult,
            0L
        )
        assertEquals(1, renderer.submittedInputCount)
        assertEquals(
            MicroscopeInputIngressStatus.STALE_INPUT_EPOCH.wireValue,
            staleResult.order(ByteOrder.LITTLE_ENDIAN).getInt(52)
        )

        renderer.activePointerCount = 1
        assertTrue(surface.requestInputResync())
        assertEquals(1, renderer.resyncRequestCount)
        assertEquals(
            MicroscopeInputIngressState.RESYNCING,
            surface.inputIngressSnapshot().state
        )
        assertTrue(surface.requestInputResync())
        assertEquals(1, renderer.resyncRequestCount)

        renderer.activePointerCount = 0
        val recovered = surface.inputIngressSnapshot()
        assertEquals(MicroscopeInputIngressState.ACCEPTING, recovered.state)
        assertTrue(recovered.inputEpoch != 0L)
        assertTrue(recovered.inputEpoch != initial.inputEpoch)

        surface.onSurfaceDestroyed()
        assertEquals(2, renderer.resyncRequestCount)
        gateway.close()
    }

    @Test
    fun closeRequestsPointerCancellationBeforeDetachingSurface() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend().apply { activePointerCount = 1 }
        val surface = gateway.createRenderAttachment(renderer)
        surface.onSurfaceCreated()

        gateway.close()

        assertEquals(1, renderer.resyncRequestCount)
        assertEquals(1, renderer.surfaceDestroyedCount)
        assertEquals(SimulationSessionState.Closed, gateway.state.value)
    }

    @Test
    fun attachmentWithOldPointersPublishesNoEpochUntilOwnerDrain() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend().apply { activePointerCount = 1 }
        val surface = gateway.createRenderAttachment(renderer)
        surface.onSurfaceCreated()

        val resyncing = surface.inputIngressSnapshot()
        assertEquals(MicroscopeInputIngressState.RESYNCING, resyncing.state)
        assertEquals(0L, resyncing.inputEpoch)
        assertEquals(1, renderer.resyncRequestCount)

        renderer.activePointerCount = 0
        val accepting = surface.inputIngressSnapshot()
        assertEquals(MicroscopeInputIngressState.ACCEPTING, accepting.state)
        assertTrue(accepting.inputEpoch != 0L)

        surface.onSurfaceDestroyed()
        gateway.close()
    }

    @Test
    fun rejectedUpDuringHandoffCannotLeakIntoTheFreshEpoch() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        val first = gateway.createRenderAttachment(renderer)
        first.onSurfaceCreated()
        val firstEpoch = first.inputIngressSnapshot().inputEpoch
        val input = ByteBuffer.allocateDirect(1)

        first.submitMicroscopeInputBatch(
            firstEpoch,
            41L,
            input,
            MicroscopeInputBatchLayout.allocateResultBuffer(),
            0L
        )
        renderer.activePointerCount = 1
        first.onSurfaceDestroyed()

        val rejectedUp = MicroscopeInputBatchLayout.allocateResultBuffer()
        first.submitMicroscopeInputBatch(firstEpoch, 42L, input, rejectedUp, 0L)
        assertEquals(
            MicroscopeInputIngressStatus.INACTIVE_SURFACE.wireValue,
            rejectedUp.order(ByteOrder.LITTLE_ENDIAN).getInt(52)
        )
        assertEquals(1, renderer.submittedInputCount)

        val replacement = gateway.createRenderAttachment(renderer)
        replacement.onSurfaceCreated()
        assertEquals(
            MicroscopeInputIngressState.RESYNCING,
            replacement.inputIngressSnapshot().state
        )
        renderer.activePointerCount = 0
        val recovered = replacement.inputIngressSnapshot()
        assertEquals(MicroscopeInputIngressState.ACCEPTING, recovered.state)
        assertTrue(recovered.inputEpoch != 0L && recovered.inputEpoch != firstEpoch)

        replacement.submitMicroscopeInputBatch(
            recovered.inputEpoch,
            43L,
            input,
            MicroscopeInputBatchLayout.allocateResultBuffer(),
            0L
        )
        assertEquals(2, renderer.submittedInputCount)

        replacement.onSurfaceDestroyed()
        gateway.close()
    }

    @Test
    fun inputBatchDoesNotWaitForSurfaceWriteLock() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        val first = gateway.createRenderAttachment(renderer)
        first.onSurfaceCreated()
        val epoch = first.inputIngressSnapshot().inputEpoch

        renderer.surfaceCreateGate = CountDownLatch(1)
        val replacement = gateway.createRenderAttachment(renderer)
        val attaching = async(Dispatchers.Default) {
            replacement.onSurfaceCreated()
        }
        assertTrue(renderer.secondSurfaceCreated.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))

        val result = MicroscopeInputBatchLayout.allocateResultBuffer()
        val startedAt = System.nanoTime()
        first.submitMicroscopeInputBatch(
            epoch,
            21L,
            ByteBuffer.allocateDirect(1),
            result,
            0L
        )
        val elapsedMillis = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startedAt)
        assertTrue(elapsedMillis < 100L)
        assertEquals(
            MicroscopeInputIngressStatus.ATTACHMENT_UNAVAILABLE.wireValue,
            result.order(ByteOrder.LITTLE_ENDIAN).getInt(52)
        )
        assertEquals(0, renderer.submittedInputCount)

        renderer.surfaceCreateGate?.countDown()
        attaching.await()
        replacement.onSurfaceDestroyed()
        gateway.close()
    }

    @Test
    fun inputBatchEpochValidationAndResyncAreOneAttachmentTransaction() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend().apply {
            inputSubmitGate = CountDownLatch(1)
        }
        val surface = gateway.createRenderAttachment(renderer)
        surface.onSurfaceCreated()
        val epoch = surface.inputIngressSnapshot().inputEpoch

        val submitting = async(Dispatchers.Default) {
            surface.submitMicroscopeInputBatch(
                epoch,
                31L,
                ByteBuffer.allocateDirect(1),
                MicroscopeInputBatchLayout.allocateResultBuffer(),
                0L
            )
        }
        assertTrue(renderer.inputSubmitStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))

        val resyncing = async(Dispatchers.Default) { surface.requestInputResync() }
        assertFalse(renderer.inputResyncEntered.await(100, TimeUnit.MILLISECONDS))

        renderer.inputSubmitGate?.countDown()
        submitting.await()
        assertTrue(resyncing.await())
        assertTrue(renderer.inputResyncEntered.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertEquals(1, renderer.resyncRequestCount)

        surface.onSurfaceDestroyed()
        gateway.close()
    }

    @Test
    fun surfaceDetachWaitsForInFlightDrawAndRejectsLateDraws() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend().apply {
            drawGate = CountDownLatch(1)
            surfaceDestroyedEntered = CountDownLatch(1)
        }
        val surface = gateway.createRenderAttachment(renderer)
        surface.onSurfaceCreated()

        val drawing = async(Dispatchers.Default) { surface.draw() }
        assertTrue(renderer.drawStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        val detachCallStarted = CountDownLatch(1)
        val detaching = async(Dispatchers.Default) {
            detachCallStarted.countDown()
            surface.onSurfaceDestroyed()
        }
        assertTrue(detachCallStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertFalse(renderer.surfaceDestroyedEntered?.await(100, TimeUnit.MILLISECONDS) ?: true)

        renderer.drawGate?.countDown()
        drawing.await()
        detaching.await()
        surface.draw()

        assertEquals(1, renderer.drawCount)
        assertEquals(1, renderer.surfaceDestroyedCount)
        assertEquals(SimulationSessionState.Ready, gateway.state.value)
        gateway.close()
    }

    @Test
    fun glesToVulkanHandoffDetachesOldSurfaceBeforeWorkerAuthorization() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        val gles = gateway.createRenderAttachment(renderer, RenderBackendKind.OPEN_GL_ES)
        gles.onSurfaceCreated()

        gateway.createRenderAttachment(renderer, RenderBackendKind.VULKAN)
        val workerOwner = Any()
        var authorized = false
        assertTrue(gateway.beginVulkanWorkerBinding(2L, workerOwner) {
            authorized = true
        })
        gles.draw()

        assertEquals(1, renderer.surfaceDestroyedCount)
        assertTrue(authorized)
        assertEquals(0, renderer.drawCount)
        assertEquals(SimulationSessionState.Ready, gateway.state.value)

        assertTrue(gateway.completeVulkanWorkerUnbind(2L, workerOwner))
        gateway.close()
    }

    @Test
    fun staleVulkanReadinessIsIgnoredWithoutFailingTheSession() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        gateway.createRenderAttachment(renderer, RenderBackendKind.VULKAN)
        val workerOwner = Any()
        assertTrue(gateway.beginVulkanWorkerBinding(1L, workerOwner) {})
        var detachCalled = false

        val attached = gateway.attachSurface(
            token = 1L,
            backendKind = RenderBackendKind.VULKAN,
            attachOperation = { null },
            detachOperation = { detachCalled = true },
            requestInputResync = { true },
            completeInputResync = { 0L },
            activePointerCount = { 0 }
        )

        assertFalse(attached)
        assertFalse(detachCalled)
        assertEquals(SimulationSessionState.Ready, gateway.state.value)
        assertTrue(gateway.completeVulkanWorkerUnbind(1L, workerOwner))
        gateway.close()
    }

    @Test
    fun vulkanToGlesHandoffWaitsUntilVulkanSessionIsUnbound() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        gateway.createRenderAttachment(renderer, RenderBackendKind.VULKAN)
        val workerOwner = Any()
        assertTrue(gateway.beginVulkanWorkerBinding(1L, workerOwner) {})

        val gles = gateway.createRenderAttachment(renderer, RenderBackendKind.OPEN_GL_ES)
        val attachCallStarted = CountDownLatch(1)
        val attaching = async(Dispatchers.Default) {
            attachCallStarted.countDown()
            gles.onSurfaceCreated()
        }
        assertTrue(attachCallStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertFalse(renderer.secondSurfaceCreated.await(100, TimeUnit.MILLISECONDS))

        assertTrue(gateway.completeVulkanWorkerUnbind(1L, workerOwner))
        attaching.await()
        gles.draw()

        assertEquals(1, renderer.surfaceCreatedCount)
        assertEquals(1, renderer.drawCount)
        assertEquals(SimulationSessionState.Ready, gateway.state.value)
        gles.onSurfaceDestroyed()
        gateway.close()
    }

    @Test
    fun vulkanOwnerCanFinalizeAfterGatewayClose() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, null)
        val owner = Any()
        assertTrue(gateway.open() is CellularityResult.Success)
        assertTrue(gateway.beginVulkanWorkerBinding(1L, owner) {})

        gateway.close()

        assertEquals(SimulationSessionState.Closed, gateway.state.value)
        assertEquals(1, runtime.closeCount)
        assertFalse(gateway.completeVulkanWorkerUnbind(1L, Any()))
        assertFalse(gateway.completeVulkanWorkerUnbind(2L, owner))
        assertTrue(gateway.completeVulkanWorkerUnbind(1L, owner))
        assertTrue(gateway.completeVulkanWorkerUnbind(1L, owner))
        assertEquals(1, runtime.closeCount)
        assertEquals(SimulationSessionState.Closed, gateway.state.value)
    }

    @Test
    fun vulkanOwnerFinalizationIsIdempotentButRejectsUnknownOwners() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        val owner = Any()
        assertTrue(gateway.open() is CellularityResult.Success)
        assertFalse(gateway.completeVulkanWorkerUnbind(1L, owner))
        assertTrue(gateway.beginVulkanWorkerBinding(1L, owner) {})
        assertTrue(gateway.completeVulkanWorkerUnbind(1L, owner))
        assertTrue(gateway.completeVulkanWorkerUnbind(1L, owner))
        assertFalse(gateway.completeVulkanWorkerUnbind(1L, Any()))
        assertFalse(gateway.completeVulkanWorkerUnbind(2L, owner))
        gateway.close()
        assertTrue(gateway.completeVulkanWorkerUnbind(1L, owner))
    }

    @Test
    fun completedVulkanOwnerCannotUnbindItsReplacement() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        val oldOwner = Any()
        val newOwner = Any()
        assertTrue(gateway.open() is CellularityResult.Success)
        assertTrue(gateway.beginVulkanWorkerBinding(1L, oldOwner) {})
        assertTrue(gateway.completeVulkanWorkerUnbind(1L, oldOwner))
        assertTrue(gateway.beginVulkanWorkerBinding(2L, newOwner) {})

        assertTrue(gateway.completeVulkanWorkerUnbind(1L, oldOwner))
        assertFalse(gateway.completeVulkanWorkerUnbind(2L, oldOwner))
        assertFalse(gateway.completeVulkanWorkerUnbind(1L, newOwner))
        var rebound = false
        assertTrue(gateway.beginVulkanWorkerBinding(2L, newOwner) { rebound = true })
        assertFalse(rebound)
        assertTrue(gateway.completeVulkanWorkerUnbind(2L, newOwner))
        gateway.close()
    }

    @Test
    fun closedGatewayRejectsReplacementWhileItsVulkanOwnerFinalizes() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        val owner = Any()
        assertTrue(gateway.open() is CellularityResult.Success)
        assertTrue(gateway.beginVulkanWorkerBinding(1L, owner) {})
        gateway.close()
        var authorized = false
        assertFalse(gateway.beginVulkanWorkerBinding(2L, Any()) { authorized = true })
        assertFalse(authorized)
        assertTrue(gateway.completeVulkanWorkerUnbind(1L, owner))
        assertEquals(SimulationSessionState.Closed, gateway.state.value)
    }

    @Test
    fun closeWhileSaveWaitsAndRejectsQueuedAndNewCommands() = runBlocking {
        val runtime = FakeAndroidRuntime().apply { saveGate = CountDownLatch(1) }
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("plate"))
        assertTrue(gateway.open() is CellularityResult.Success)

        val saving = async(start = CoroutineStart.UNDISPATCHED) { gateway.save() }
        assertTrue(runtime.saveStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        val queuedBeforeClose = async(start = CoroutineStart.UNDISPATCHED) {
            gateway.execute(SimulationCommand.Reset)
        }
        gateway.requestClose()

        assertEquals(SimulationSessionState.Closing, gateway.state.value)
        val rejectedAfterClose = gateway.execute(SimulationCommand.Reset)
        assertTrue(rejectedAfterClose is SimulationResult.Failure)
        assertEquals(
            CellularityErrorCode.SESSION_CLOSED,
            (rejectedAfterClose as SimulationResult.Failure).error.code
        )

        runtime.saveGate?.countDown()
        assertEquals(SaveResult.Saved(StoredSubstrateId("plate")), saving.await())
        val queuedResult = queuedBeforeClose.await()
        assertTrue(queuedResult is SimulationResult.Failure)
        gateway.close()

        assertEquals(listOf("open", "save:start", "save:end", "close"), runtime.events.toList())
        assertTrue(runtime.commands.isEmpty())
        assertEquals(1, runtime.closeCount)
        assertEquals(SimulationSessionState.Closed, gateway.state.value)
    }

    @Test
    fun acceptedOperationsRunSeriallyInQueueOrder() = runBlocking {
        val runtime = FakeAndroidRuntime().apply { executeGate = CountDownLatch(1) }
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val patch = EnvironmentPatch.single(
            EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, false),
            baseRevision = 3
        )

        val first = async(start = CoroutineStart.UNDISPATCHED) {
            gateway.execute(SimulationCommand.Reset)
        }
        assertTrue(runtime.executeStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        val second = async(start = CoroutineStart.UNDISPATCHED) {
            gateway.applyEnvironment(patch)
        }

        assertTrue(runtime.patches.isEmpty())
        assertEquals(1, runtime.maxConcurrentCalls.get())
        runtime.executeGate?.countDown()
        assertEquals(SimulationResult.Queued, first.await())
        assertEquals(ApplyResult.Applied(4), second.await())

        assertEquals(listOf("open", "execute:start", "execute:end", "apply"), runtime.events.toList())
        assertEquals(1, runtime.maxConcurrentCalls.get())
        gateway.close()
    }

    @Test
    fun renderDoesNotWaitBehindQueuedRuntimeMutation() = runBlocking {
        val runtime = FakeAndroidRuntime().apply { saveGate = CountDownLatch(1) }
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("plate"))
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        val surface = gateway.createRenderAttachment(renderer)
        surface.onSurfaceCreated()

        val saving = async(start = CoroutineStart.UNDISPATCHED) { gateway.save() }
        assertTrue(runtime.saveStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        val renderAttempted = CountDownLatch(1)
        val rendering = async(Dispatchers.Default) {
            renderAttempted.countDown()
            surface.draw()
        }
        assertTrue(renderAttempted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))

        rendering.await()
        assertEquals(1, renderer.drawCount)
        assertFalse(saving.isCompleted)
        runtime.saveGate?.countDown()

        assertEquals(SaveResult.Saved(StoredSubstrateId("plate")), saving.await())
        gateway.close()
    }

    @Test
    fun reloadPreparationDoesNotHoldRenderOrRuntimeLocks() = runBlocking {
        val runtime = FakeAndroidRuntime().apply {
            prepareReplacementGate = CountDownLatch(1)
        }
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("old"))
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        val surface = gateway.createRenderAttachment(renderer)
        surface.onSurfaceCreated()

        val reload = async(start = CoroutineStart.UNDISPATCHED) {
            gateway.execute(
                SimulationCommand.Reload(StoredSubstrateId("replacement"))
            )
        }
        assertTrue(
            runtime.prepareReplacementStarted.await(
                TIMEOUT_SECONDS,
                TimeUnit.SECONDS
            )
        )

        val drawing = async(Dispatchers.Default) { surface.draw() }
        assertEquals(presentedFrameResult(), drawing.await())
        assertEquals(1, renderer.drawCount)
        assertEquals(0, runtime.commitReplacementCount)

        runtime.prepareReplacementGate?.countDown()
        assertEquals(SimulationResult.Queued, reload.await())
        assertEquals(1, runtime.commitReplacementCount)
        assertEquals(1, runtime.preparedCloseCount)
        assertEquals(1, runtime.retirementCloseCount)
        surface.onSurfaceDestroyed()
        gateway.close()
    }

    @Test
    fun reloadCommitWaitsForInFlightFrameThenAdvancesInputEpoch() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("old"))
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend().apply {
            drawGate = CountDownLatch(1)
        }
        val surface = gateway.createRenderAttachment(renderer)
        surface.onSurfaceCreated()
        val oldEpoch = surface.inputIngressSnapshot().inputEpoch

        val drawing = async(Dispatchers.Default) { surface.draw() }
        assertTrue(renderer.drawStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        val reload = async(Dispatchers.Default) {
            gateway.execute(
                SimulationCommand.Reload(StoredSubstrateId("replacement"))
            )
        }
        assertTrue(
            runtime.prepareReplacementStarted.await(
                TIMEOUT_SECONDS,
                TimeUnit.SECONDS
            )
        )
        assertFalse(
            runtime.commitReplacementStarted.await(100, TimeUnit.MILLISECONDS)
        )

        renderer.drawGate?.countDown()
        drawing.await()
        assertEquals(SimulationResult.Queued, reload.await())
        assertTrue(
            runtime.commitReplacementStarted.await(
                TIMEOUT_SECONDS,
                TimeUnit.SECONDS
            )
        )
        val newIngress = surface.inputIngressSnapshot()
        assertEquals(MicroscopeInputIngressState.ACCEPTING, newIngress.state)
        assertTrue(newIngress.inputEpoch > oldEpoch)
        assertEquals(1, renderer.resyncRequestCount)

        surface.onSurfaceDestroyed()
        gateway.close()
    }

    @Test
    fun reloadCommitFailureDiscardsCandidateAndLeavesCurrentRuntimeUsable() = runBlocking {
        val runtime = FakeAndroidRuntime().apply {
            commitReplacementFailure = IllegalArgumentException(
                "replacement rejected"
            )
        }
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("old"))
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend()
        val surface = gateway.createRenderAttachment(renderer)
        surface.onSurfaceCreated()
        val oldEpoch = surface.inputIngressSnapshot().inputEpoch

        val failed = gateway.execute(
            SimulationCommand.Reload(StoredSubstrateId("replacement"))
        )

        assertTrue(failed is SimulationResult.Failure)
        assertEquals(
            CellularityErrorCode.INVALID_VALUE,
            (failed as SimulationResult.Failure).error.code
        )
        assertEquals(SimulationSessionState.Ready, gateway.state.value)
        assertEquals(0, runtime.commitReplacementCount)
        assertEquals(1, runtime.preparedCloseCount)
        assertEquals(0, runtime.retirementCloseCount)
        assertTrue(surface.inputIngressSnapshot().inputEpoch > oldEpoch)
        assertEquals(
            SimulationResult.Queued,
            gateway.execute(SimulationCommand.Reset)
        )

        surface.onSurfaceDestroyed()
        gateway.close()
    }

    @Test
    fun attachmentRecoveryFailureDoesNotMisreportCommittedReload() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("old"))
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend().apply {
            completeInputResyncFailure = IllegalStateException(
                "could not complete input resync"
            )
        }
        gateway.createRenderAttachment(renderer).onSurfaceCreated()

        val result = gateway.execute(
            SimulationCommand.Reload(StoredSubstrateId("replacement"))
        )

        assertEquals(SimulationResult.Queued, result)
        assertTrue(gateway.state.value is SimulationSessionState.Failed)
        assertEquals(1, runtime.commitReplacementCount)
        assertEquals(1, runtime.preparedCloseCount)
        assertEquals(1, runtime.retirementCloseCount)

        gateway.close()
    }

    @Test
    fun closeDuringReloadPreparationDiscardsCandidateWithoutCommit() = runBlocking {
        val runtime = FakeAndroidRuntime().apply {
            prepareReplacementGate = CountDownLatch(1)
        }
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("old"))
        assertTrue(gateway.open() is CellularityResult.Success)

        val reload = async(start = CoroutineStart.UNDISPATCHED) {
            gateway.execute(
                SimulationCommand.Reload(StoredSubstrateId("replacement"))
            )
        }
        assertTrue(
            runtime.prepareReplacementStarted.await(
                TIMEOUT_SECONDS,
                TimeUnit.SECONDS
            )
        )
        gateway.requestClose()
        runtime.prepareReplacementGate?.countDown()

        val result = reload.await()
        assertTrue(result is SimulationResult.Failure)
        assertEquals(
            CellularityErrorCode.SESSION_CLOSED,
            (result as SimulationResult.Failure).error.code
        )
        gateway.close()
        assertEquals(0, runtime.commitReplacementCount)
        assertEquals(1, runtime.preparedCloseCount)
        assertEquals(1, runtime.closeCount)
    }

    @Test
    fun reloadPreparationFailureLeavesCurrentSessionUsable() = runBlocking {
        val runtime = FakeAndroidRuntime().apply {
            prepareReplacementFailure = IllegalArgumentException(
                "invalid replacement"
            )
        }
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("old"))
        assertTrue(gateway.open() is CellularityResult.Success)

        val failed = gateway.execute(
            SimulationCommand.Reload(StoredSubstrateId("broken"))
        )
        assertTrue(failed is SimulationResult.Failure)
        assertEquals(SimulationSessionState.Ready, gateway.state.value)
        assertEquals(0, runtime.commitReplacementCount)
        assertEquals(
            SimulationResult.Queued,
            gateway.execute(SimulationCommand.Reset)
        )

        gateway.close()
    }

    @Test
    fun publicationRunsWithoutSurfaceAndAllowsOnlyOnePollInFlight() = runBlocking {
        val publicationExecutor = Executors.newSingleThreadExecutor { operation ->
            Thread(operation, "test-runtime-publication")
        }
        val publicationDispatcher = publicationExecutor.asCoroutineDispatcher()
        val gateway = AndroidSimulationGateway(
            runtime = FakeAndroidRuntime(),
            initialTarget = null,
            publicationDispatcher = publicationDispatcher
        )
        val pollEntered = CountDownLatch(1)
        val allowPoll = CountDownLatch(1)
        val completed = CountDownLatch(1)
        val completionThread = AtomicReference<String>()
        try {
            assertTrue(gateway.open() is CellularityResult.Success)

            assertTrue(
                gateway.tryStartPublicationPoll(
                    viewGeneration = 17L,
                    poll = {
                        pollEntered.countDown()
                        check(allowPoll.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
                        null
                    },
                    completion = { generation, snapshot ->
                        assertEquals(17L, generation)
                        assertNull(snapshot)
                        completionThread.set(Thread.currentThread().name)
                        completed.countDown()
                    }
                )
            )
            assertTrue(pollEntered.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
            assertFalse(
                gateway.tryStartPublicationPoll(18L, { null }) { _, _ -> Unit }
            )

            allowPoll.countDown()
            assertTrue(completed.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
            assertTrue(completionThread.get().startsWith("test-runtime-publication"))
        } finally {
            allowPoll.countDown()
            gateway.close()
            publicationDispatcher.close()
            publicationExecutor.shutdownNow()
        }
    }

    @Test
    fun publicationSkipsRuntimeLockContentionWithoutWaiting() = runBlocking {
        val publicationExecutor = Executors.newSingleThreadExecutor()
        val publicationDispatcher = publicationExecutor.asCoroutineDispatcher()
        val gateway = AndroidSimulationGateway(
            runtime = FakeAndroidRuntime(),
            initialTarget = null,
            publicationDispatcher = publicationDispatcher
        )
        val runtimeLockHeld = CountDownLatch(1)
        val releaseRuntimeLock = CountDownLatch(1)
        val pollCount = AtomicInteger()
        val completionCount = AtomicInteger()
        try {
            assertTrue(gateway.open() is CellularityResult.Success)
            val lockOwner = async(Dispatchers.Default) {
                gateway.withRuntimeAccess(Unit) {
                    runtimeLockHeld.countDown()
                    check(releaseRuntimeLock.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
                }
            }
            assertTrue(runtimeLockHeld.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))

            assertTrue(
                gateway.tryStartPublicationPoll(
                    1L,
                    { pollCount.incrementAndGet(); null },
                    { _, _ -> completionCount.incrementAndGet() }
                )
            )
            val workerPassedSkippedPoll = CountDownLatch(1)
            publicationExecutor.execute(workerPassedSkippedPoll::countDown)
            assertTrue(workerPassedSkippedPoll.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
            assertEquals(0, pollCount.get())
            assertEquals(1, completionCount.get())

            releaseRuntimeLock.countDown()
            lockOwner.await()
            val completed = CountDownLatch(1)
            assertTrue(
                gateway.tryStartPublicationPoll(
                    2L,
                    { pollCount.incrementAndGet(); null },
                    { _, _ -> completed.countDown() }
                )
            )
            assertTrue(completed.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
            assertEquals(1, pollCount.get())
        } finally {
            releaseRuntimeLock.countDown()
            gateway.close()
            publicationDispatcher.close()
            publicationExecutor.shutdownNow()
        }
    }

    @Test
    fun closeDropsLatePublicationCompletion() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        val pollEntered = CountDownLatch(1)
        val allowPoll = CountDownLatch(1)
        val completionCount = AtomicInteger()
        assertTrue(gateway.open() is CellularityResult.Success)

        assertTrue(
            gateway.tryStartPublicationPoll(
                1L,
                {
                    pollEntered.countDown()
                    check(allowPoll.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
                    null
                },
                { _, _ -> completionCount.incrementAndGet() }
            )
        )
        assertTrue(pollEntered.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        gateway.requestClose()
        allowPoll.countDown()
        gateway.close()

        assertEquals(0, completionCount.get())
        assertFalse(
            gateway.tryStartPublicationPoll(2L, { null }) { _, _ -> Unit }
        )
    }

    @Test
    fun unexpectedNativeFailureMovesSessionToFailedAndStopsRenderAccess() = runBlocking {
        val runtime = FakeAndroidRuntime().apply {
            executeFailure = IllegalStateException("native exploded")
        }
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)

        val result = gateway.execute(SimulationCommand.Reset)
        assertTrue(result is SimulationResult.Failure)
        assertEquals(
            CellularityErrorCode.INTERNAL_FAILURE,
            (result as SimulationResult.Failure).error.code
        )
        val failed = gateway.state.value
        assertTrue(failed is SimulationSessionState.Failed)
        assertEquals("native exploded", (failed as SimulationSessionState.Failed).error.message)

        val renderer = FakeMicroscopeBackend()
        gateway.createRenderAttachment(renderer).onSurfaceCreated()
        assertEquals(0, renderer.surfaceCreatedCount)
        assertEquals(0, renderer.authorizeRenderWorkerCount)
        val rejected = gateway.applyEnvironment(
            EnvironmentPatch.single(
                EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, true),
                baseRevision = 3
            )
        )
        assertTrue(rejected is ApplyResult.Failure)
        assertTrue(runtime.patches.isEmpty())

        gateway.close()
        assertEquals(1, runtime.closeCount)
        assertEquals(SimulationSessionState.Closed, gateway.state.value)
    }

    @Test
    fun failedGlesAttachReleasesItsNativeWorkerBinding() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val renderer = FakeMicroscopeBackend().apply {
            surfaceCreateFailure = IllegalStateException("surface creation failed")
        }

        gateway.createRenderAttachment(renderer).onSurfaceCreated()

        assertTrue(gateway.state.value is SimulationSessionState.Failed)
        assertEquals(1, renderer.authorizeRenderWorkerCount)
        assertEquals(1, renderer.deactivateRenderSurfaceCount)
        assertEquals(1, renderer.surfaceDestroyedCount)
        assertEquals(1, renderer.markRenderWorkerClosingCount)
        assertEquals(1, renderer.finishRenderWorkerCloseCount)
        gateway.close()
    }

    @Test
    fun surfaceTokenGenerationFailsClosedInsteadOfWrappingToZero() = runBlocking {
        val gateway = AndroidSimulationGateway(FakeAndroidRuntime(), null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val tokenField = AndroidSimulationGateway::class.java
            .getDeclaredField("nextSurfaceToken")
            .apply { isAccessible = true }
        tokenField.setLong(gateway, Long.MAX_VALUE)

        gateway.createRenderAttachment(FakeMicroscopeBackend())
        val failure = runCatching {
            gateway.createRenderAttachment(FakeMicroscopeBackend())
        }.exceptionOrNull()

        assertTrue(failure is IllegalStateException)
        assertTrue(failure?.message?.contains("token generation is exhausted") == true)
        gateway.close()
    }

    @Test
    fun nativeOutOfMemoryFailureDoesNotKillTheSessionQueue() = runBlocking {
        val runtime = FakeAndroidRuntime().apply {
            executeFailure = OutOfMemoryError("native allocation failed")
        }
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)

        val result = gateway.execute(SimulationCommand.Reset)
        assertTrue(result is SimulationResult.Failure)
        assertEquals(
            CellularityErrorCode.OUT_OF_MEMORY,
            (result as SimulationResult.Failure).error.code
        )
        assertTrue(gateway.state.value is SimulationSessionState.Failed)

        val rejected = gateway.execute(SimulationCommand.Reset)
        assertTrue(rejected is SimulationResult.Failure)
        assertEquals(
            CellularityErrorCode.OUT_OF_MEMORY,
            (rejected as SimulationResult.Failure).error.code
        )
        gateway.close()
        assertEquals(1, runtime.closeCount)
    }

    @Test
    fun rejectedEnvironmentPatchPublishesAuthoritativeRevisionWithoutMutation() = runBlocking {
        val runtime = FakeAndroidRuntime().apply {
            nextApplyResult = ApplyResult.Rejected(
                currentRevision = 3,
                rejections = listOf(
                    EnvironmentPatchRejection(
                        changeIndex = -1,
                        reason = EnvironmentPatchRejectionReason.REVISION_CONFLICT
                    )
                )
            )
        }
        val gateway = AndroidSimulationGateway(runtime, null)
        assertTrue(gateway.open() is CellularityResult.Success)
        val patch = EnvironmentPatch.single(
            EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, false),
            baseRevision = 2
        )

        val result = gateway.applyEnvironment(patch)

        assertTrue(result is ApplyResult.Rejected)
        assertEquals(3, gateway.environment.value.revision)
        assertEquals(listOf(patch), runtime.patches)
        gateway.close()
    }

    @Test
    fun contentStateTracksStoredSaveAndOnlyTheFirstUnsavedMutation() = runBlocking {
        val target = StoredSubstrateId("plate")
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, target)
        assertEquals(SimulationContentState.storedWorld(), gateway.contentState.value)
        assertTrue(gateway.open() is CellularityResult.Success)

        gateway.publishFrame(
            SimulationStats(time = 1.0, updateCount = 1),
            null
        )
        assertEquals(SimulationContentState(1, 0), gateway.contentState.value)

        gateway.publishFrame(
            SimulationStats(time = 2.0, updateCount = 2),
            null
        )
        assertEquals(
            "Further world updates do not churn UI state while it is already dirty",
            SimulationContentState(1, 0),
            gateway.contentState.value
        )

        assertEquals(SaveResult.Saved(target), gateway.save(target))
        assertEquals(SimulationContentState(1, 1), gateway.contentState.value)

        gateway.publishFrame(
            SimulationStats(time = 3.0, updateCount = 3),
            null
        )
        assertEquals(SimulationContentState(2, 1), gateway.contentState.value)
        gateway.close()
    }

    @Test
    fun newWorldStartsDirtyAndAppliedEnvironmentChangeDirtiesSavedWorld() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, null)
        assertEquals(SimulationContentState.newWorld(), gateway.contentState.value)
        assertTrue(gateway.contentState.value.isDirty)
        assertTrue(gateway.open() is CellularityResult.Success)

        val target = StoredSubstrateId("first-save")
        assertEquals(SaveResult.Saved(target), gateway.save(target))
        assertEquals(SimulationContentState(0, 0), gateway.contentState.value)

        val applied = EnvironmentPatch.single(
            EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, false),
            baseRevision = 3
        )
        assertEquals(ApplyResult.Applied(4), gateway.applyEnvironment(applied))
        assertEquals(SimulationContentState(1, 0), gateway.contentState.value)

        runtime.nextApplyResult = ApplyResult.Rejected(
            currentRevision = 4,
            rejections = listOf(
                EnvironmentPatchRejection(
                    changeIndex = -1,
                    reason = EnvironmentPatchRejectionReason.REVISION_CONFLICT
                )
            )
        )
        assertTrue(gateway.applyEnvironment(applied.copyAtRevision(4)) is ApplyResult.Rejected)
        assertEquals(SimulationContentState(1, 0), gateway.contentState.value)
        gateway.close()
    }

    @Test
    fun queuedReloadBecomesCleanOnlyAtSafePointAndResetBecomesDirty() = runBlocking {
        val runtime = FakeAndroidRuntime()
        val gateway = AndroidSimulationGateway(runtime, StoredSubstrateId("old"))
        assertTrue(gateway.open() is CellularityResult.Success)
        gateway.publishFrame(SimulationStats(time = 1.0, updateCount = 1), null)
        assertEquals(SimulationContentState(1, 0), gateway.contentState.value)

        val replacement = StoredSubstrateId("replacement")
        assertEquals(
            SimulationResult.Queued,
            gateway.execute(SimulationCommand.Reload(replacement))
        )
        assertEquals(SimulationContentState(1, 0), gateway.contentState.value)

        gateway.publishFrame(SimulationStats(), environment(revision = 3))
        assertEquals(SimulationContentState(2, 2), gateway.contentState.value)
        assertEquals(SaveResult.Saved(replacement), gateway.save())
        assertEquals(replacement, runtime.savedTargets.last())

        assertEquals(SimulationResult.Queued, gateway.execute(SimulationCommand.Reset))
        gateway.publishFrame(SimulationStats(), environment(revision = 3))
        assertEquals(SimulationContentState(3, 2), gateway.contentState.value)

        gateway.publishFrame(SimulationStats(time = 1.0, updateCount = 1), null)
        assertEquals(SimulationContentState(3, 2), gateway.contentState.value)
        assertEquals(SimulationResult.Queued, gateway.execute(SimulationCommand.Reset))
        gateway.publishFrame(SimulationStats(), environment(revision = 3))
        assertEquals(
            "Reset replacement must advance even when the previous world was already dirty",
            SimulationContentState(4, 2),
            gateway.contentState.value
        )
        gateway.close()
    }

}

private fun presentedFrameResult(): Long =
    com.cytarch.cellularity.platform.render.RenderFrameResultCodec.pack(
        com.cytarch.cellularity.platform.render.RenderFrameResultCodec.OUTCOME_PRESENTED,
        com.cytarch.cellularity.platform.render.RenderFrameResultCodec.FLAG_PRIMARY_CONTENT_PRESENTED
    )

private fun EnvironmentPatch.copyAtRevision(revision: Long): EnvironmentPatch =
    EnvironmentPatch(
        schemaVersion = schemaVersion,
        baseRevision = revision,
        changes = changes
    )

private class FakeAndroidRuntime : AndroidSimulationRuntime {
    val commands = Collections.synchronizedList(mutableListOf<SimulationCommand>())
    val patches = Collections.synchronizedList(mutableListOf<EnvironmentPatch>())
    val inspections = Collections.synchronizedList(mutableListOf<CellId>())
    val savedTargets = Collections.synchronizedList(mutableListOf<StoredSubstrateId>())
    val events = Collections.synchronizedList(mutableListOf<String>())
    val openStarted = CountDownLatch(1)
    val saveStarted = CountDownLatch(1)
    val executeStarted = CountDownLatch(1)
    val prepareReplacementStarted = CountDownLatch(1)
    val commitReplacementStarted = CountDownLatch(1)
    val maxConcurrentCalls = AtomicInteger()
    var openGate: CountDownLatch? = null
    var saveGate: CountDownLatch? = null
    var executeGate: CountDownLatch? = null
    var prepareReplacementGate: CountDownLatch? = null
    var commitReplacementGate: CountDownLatch? = null
    var executeFailure: Throwable? = null
    var prepareReplacementFailure: Throwable? = null
    var commitReplacementFailure: Throwable? = null
    var nextApplyResult: ApplyResult? = null
    var openCount = 0
    var closeCount = 0
    var preparedCloseCount = 0
    var commitReplacementCount = 0
    var retirementCloseCount = 0
    private var environmentRevision = 3L
    private var replacementGeneration = 1L
    private var detached = false
    private val activeCalls = AtomicInteger()

    private inner class FakePreparedReplacement(
        override val target: StoredSubstrateId,
        val expectedGeneration: Long
    ) : PreparedRuntimeReplacement {
        var closed = false

        override fun close() {
            if (closed) return
            closed = true
            preparedCloseCount += 1
        }
    }

    private inner class FakeRetiredRuntime(
        private val terminal: Boolean
    ) : RetiredRuntime {
        private var closed = false

        override fun close() {
            if (closed) return
            closed = true
            if (terminal) {
                events += "close"
                closeCount += 1
            } else {
                retirementCloseCount += 1
            }
        }
    }

    override fun open() = tracked("open", split = false) {
        openStarted.countDown()
        await(openGate)
        openCount += 1
    }

    override fun environment(): EnvironmentSnapshot = environment(revision = environmentRevision)

    override fun stats(): SimulationStats = SimulationStats()

    override fun execute(command: SimulationCommand): SimulationResult = tracked("execute") {
        executeStarted.countDown()
        await(executeGate)
        executeFailure?.let { throw it }
        commands += command
        SimulationResult.Queued
    }

    override fun prepareReplacement(
        command: SimulationCommand.Reload
    ): PreparedRuntimeReplacement = tracked("prepare-replacement") {
        prepareReplacementStarted.countDown()
        await(prepareReplacementGate)
        prepareReplacementFailure?.let { throw it }
        commands += command
        FakePreparedReplacement(command.source, replacementGeneration)
    }

    override fun commitReplacement(
        prepared: PreparedRuntimeReplacement,
        expectedGeneration: Long
    ): RuntimeReplacementCommit = tracked("commit-replacement", split = false) {
        commitReplacementStarted.countDown()
        await(commitReplacementGate)
        commitReplacementFailure?.let { throw it }
        val fake = prepared as FakePreparedReplacement
        check(expectedGeneration == replacementGeneration)
        check(fake.expectedGeneration == replacementGeneration)
        replacementGeneration += 1L
        commitReplacementCount += 1
        RuntimeReplacementCommit(FakeRetiredRuntime(terminal = false))
    }

    override fun applyEnvironment(patch: EnvironmentPatch): ApplyResult = tracked("apply", split = false) {
        patches += patch
        (nextApplyResult ?: ApplyResult.Applied(patch.baseRevision + 1)).also { result ->
            environmentRevision = when (result) {
                is ApplyResult.Applied -> result.revision
                is ApplyResult.Rejected -> result.currentRevision
                is ApplyResult.Failure -> environmentRevision
            }
        }
    }

    override fun inspectCell(id: CellId): CellSnapshot? = tracked("inspect", split = false) {
        inspections += id
        null
    }

    override fun save(target: StoredSubstrateId): SaveResult = tracked("save") {
        saveStarted.countDown()
        await(saveGate)
        savedTargets += target
        SaveResult.Saved(target)
    }

    override fun detachForClose(): RetiredRuntime {
        if (detached) return object : RetiredRuntime {
            override fun close() = Unit
        }
        detached = true
        return FakeRetiredRuntime(terminal = true)
    }

    private fun await(gate: CountDownLatch?) {
        check(gate == null || gate.await(TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
            "Timed out waiting for fake runtime gate"
        }
    }

    private fun <T> tracked(name: String, split: Boolean = true, operation: () -> T): T {
        val active = activeCalls.incrementAndGet()
        maxConcurrentCalls.updateAndGet { previous -> maxOf(previous, active) }
        events += if (split) "$name:start" else name
        return try {
            operation()
        } finally {
            if (split) events += "$name:end"
            activeCalls.decrementAndGet()
        }
    }
}

private class FakeMicroscopeBackend :
    MicroscopeSimulationBackend,
    NativeMicroscopeRenderAccess {
    var surfaceCreatedCount = 0
    var surfaceDestroyedCount = 0
    var drawCount = 0
    val drawStarted = CountDownLatch(1)
    val secondSurfaceCreated = CountDownLatch(1)
    val inputSubmitStarted = CountDownLatch(1)
    val inputResyncEntered = CountDownLatch(1)
    var drawGate: CountDownLatch? = null
    var surfaceDestroyedEntered: CountDownLatch? = null
    var surfaceCreateGate: CountDownLatch? = null
    var inputSubmitGate: CountDownLatch? = null
    val lifecycleStates = mutableListOf<Boolean>()
    val controlChanges = mutableListOf<String>()
    var submittedInputCount = 0
    var activePointerCount = 0
    var resyncRequestCount = 0
    var completeInputResyncFailure: Throwable? = null
    var surfaceCreateFailure: Throwable? = null
    var authorizeRenderWorkerCount = 0
    var deactivateRenderSurfaceCount = 0
    var markRenderWorkerClosingCount = 0
    var finishRenderWorkerCloseCount = 0

    override val runtimePublicationSource: MicroscopeRuntimePublicationSource =
        object : MicroscopeRuntimePublicationSource {
            override val publicationIntervalMillis: Long = 50L

            override fun tryStartPoll(
                viewGeneration: Long,
                completion: (Long, MicroscopeRuntimePublicationEnvelope) -> Unit
            ): Boolean = false
        }

    override fun onSurfaceCreated() {
        surfaceCreatedCount += 1
        if (surfaceCreatedCount >= 2) secondSurfaceCreated.countDown()
        surfaceCreateFailure?.let { throw it }
        check(surfaceCreateGate?.await(TIMEOUT_SECONDS, TimeUnit.SECONDS) != false) {
            "Timed out waiting for fake surface-create gate"
        }
    }

    override fun authorizeRenderWorker(surfaceToken: Long, workerIdentity: Long): Long {
        authorizeRenderWorkerCount += 1
        return 1L
    }

    override fun borrowRenderEndpointForWorker():
        com.cytarch.cellularity.platform.render.microscope.NativeRenderEndpointOwner.Borrow =
        error("The fake renderer does not create native Vulkan workers")

    override fun activateRenderSurface(
        surfaceToken: Long,
        workerIdentity: Long,
        workerBindingGeneration: Long,
        platformSurfaceGeneration: Long,
        width: Int,
        height: Int
    ): Long = 0L

    override fun updateRenderSurfaceDimensions(
        surfaceToken: Long,
        width: Int,
        height: Int
    ) = Unit

    override fun deactivateRenderSurface(surfaceToken: Long) {
        deactivateRenderSurfaceCount += 1
    }

    override fun markRenderWorkerClosing(
        surfaceToken: Long,
        workerIdentity: Long,
        workerBindingGeneration: Long
    ) {
        markRenderWorkerClosingCount += 1
    }

    override fun finishRenderWorkerClose(
        surfaceToken: Long,
        workerIdentity: Long,
        workerBindingGeneration: Long
    ) {
        finishRenderWorkerCloseCount += 1
    }

    override fun onSurfaceDestroyed() {
        surfaceDestroyedEntered?.countDown()
        surfaceDestroyedCount += 1
    }

    override fun draw(surfaceToken: Long): Long {
        drawStarted.countDown()
        drawGate?.let { gate ->
            check(gate.await(TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                "Timed out waiting for fake renderer draw gate"
            }
        }
        drawCount += 1
        return presentedFrameResult()
    }

    override fun clearWithoutRendering(surfaceToken: Long): Long = presentedFrameResult()

    override fun enqueueSubstratePreviewCapture(
        requestId: Long,
        substratePreviewToken: Long
    ): com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest? = null

    override fun setLifecyclePaused(paused: Boolean) {
        lifecycleStates += paused
    }

    override fun publishPlacementGenome(genomeDraftBytes: ByteArray): Long = 1L

    override fun submitInputBatch(
        surfaceToken: Long,
        input: ByteBuffer,
        result: ByteBuffer,
        expectedPlacementGenomeRevision: Long
    ) {
        inputSubmitStarted.countDown()
        inputSubmitGate?.let { gate ->
            check(gate.await(TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                "Timed out waiting for fake input-submit gate"
            }
        }
        submittedInputCount += 1
        MicroscopeInputBatchLayout.writeIngressResult(
            result,
            0L,
            0L,
            MicroscopeInputIngressStatus.APPLIED
        )
    }

    override fun readPresentationMotionSnapshot(result: ByteBuffer) = Unit

    override fun requestInputResync(surfaceToken: Long): Boolean {
        inputResyncEntered.countDown()
        resyncRequestCount += 1
        return true
    }

    override fun activeInputEpoch(surfaceToken: Long): Long = 1L

    override fun completeInputResync(
        surfaceToken: Long,
        activePointersDrained: Boolean
    ): Long {
        completeInputResyncFailure?.let { throw it }
        return 0L
    }

    override fun activePointerGenerationCount(): Int = activePointerCount

    override fun setToolOrdinal(toolOrdinal: Int) {
        controlChanges += "tool=$toolOrdinal"
    }

    override fun setTemperatureMode(temperatureMode: Int) {
        controlChanges += "temperature=$temperatureMode"
    }

    override fun setSlowMotionSpeed(speed: Float) {
        controlChanges += "slowMotion=$speed"
    }

    override fun setIncubateTargetUps(targetUps: Double) {
        controlChanges += "targetUps=$targetUps"
    }

    override fun setPerformanceHudEnabled(enabled: Boolean) {
        controlChanges += "performanceHud=$enabled"
    }

    override fun selectedGenome(): GenomeDraft? = null
}

private fun AndroidSimulationGateway.createRenderAttachment(
    backend: FakeMicroscopeBackend,
    backendKind: RenderBackendKind = RenderBackendKind.OPEN_GL_ES
) = createRenderAttachment(backend, backend, backendKind)

private fun environment(revision: Long): EnvironmentSnapshot = EnvironmentSnapshot(
    revision = revision,
    addRandom = false,
    dieEdge = false,
    onlyTweakMutations = false,
    mutationOnlyAfterSplit = false,
    aging = true,
    mobileFood = false,
    gravityField = VectorFieldSnapshot(VectorFieldType.LINEAR_DIRECTIONAL, 0.0, 0.0),
    densityField = scalarField(),
    salinityField = scalarField(),
    foodLipidLayerField = scalarField(),
    radiation = 0.0,
    testMulticellularCoef = 1.0,
    testMutationCoef = 1.0,
    testFoodDecay = 1.0,
    testIdleMetabolismScale = 1.0,
    testFlagellocyteCostScale = 1.0,
    testPhagocyteEatSpeedScale = 1.0,
    viscosity = 0.0,
    frictionDynamic = 0.0,
    frictionStatic = 0.0,
    foodSpeed = 0.0,
    foodSize = 0.0,
    foodLumpiness = 0.0,
    foodLumpSize = 0.0,
    foodLipidLayer = 0.0,
    salinity = 0.0,
    lightIntensity = 0.0,
    lightRange = 0.0,
    lightSpeed = 0.0,
    gravity = 0.0,
    density = 0.0,
    densityGradient = 0.0,
    nitrates = 0.0,
    genePool = emptyList(),
    mutationWeights = emptyList()
)

private fun scalarField() = ScalarFieldSnapshot(
    type = ScalarFieldType.CONSTANT,
    base = 0.0,
    strength = 0.0,
    angle = 0.0,
    offset = 0.0,
    softness = 0.0
)
