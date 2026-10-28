/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.simulation.SimulationContentState
import com.cytarch.cellularity.shared.ui.play.PlayAction
import com.cytarch.cellularity.shared.ui.play.PlayDialog
import com.cytarch.cellularity.shared.ui.play.PlayLaunchSource
import com.cytarch.cellularity.shared.ui.play.PlayOperation
import com.cytarch.cellularity.shared.ui.play.PlayPhase
import com.cytarch.cellularity.shared.ui.play.PlayPlateAction
import com.cytarch.cellularity.shared.ui.play.PlayState
import com.cytarch.cellularity.shared.ui.play.PlayTab

/**
 * The desktop side of the play screen's state machine.
 *
 * The shared store exists to co-ordinate a simulation session that is opened, saved and
 * reloaded through platform operations, because on a phone each of those is a lifecycle
 * event as much as a command. The desktop session is opened once and stays open for as
 * long as the screen is shown, so those operations reduce to what the renderer can
 * actually do: reset the world, and report that the rest is not available yet.
 *
 * What matters is that the interface is driven by the same [PlayState] the phone uses, so
 * the screen behaves identically and the tab and dialog rules are not restated here. Only
 * the operations differ, and they differ honestly -- a save that cannot happen says so
 * rather than appearing to succeed.
 */
class DesktopPlayStore(
    source: PlayLaunchSource,
    private val resetWorld: () -> Unit
) {
    var state by mutableStateOf(PlayState.initial(source))
        private set

    /** The last failure, for the interface to show. Cleared when a dialog is dismissed. */
    var failure by mutableStateOf<CellularityError?>(null)
        private set

    fun dispatch(action: PlayAction) {
        when (action) {
            // The session is already open by the time this screen exists, so the only
            // work left is to say so.
            PlayAction.Start -> state = state.copy(phase = PlayPhase.READY)

            is PlayAction.SelectTab -> state = state.copy(selectedTab = action.tab)

            PlayAction.RequestPlateActions ->
                state = state.copy(dialog = PlayDialog.PLATE_ACTIONS)

            PlayAction.DismissDialog -> state = state.copy(dialog = PlayDialog.NONE)

            PlayAction.BackRequested ->
                state = state.copy(dialog = PlayDialog.CONFIRM_LEAVE)

            is PlayAction.SelectPlateAction -> when (action.action) {
                PlayPlateAction.STERILIZE -> {
                    // Sterilising asks first when the substrate is configured to ask,
                    // which is the same rule the phone applies.
                    state = if (action.confirmationRequired) {
                        state.copy(dialog = PlayDialog.CONFIRM_RESET)
                    } else {
                        sterilize()
                    }
                }

                // Saving a substrate and reloading it both need the native save path,
                // which the desktop bridge does not expose yet. Reporting the failure is
                // the honest outcome; pretending to save would be worse than saying no.
                PlayPlateAction.SAVE ->
                    state = state.copy(dialog = PlayDialog.SAVE_PLATE)

                PlayPlateAction.RELOAD ->
                    state = state.copy(dialog = PlayDialog.CONFIRM_RELOAD)
            }

            PlayAction.ConfirmReset -> {
                sterilize()
                state = state.copy(dialog = PlayDialog.NONE)
            }

            PlayAction.ConfirmSave -> {
                failure = CellularityError(
                    CellularityErrorCode.UNSUPPORTED_FEATURE,
                    "Saving a substrate is not available in the desktop build yet."
                )
                state = state.copy(dialog = PlayDialog.NONE)
            }

            PlayAction.ConfirmReload -> {
                failure = CellularityError(
                    CellularityErrorCode.UNSUPPORTED_FEATURE,
                    "Reloading a substrate is not available in the desktop build yet."
                )
                state = state.copy(dialog = PlayDialog.NONE)
            }

            is PlayAction.SaveNameChanged -> state = state.copy(saveName = action.value)

            // The remaining actions are reports from the platform session, which this
            // store does not own. They carry no information the screen needs.
            else -> Unit
        }
    }

    private fun sterilize(): PlayState {
        resetWorld()
        // The world is empty now, so whatever was persisted no longer describes it. The
        // revision still advances, because the content genuinely changed.
        return state.copy(
            dialog = PlayDialog.NONE,
            operation = PlayOperation.NONE,
            contentState = SimulationContentState(
                revision = state.contentState.revision + 1,
                persistedRevision = null
            )
        )
    }

    fun clearFailure() {
        failure = null
    }
}
