package com.cytarch.cellularity.platform.render.microscope

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class MicroscopeInputStreamStateTest {
    @Test
    fun staleGestureIsConsumedUntilANewEpochAndFreshDownArrive() {
        val state = MicroscopeInputStreamState()

        assertEquals(
            MicroscopePointerEventAdmission.WAITING_FOR_FRESH_DOWN,
            state.admitPointerEvent(isDown = false)
        )
        assertTrue(state.acceptEpoch(7L))
        assertEquals(
            MicroscopePointerEventAdmission.WAITING_FOR_FRESH_DOWN,
            state.admitPointerEvent(isDown = false)
        )
        assertEquals(
            MicroscopePointerEventAdmission.FRESH_DOWN,
            state.admitPointerEvent(isDown = true)
        )
        assertEquals(
            MicroscopePointerEventAdmission.ACTIVE,
            state.admitPointerEvent(isDown = false)
        )

        assertTrue(state.beginResync())
        assertFalse(state.beginResync())
        assertEquals(0L, state.inputEpoch)
        assertEquals(
            MicroscopePointerEventAdmission.WAITING_FOR_FRESH_DOWN,
            state.admitPointerEvent(isDown = false)
        )
        assertTrue(state.acceptEpoch(8L))
        assertEquals(
            MicroscopePointerEventAdmission.WAITING_FOR_FRESH_DOWN,
            state.admitPointerEvent(isDown = false)
        )
        assertEquals(
            MicroscopePointerEventAdmission.FRESH_DOWN,
            state.admitPointerEvent(isDown = true)
        )
    }

    @Test
    fun detachedDetectorBatchesUseOneMonotonicSequenceAndExhaustFailClosed() {
        val state = MicroscopeInputStreamState(initialBatchSequence = -1L)
        assertTrue(state.acceptEpoch(3L))
        state.admitPointerEvent(isDown = true)

        assertEquals(-1L, state.takeBatchSequence())
        assertNull(state.takeBatchSequence())
        assertTrue(state.terminal)
        assertEquals(
            MicroscopePointerEventAdmission.TERMINAL,
            state.admitPointerEvent(isDown = true)
        )
    }

    @Test
    fun everyIngressStatusHasAnExplicitRecoveryPolicy() {
        assertEquals(
            MicroscopeInputRecoveryAction.APPLY,
            MicroscopeInputIngressStatus.APPLIED.recoveryAction()
        )
        listOf(
            MicroscopeInputIngressStatus.MALFORMED,
            MicroscopeInputIngressStatus.STALE_INPUT_EPOCH,
            MicroscopeInputIngressStatus.ATTACHMENT_UNAVAILABLE,
            MicroscopeInputIngressStatus.INACTIVE_SURFACE
        ).forEach { status ->
            assertEquals(
                MicroscopeInputRecoveryAction.REQUEST_RESYNC,
                status.recoveryAction()
            )
        }
        assertEquals(
            MicroscopeInputRecoveryAction.WAIT_FOR_RESYNC,
            MicroscopeInputIngressStatus.RESYNCING.recoveryAction()
        )
        listOf(
            MicroscopeInputIngressStatus.CLOSING,
            MicroscopeInputIngressStatus.INTERNAL_FAILURE
        ).forEach { status ->
            assertEquals(MicroscopeInputRecoveryAction.CLOSE, status.recoveryAction())
        }
    }
}
