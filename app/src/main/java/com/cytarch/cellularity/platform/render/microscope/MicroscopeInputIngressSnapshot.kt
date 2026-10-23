package com.cytarch.cellularity.platform.render.microscope

internal enum class MicroscopeInputIngressState {
    ACCEPTING,
    RESYNCING,
    INACTIVE_SURFACE,
    CLOSING
}

internal enum class MicroscopePointerEventAdmission {
    TERMINAL,
    WAITING_FOR_FRESH_DOWN,
    FRESH_DOWN,
    ACTIVE
}

internal enum class MicroscopeInputRecoveryAction {
    APPLY,
    REQUEST_RESYNC,
    WAIT_FOR_RESYNC,
    CLOSE
}

internal fun MicroscopeInputIngressStatus.recoveryAction(): MicroscopeInputRecoveryAction =
    when (this) {
        MicroscopeInputIngressStatus.APPLIED -> MicroscopeInputRecoveryAction.APPLY
        MicroscopeInputIngressStatus.STALE_INPUT_EPOCH,
        MicroscopeInputIngressStatus.ATTACHMENT_UNAVAILABLE,
        MicroscopeInputIngressStatus.INACTIVE_SURFACE,
        MicroscopeInputIngressStatus.MALFORMED ->
            MicroscopeInputRecoveryAction.REQUEST_RESYNC
        MicroscopeInputIngressStatus.RESYNCING ->
            MicroscopeInputRecoveryAction.WAIT_FOR_RESYNC
        MicroscopeInputIngressStatus.CLOSING,
        MicroscopeInputIngressStatus.INTERNAL_FAILURE ->
            MicroscopeInputRecoveryAction.CLOSE
    }

/** Pure epoch/gesture gate shared by synchronous and detached detector batches. */
internal class MicroscopeInputStreamState(
    initialBatchSequence: Long = 1L
) {
    var inputEpoch: Long = 0L
        private set

    var terminal: Boolean = false
        private set

    var requiresFreshDown: Boolean = true
        private set

    private var nextBatchSequence = initialBatchSequence

    fun acceptEpoch(epoch: Long): Boolean {
        require(epoch != 0L) { "A zero input epoch cannot accept events" }
        if (terminal || epoch == inputEpoch) return false
        inputEpoch = epoch
        requiresFreshDown = true
        return true
    }

    fun admitPointerEvent(isDown: Boolean): MicroscopePointerEventAdmission {
        if (terminal) return MicroscopePointerEventAdmission.TERMINAL
        if (requiresFreshDown) {
            if (!isDown || inputEpoch == 0L) {
                return MicroscopePointerEventAdmission.WAITING_FOR_FRESH_DOWN
            }
            requiresFreshDown = false
            return MicroscopePointerEventAdmission.FRESH_DOWN
        }
        return MicroscopePointerEventAdmission.ACTIVE
    }

    fun beginResync(): Boolean {
        if (terminal) return false
        val changed = inputEpoch != 0L || !requiresFreshDown
        inputEpoch = 0L
        requiresFreshDown = true
        return changed
    }

    fun close() {
        terminal = true
        inputEpoch = 0L
        requiresFreshDown = true
    }

    fun takeBatchSequence(): Long? {
        if (terminal) return null
        val sequence = nextBatchSequence
        if (sequence == 0L) {
            close()
            return null
        }
        nextBatchSequence += 1L
        return sequence
    }
}

internal data class MicroscopeInputIngressSnapshot(
    val state: MicroscopeInputIngressState,
    val inputEpoch: Long
) {
    val accepting: Boolean
        get() = state == MicroscopeInputIngressState.ACCEPTING && inputEpoch != 0L

    companion object {
        val INACTIVE = MicroscopeInputIngressSnapshot(
            MicroscopeInputIngressState.INACTIVE_SURFACE,
            0L
        )

        val CLOSING = MicroscopeInputIngressSnapshot(
            MicroscopeInputIngressState.CLOSING,
            0L
        )
    }
}
