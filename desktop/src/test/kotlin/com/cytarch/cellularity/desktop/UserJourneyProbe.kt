/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.simulation.SimulationContentState
import com.cytarch.cellularity.shared.ui.play.PlayAction
import com.cytarch.cellularity.shared.ui.play.PlayDialog
import com.cytarch.cellularity.shared.ui.play.PlayEffect
import com.cytarch.cellularity.shared.ui.play.PlayLaunchSource
import com.cytarch.cellularity.shared.ui.play.PlayOpenResult
import com.cytarch.cellularity.shared.ui.play.PlayPhase
import com.cytarch.cellularity.shared.ui.play.PlayPlateAction
import com.cytarch.cellularity.shared.ui.play.PlayRestoredUiState
import com.cytarch.cellularity.shared.ui.play.PlaySaveResult
import com.cytarch.cellularity.shared.ui.play.PlaySessionCommandResult
import com.cytarch.cellularity.shared.ui.play.PlaySessionOperations
import com.cytarch.cellularity.shared.ui.play.PlayStore
import com.cytarch.cellularity.shared.ui.play.PlayTab
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking

/**
 * Walks the whole user journey and reports the state after every step.
 *
 * Every screen involved has been photographed and every part of the input path has been measured,
 * but the journey itself -- opening, tabs, dialog, saving, leaving -- is a sequence, and a sequence
 * can be wrong while each of its steps is right. This drives the same state machine the window
 * drives, in the same order a person would, and says what the screen would be showing at each point.
 *
 * It is the *shared* machine, not a desktop copy of it: the screen was moved onto the phone's store
 * so that a save, a reset and the dirty state that follows them behave here because they are the
 * same sequence, and a probe driving a different store than the application would prove nothing
 * about the application. What stands in is the session behind it: the machine's own job is the
 * sequence, and the native side of these operations is measured where it belongs -- opening a plate
 * and saving one are both measured by the probes that run a real session.
 *
 * Run through `:desktop:runUserJourney`.
 */
fun main() {
    val operations = JourneyOperations()
    val store = PlayStore(
        source = PlayLaunchSource.New(SimulationContract.defaultConfiguration),
        operations = operations,
        // The screen opens on the plate rather than on the panel that describes it, and the journey
        // has to start where the screen starts or it is walking somebody else's path.
        restoredUi = PlayRestoredUiState(selectedTab = PlayTab.MICROSCOPE)
    )
    var failures = 0

    fun step(what: String, expected: String, actual: String, matches: Boolean) {
        val mark = if (matches) "ok  " else "FAIL"
        if (!matches) failures++
        println("JOURNEY $mark $what -> expected $expected, was $actual")
    }

    /** The next effect the machine emits, or null when it emits none. */
    fun nextEffect(): PlayEffect? = runBlocking {
        kotlinx.coroutines.withTimeoutOrNull(EFFECT_TIMEOUT_MILLIS) { store.effects.first() }
    }

    runBlocking { store.dispatch(PlayAction.Start) }
    // The machine opens a session before it is ready, which is what the phone shows as loading.
    step(
        "opening a plate is announced while it lasts",
        "READY",
        store.state.value.phase.name,
        store.state.value.phase == PlayPhase.READY
    )
    step(
        "the machine opened the session it was given",
        "1",
        operations.opens.toString(),
        operations.opens == 1
    )

    step(
        "the experiment opens on the plate",
        "MICROSCOPE",
        store.state.value.selectedTab.name,
        store.state.value.selectedTab == PlayTab.MICROSCOPE
    )
    step(
        "no dialog is showing",
        "NONE",
        store.state.value.dialog.name,
        store.state.value.dialog == PlayDialog.NONE
    )

    runBlocking { store.dispatch(PlayAction.SelectTab(PlayTab.SUBSTRATE)) }
    step(
        "the substrate tab is selected",
        "SUBSTRATE",
        store.state.value.selectedTab.name,
        store.state.value.selectedTab == PlayTab.SUBSTRATE
    )

    runBlocking { store.dispatch(PlayAction.SelectTab(PlayTab.MICROSCOPE)) }
    step(
        "the microscope tab is selected again",
        "MICROSCOPE",
        store.state.value.selectedTab.name,
        store.state.value.selectedTab == PlayTab.MICROSCOPE
    )

    runBlocking { store.dispatch(PlayAction.RequestPlateActions) }
    step(
        "the plate actions dialog opens over the world",
        "PLATE_ACTIONS",
        store.state.value.dialog.name,
        store.state.value.dialog == PlayDialog.PLATE_ACTIONS
    )

    runBlocking { store.dispatch(PlayAction.DismissDialog) }
    step(
        "the dialog is dismissed and the world is visible again",
        "NONE",
        store.state.value.dialog.name,
        store.state.value.dialog == PlayDialog.NONE
    )

    // Saving is walked through the dialog the person sees, because the interesting part is not that
    // the file is written -- the save probe measures that -- but that the machine asks for a name,
    // refuses one it cannot use, and answers with a notice a person can read.
    runBlocking {
        // Saving is reached the way a person reaches it: the plate's menu, then the action in it.
        store.dispatch(PlayAction.RequestPlateActions)
        store.dispatch(
            PlayAction.SelectPlateAction(PlayPlateAction.SAVE, confirmationRequired = false)
        )
    }
    step(
        "saving asks for a name first",
        "SAVE_PLATE",
        store.state.value.dialog.name,
        store.state.value.dialog == PlayDialog.SAVE_PLATE
    )
    runBlocking { store.dispatch(PlayAction.SaveNameChanged("  ")) }
    runBlocking { store.dispatch(PlayAction.ConfirmSave) }
    val refusedName = nextEffect()
    step(
        "a name that cannot be used is refused with words",
        "BAD_NAME",
        noticeFeedbackOf(refusedName) ?: "no notice",
        noticeFeedbackOf(refusedName) == "BAD_NAME"
    )
    step(
        "a refused name is not saved under",
        "0",
        operations.saves.toString(),
        operations.saves == 0
    )

    runBlocking { store.dispatch(PlayAction.SaveNameChanged("Journey Plate")) }
    runBlocking { store.dispatch(PlayAction.ConfirmSave) }
    val savedNotice = nextEffect()
    step(
        "a good name saves the plate",
        "SAVED",
        noticeFeedbackOf(savedNotice) ?: "no notice",
        noticeFeedbackOf(savedNotice) == "SAVED"
    )
    step(
        "the machine saved under the name that was typed",
        "Journey Plate",
        operations.lastSaved?.value ?: "none",
        operations.lastSaved?.value == "Journey Plate"
    )
    step(
        "a saved plate is no longer dirty",
        "false",
        store.state.value.isDirty.toString(),
        !store.state.value.isDirty
    )

    // Leaving a plate that has just been saved asks nothing: there is nothing to lose, and a warning
    // over a saved run is a warning people learn to click through. The step is here because that is
    // the whole meaning of the save having succeeded -- the dirty state is the machine's, and this is
    // where it is read.
    runBlocking { store.dispatch(PlayAction.BackRequested) }
    step(
        "a saved plate can be left without a warning",
        "NONE",
        store.state.value.dialog.name,
        store.state.value.dialog == PlayDialog.NONE
    )

    // Sterilising changes the world again, so now there is something to lose and the machine asks.
    runBlocking {
        store.dispatch(PlayAction.RequestPlateActions)
        store.dispatch(
            PlayAction.SelectPlateAction(PlayPlateAction.STERILIZE, confirmationRequired = false)
        )
        // The world's own report that the reset happened, which the screen forwards from the
        // gateway. Without it the machine keeps the change pending -- and a pending change is what
        // makes the plate's menu stop answering, which is the fault this step exists to catch.
        store.dispatch(
            PlayAction.ContentStateObserved(
                SimulationContentState(revision = 1, persistedRevision = null)
            )
        )
    }
    step(
        "sterilising makes the plate differ from its file",
        "true",
        store.state.value.isDirty.toString(),
        store.state.value.isDirty
    )
    runBlocking { store.dispatch(PlayAction.BackRequested) }
    step(
        "leaving asks before discarding",
        "CONFIRM_LEAVE",
        store.state.value.dialog.name,
        store.state.value.dialog == PlayDialog.CONFIRM_LEAVE
    )

    runBlocking { store.dispatch(PlayAction.DismissDialog) }
    step(
        "refusing to leave returns to the world",
        "NONE",
        store.state.value.dialog.name,
        store.state.value.dialog == PlayDialog.NONE
    )

    runBlocking { store.dispatch(PlayAction.BackRequested) }
    runBlocking { store.dispatch(PlayAction.ConfirmLeave) }
    val leaving = nextEffect()
    step(
        "confirming leaves the experiment",
        "Finish",
        leaving?.let { it::class.simpleName } ?: "no effect",
        leaving is PlayEffect.Finish
    )

    println("JOURNEY steps failed=$failures")
    System.out.flush()
    kotlin.system.exitProcess(if (failures == 0) 0 else 4)
}

/** The plate action the journey saves through, named once so the step reads as what it is. */
private fun noticeFeedbackOf(effect: PlayEffect?): String? =
    (effect as? PlayEffect.ShowNotice)?.notice?.feedback?.name

/**
 * A session that answers without a world.
 *
 * The machine's own job is the sequence of states and notices it produces, and that sequence does
 * not depend on what the world behind it contains -- so this stands in for the session and counts
 * what it was asked to do. The world itself is measured by the probes that open and save real
 * plates, where a stand-in would be the one thing that could hide a fault.
 */
private class JourneyOperations : PlaySessionOperations {
    var opens = 0
        private set
    var saves = 0
        private set
    var lastSaved: StoredSubstrateId? = null
        private set

    override suspend fun open(source: PlayLaunchSource): PlayOpenResult {
        opens++
        return PlayOpenResult.Opened(SimulationContentState.newWorld())
    }

    override suspend fun substrateExists(name: String): StorageResult<Boolean> =
        StorageResult.Success(false)

    override suspend fun save(target: StoredSubstrateId): PlaySaveResult {
        saves++
        lastSaved = target
        return PlaySaveResult.Saved(target, SimulationContentState(revision = 0, persistedRevision = 0))
    }

    override suspend fun reload(source: StoredSubstrateId): PlaySessionCommandResult =
        PlaySessionCommandResult.Failure(
            com.cytarch.cellularity.shared.ui.play.PlayFailureCause.Storage(
                StorageError(StorageErrorCode.NOT_FOUND, "the journey does not reload")
            )
        )

    override suspend fun reset(): PlaySessionCommandResult =
        PlaySessionCommandResult.Accepted(SimulationContentState.newWorld())
}

/** How long an effect is waited for before the step is reported as unanswered. */
private const val EFFECT_TIMEOUT_MILLIS = 2_000L
