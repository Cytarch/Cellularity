/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.nativebridge.SubstratePreviewCapture
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.simulation.SimulationCommand
import com.cytarch.cellularity.shared.simulation.SimulationResult
import com.cytarch.cellularity.shared.ui.play.PlayFailureCause
import com.cytarch.cellularity.shared.ui.play.PlayLaunchSource
import com.cytarch.cellularity.shared.ui.play.PlayOpenResult
import com.cytarch.cellularity.shared.ui.play.PlaySaveResult
import com.cytarch.cellularity.shared.ui.play.PlaySessionCommandResult
import com.cytarch.cellularity.shared.ui.play.PlaySessionOperations
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CompletableDeferred

/**
 * What the desktop does when the shared play machine asks it to open, save or reset.
 *
 * The play screen is the phone's screen: the tabs, the dialogs, the dirty state and the wording of
 * every answer come from the shared machine, and this is the only part of it the desktop replaces.
 * That is the same seam the phone fills -- [PlaySessionOperations] exists for exactly this -- so a
 * save behaves identically on both platforms because it *is* the same sequence, not because two
 * implementations were kept in step by hand.
 *
 * The session is not opened here. On a desktop the world is created by the surface that draws it,
 * which happens when the screen composes and before any of this is asked anything, so the gateway
 * is handed in when it exists and [open] waits for it. Waiting is the honest answer to "open me a
 * plate": the machine's OPENING phase lasts exactly as long as the renderer takes, which is what
 * the person sees on the phone too.
 *
 * Reloading is the one operation the desktop does not have. It answers so rather than pretending,
 * and the machine turns that into the same notice the phone shows when a reload fails -- the
 * difference between the two platforms is then visible instead of silent.
 */
class DesktopPlaySessionOperations(
    private val fileRepository: DesktopCellularityFileRepository,
    /**
     * The plate this session was opened from, when it was opened from one.
     *
     * It decides the one file a save may replace: the plate's own. Everything else is refused as a
     * name that already exists, which is the phone's rule.
     */
    private val openedPlate: StoredSubstrateId?,
    /** Where a saved plate's picture is taken, which is renderer work. */
    private val previewCapture: SubstratePreviewCapture
) : PlaySessionOperations {
    private val firstSession = CompletableDeferred<DesktopSimulationGateway>()
    private val currentSession = AtomicReference<DesktopSimulationGateway?>(null)

    /**
     * Called by the screen whenever its renderer has made the session this screen is showing.
     * A stored plate replaces the provisional surface after resolving its path, so the newest
     * attachment must replace the gateway used by subsequent operations rather than being dropped.
     */
    fun attachGateway(gateway: DesktopSimulationGateway) {
        currentSession.set(gateway)
        firstSession.complete(gateway)
    }

    private suspend fun awaitCurrentSession(): DesktopSimulationGateway {
        firstSession.await()
        return checkNotNull(currentSession.get())
    }

    override suspend fun open(source: PlayLaunchSource): PlayOpenResult {
        val gateway = awaitCurrentSession()
        return PlayOpenResult.Opened(gateway.contentState.value)
    }

    override suspend fun substrateExists(name: String): StorageResult<Boolean> =
        fileRepository.substrateExists(name)

    override suspend fun save(target: StoredSubstrateId): PlaySaveResult {
        val gateway = awaitCurrentSession()
        return when (
            val result = gateway.savePlate(
                id = target,
                replaceExisting = target == openedPlate
            )
        ) {
            is StorageResult.Success -> PlaySaveResult.Saved(
                target = target,
                content = gateway.contentState.value
            )

            // The storage outcome itself travels on, because the notice the person sees is chosen
            // by it: "that name is taken" and "the disk refused it" are different sentences about
            // different problems, and collapsing them into one is what makes a save unexplainable.
            is StorageResult.Failure -> PlaySaveResult.Failure(
                PlayFailureCause.Storage(result.error)
            )
        }
    }

    override suspend fun reload(source: StoredSubstrateId): PlaySessionCommandResult {
        val gateway = awaitCurrentSession()
        return when (val result = gateway.execute(SimulationCommand.Reload(source))) {
            is SimulationResult.Failure -> PlaySessionCommandResult.Failure(
                PlayFailureCause.Simulation(result.error)
            )

            SimulationResult.Queued -> PlaySessionCommandResult.Accepted(
                gateway.contentState.value
            )
        }
    }

    override suspend fun reset(): PlaySessionCommandResult {
        val gateway = awaitCurrentSession()
        // Acceptance belongs to the state on which the command was issued. The gateway publishes
        // the post-reset revision during execute; returning that newer snapshot here would make
        // the play machine wait for a later observation that this desktop gateway never emits.
        // Its collector is blocked by the play store's dispatch mutex until this result is
        // reduced, so the already-published revision is delivered afterwards and releases the lock.
        val contentBeforeReset = gateway.contentState.value
        return when (val result = gateway.execute(SimulationCommand.Reset)) {
            SimulationResult.Queued -> PlaySessionCommandResult.Accepted(contentBeforeReset)

            is SimulationResult.Failure -> PlaySessionCommandResult.Failure(
                PlayFailureCause.Simulation(result.error)
            )
        }
    }
}
