package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.platform.render.preview.GenomePreviewAcknowledgment
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class GenomePreviewRevealCoordinatorTest {
    @Test
    fun glesKeepsTheEstablishedUngatedPresentationPath() {
        val coordinator = coordinator(enabled = false)

        assertFalse(coordinator.coverVisible)
        assertFalse(coordinator.onSurfaceAvailable().coverVisible)
        assertFalse(coordinator.onPlatformContentCurrent(true).readAcknowledgment)
    }

    @Test
    fun initialTargetsAreStagedBeforeSurfaceAndTrackedAfterAttach() {
        val coordinator = coordinator()

        assertFalse(coordinator.onContentTargetAccepted(2).requestTrackedFrame)
        assertFalse(coordinator.onViewTargetAccepted(3).requestTrackedFrame)

        val attached = coordinator.onSurfaceAvailable()
        assertTrue(attached.coverVisible)
        assertTrue(attached.requestTrackedFrame)
    }

    @Test
    fun matchingPresentedTupleRevealsCurrentContent() {
        val coordinator = readyCoordinator()

        val current = coordinator.onPlatformContentCurrent(true)
        assertTrue(current.readAcknowledgment)

        val revealed = coordinator.observeAcknowledgment(acknowledgment())
        assertFalse(revealed.coverVisible)
        assertFalse(revealed.requestTrackedFrame)
    }

    @Test
    fun geneSelectionAfterRevealRequestsAcknowledgmentWithoutHidingPreview() {
        val coordinator = revealedCoordinator()

        val selection = coordinator.onInteractionTargetAccepted(5)
        assertTrue(selection.requestTrackedFrame)
        assertFalse(selection.coverVisible)
        assertFalse(coordinator.onPlatformContentCurrent(false).coverVisible)
        assertTrue(coordinator.onPlatformContentCurrent(true).readAcknowledgment)

        val presented = coordinator.observeAcknowledgment(
            acknowledgment(presentedInteraction = 5).copy(
                selectionToken = 5,
                selectedGeneIndex = 1
            )
        )
        assertFalse(presented.coverVisible)
        assertFalse(presented.requestTrackedFrame)
    }

    @Test
    fun olderSelectionPresentationRetriesWithoutHidingVisiblePreview() {
        val coordinator = revealedCoordinator()
        coordinator.onInteractionTargetAccepted(5)
        coordinator.onPlatformContentCurrent(true)

        val stale = coordinator.observeAcknowledgment(acknowledgment())
        assertTrue(stale.requestTrackedFrame)
        assertFalse(stale.coverVisible)

        coordinator.onPlatformContentCurrent(true)
        val current = coordinator.observeAcknowledgment(
            acknowledgment(presentedInteraction = 5)
        )
        assertFalse(current.requestTrackedFrame)
        assertFalse(current.coverVisible)
    }

    @Test
    fun latestGeneSelectionSupersedesAnUnfinishedPresentation() {
        val coordinator = revealedCoordinator()
        assertTrue(coordinator.onInteractionTargetAccepted(5).requestTrackedFrame)
        assertTrue(coordinator.onInteractionTargetAccepted(6).requestTrackedFrame)
        coordinator.onPlatformContentCurrent(true)

        val previous = coordinator.observeAcknowledgment(
            acknowledgment(presentedInteraction = 5)
        )
        assertTrue(previous.requestTrackedFrame)
        assertFalse(previous.coverVisible)

        coordinator.onPlatformContentCurrent(true)
        val latest = coordinator.observeAcknowledgment(
            acknowledgment(presentedInteraction = 6)
        )
        assertFalse(latest.requestTrackedFrame)
        assertFalse(latest.coverVisible)
    }

    @Test
    fun staleRevisionOrEndpointStaysCoveredAndRequestsANewerPresent() {
        val coordinator = readyCoordinator()
        coordinator.onPlatformContentCurrent(true)

        val stale = coordinator.observeAcknowledgment(
            acknowledgment(endpoint = 8, presentedView = 2)
        )

        assertTrue(stale.coverVisible)
        assertTrue(stale.requestTrackedFrame)
    }

    @Test
    fun targetSupersededDuringFirstFrameWaitRejectsTheOlderFrame() {
        val coordinator = readyCoordinator()
        assertTrue(coordinator.onViewTargetAccepted(4).requestTrackedFrame)
        coordinator.onPlatformContentCurrent(true)

        val oldFrame = coordinator.observeAcknowledgment(
            acknowledgment(presentedView = 3)
        )
        assertTrue(oldFrame.coverVisible)
        assertTrue(oldFrame.requestTrackedFrame)

        coordinator.onPlatformContentCurrent(true)
        val newestFrame = coordinator.observeAcknowledgment(
            acknowledgment(presentedView = 4)
        )
        assertFalse(newestFrame.coverVisible)
    }

    @Test
    fun resizeAndSurfaceLossRestoreTheCover() {
        val coordinator = readyCoordinator()
        coordinator.onPlatformContentCurrent(true)
        coordinator.observeAcknowledgment(acknowledgment())
        assertFalse(coordinator.coverVisible)

        assertTrue(coordinator.onSurfaceChanged().coverVisible)
        assertTrue(coordinator.onSurfaceUnavailable().coverVisible)
    }

    @Test
    fun releaseMakesLateCallbacksInert() {
        val coordinator = readyCoordinator()
        coordinator.release()

        val lateCurrent = coordinator.onPlatformContentCurrent(true)
        val lateSurface = coordinator.onSurfaceAvailable()

        assertTrue(lateCurrent.coverVisible)
        assertFalse(lateCurrent.readAcknowledgment)
        assertFalse(lateCurrent.requestTrackedFrame)
        assertFalse(lateSurface.requestTrackedFrame)
    }

    @Test
    fun stalePresentationRetriesAreBoundedAndFailClosed() {
        val coordinator = GenomePreviewRevealCoordinator(
            endpointGeneration = 9,
            enabled = true,
            maximumRetryCount = 2
        )
        coordinator.onContentTargetAccepted(2)
        coordinator.onViewTargetAccepted(3)
        coordinator.onSurfaceAvailable()

        repeat(2) {
            coordinator.onPlatformContentCurrent(true)
            assertTrue(
                coordinator.observeAcknowledgment(
                    acknowledgment(presentedContent = 1)
                ).requestTrackedFrame
            )
        }
        coordinator.onPlatformContentCurrent(true)
        val exhausted = coordinator.observeAcknowledgment(
            acknowledgment(presentedContent = 1)
        )
        assertTrue(exhausted.coverVisible)
        assertFalse(exhausted.requestTrackedFrame)
    }

    @Test
    fun oneHundredAttachRevealDetachCyclesDoNotLeakState() {
        val coordinator = coordinator()
        coordinator.onContentTargetAccepted(2)
        coordinator.onViewTargetAccepted(3)
        coordinator.onInteractionTargetAccepted(4)

        repeat(100) { cycle ->
            assertTrue(coordinator.onSurfaceAvailable().coverVisible)
            assertTrue(coordinator.onPlatformContentCurrent(true).readAcknowledgment)
            assertFalse(
                coordinator.observeAcknowledgment(
                    acknowledgment(attachment = cycle.toLong() + 1L)
                ).coverVisible
            )
            assertTrue(coordinator.onSurfaceUnavailable().coverVisible)
        }
    }

    @Test
    fun coverDoesNotConsumeInputAndCaptureDependsOnlyOnRenderAvailability() {
        assertFalse(GenomePreviewVisualContract.COVER_CLICKABLE)
        assertFalse(GenomePreviewVisualContract.COVER_ENABLED)
        assertTrue(
            isGenomePreviewCaptureAvailable(
                released = false,
                renderSurfaceAvailable = true,
                supportsOffscreenCapture = true
            )
        )
        assertFalse(
            isGenomePreviewCaptureAvailable(
                released = false,
                renderSurfaceAvailable = false,
                supportsOffscreenCapture = true
            )
        )
    }

    @Test
    fun viewportRetentionAcceptsOnlyAValidNativeAcknowledgment() {
        assertArrayEquals(
            floatArrayOf(1.25f, -2.5f, 12.0f),
            viewportStateFromAcknowledgment(
                acknowledgment().copy(
                    viewportRevision = 7,
                    cameraX = 1.25f,
                    cameraY = -2.5f,
                    zoom = 12.0f
                )
            ),
            0.0f
        )
        assertNull(
            viewportStateFromAcknowledgment(
                acknowledgment().copy(viewportRevision = 0)
            )
        )
        assertNull(
            viewportStateFromAcknowledgment(
                acknowledgment().copy(zoom = Float.NaN)
            )
        )
    }

    private fun revealedCoordinator(): GenomePreviewRevealCoordinator =
        readyCoordinator().also {
            it.onPlatformContentCurrent(true)
            it.observeAcknowledgment(acknowledgment())
        }

    private fun readyCoordinator(): GenomePreviewRevealCoordinator =
        coordinator().also {
            it.onContentTargetAccepted(2)
            it.onViewTargetAccepted(3)
            it.onInteractionTargetAccepted(4)
            it.onSurfaceAvailable()
        }

    private fun coordinator(enabled: Boolean = true) =
        GenomePreviewRevealCoordinator(
            endpointGeneration = 9,
            enabled = enabled
        )

    private fun acknowledgment(
        endpoint: Long = 9,
        attachment: Long = 1,
        presentedContent: Long = 2,
        presentedView: Long = 3,
        presentedInteraction: Long = 4
    ) = GenomePreviewAcknowledgment(
        endpointGeneration = endpoint,
        attachmentGeneration = attachment,
        appliedContentRevision = presentedContent,
        appliedViewRevision = presentedView,
        presentedContentRevision = presentedContent,
        presentedViewRevision = presentedView,
        presentedInteractionToken = presentedInteraction,
        cellCountContentRevision = presentedContent,
        viewportRevision = 1,
        selectionToken = 0,
        previewCellCount = 1,
        selectedGeneIndex = -1,
        cameraX = 0.0f,
        cameraY = 0.0f,
        zoom = 8.0f
    )
}
