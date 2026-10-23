package com.cytarch.cellularity.ui.play_menu

import android.content.Context
import com.cytarch.cellularity.NativeCellularityControllerBridge
import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.simulation.AndroidSimulationConnection
import com.cytarch.cellularity.platform.simulation.AndroidSimulationFactory
import com.cytarch.cellularity.platform.simulation.RetainedSimulationOwner
import com.cytarch.cellularity.platform.simulation.toCellularityError
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.simulation.SaveResult
import com.cytarch.cellularity.shared.simulation.SimulationCommand
import com.cytarch.cellularity.shared.simulation.SimulationContentState
import com.cytarch.cellularity.shared.simulation.SimulationGateway
import com.cytarch.cellularity.shared.simulation.SimulationResult
import com.cytarch.cellularity.shared.simulation.SimulationSessionState
import com.cytarch.cellularity.shared.ui.play.PlayFailureCause
import com.cytarch.cellularity.shared.ui.play.PlayLaunchSource
import com.cytarch.cellularity.shared.ui.play.PlayOpenResult
import com.cytarch.cellularity.shared.ui.play.PlaySaveResult
import com.cytarch.cellularity.shared.ui.play.PlaySessionCommandResult
import com.cytarch.cellularity.shared.ui.play.PlaySessionOperations
import java.util.concurrent.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.withContext

/** Owns one retained native session; it contains no Activity or Compose state. */
@OptIn(ExperimentalCoroutinesApi::class)
internal class PlaySessionController(
    ownerScope: CoroutineScope,
    context: Context,
    private val fileRepository: AndroidCellularityFileRepository,
    private val connectionFactory: suspend (
        Context,
        AndroidCellularityFileRepository,
        SimulationConfiguration,
        StoredSubstrateId?
    ) -> AndroidSimulationConnection = AndroidSimulationFactory::create
) : PlaySessionOperations, AutoCloseable {
    private val applicationContext = context.applicationContext ?: context
    private val simulationOwner = RetainedSimulationOwner<AndroidSimulationConnection>(ownerScope)
    private val activeGateway = MutableStateFlow<SimulationGateway?>(null)

    val contentStates: Flow<SimulationContentState> = activeGateway
        .filterNotNull()
        .flatMapLatest { it.contentState }

    val sessionStates: Flow<SimulationSessionState> = activeGateway
        .filterNotNull()
        .flatMapLatest { it.state }

    override suspend fun open(source: PlayLaunchSource): PlayOpenResult {
        val configuration = when (source) {
            is PlayLaunchSource.New -> {
                if (!isConfigurationSupported(source.configuration)) {
                    return PlayOpenResult.Failure(
                        PlayFailureCause.Storage(
                            StorageError(StorageErrorCode.TOO_LARGE)
                        )
                    )
                }
                source.configuration
            }
            is PlayLaunchSource.Stored -> when (val inspected = inspect(source.id)) {
                is StoredInspection.Valid -> inspected.configuration
                is StoredInspection.Invalid -> return PlayOpenResult.Failure(inspected.cause)
            }
        }

        return try {
            val connection = simulationOwner.getOrCreate {
                withContext(Dispatchers.Default) {
                    connectionFactory(
                        applicationContext,
                        fileRepository,
                        configuration,
                        (source as? PlayLaunchSource.Stored)?.id
                    )
                }
            }
            activeGateway.value = connection.gateway
            PlayOpenResult.Opened(connection.gateway.contentState.value)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: Throwable) {
            PlayOpenResult.Failure(
                PlayFailureCause.Simulation(failure.toCellularityError())
            )
        }
    }

    override suspend fun substrateExists(name: String): StorageResult<Boolean> =
        fileRepository.substrateExists(name)

    override suspend fun save(target: StoredSubstrateId): PlaySaveResult {
        val gateway = activeGateway.value ?: return PlaySaveResult.Failure(notReadyCause())
        return when (val result = gateway.save(target)) {
            is SaveResult.Saved -> PlaySaveResult.Saved(
                target = result.target,
                content = gateway.contentState.value
            )
            is SaveResult.Failure -> PlaySaveResult.Failure(
                PlayFailureCause.Simulation(result.error)
            )
        }
    }

    override suspend fun reload(source: StoredSubstrateId): PlaySessionCommandResult {
        when (val inspected = inspect(source)) {
            is StoredInspection.Invalid -> return PlaySessionCommandResult.Failure(inspected.cause)
            is StoredInspection.Valid -> Unit
        }
        return execute(SimulationCommand.Reload(source))
    }

    override suspend fun reset(): PlaySessionCommandResult =
        execute(SimulationCommand.Reset)

    fun requireGateway(): SimulationGateway = simulationOwner.requireCurrent().gateway

    fun requireMicroscopeSurfaceFactory() = simulationOwner.requireCurrent().microscope

    fun selectedGenome() = runCatching {
        simulationOwner.requireCurrent().microscope.selectedGenome()
    }.getOrNull()

    override fun close() {
        simulationOwner.close()
        activeGateway.value = null
    }

    private suspend fun inspect(id: StoredSubstrateId): StoredInspection =
        when (val result = fileRepository.inspectStoredSubstrate(id)) {
            is StorageResult.Failure -> StoredInspection.Invalid(
                PlayFailureCause.Storage(result.error)
            )
            is StorageResult.Success -> {
                val inspection = result.value
                when {
                    !inspection.loadSupported ->
                        StoredInspection.Invalid(
                            PlayFailureCause.Storage(
                                StorageError(StorageErrorCode.NEWER_VERSION)
                            )
                        )
                    !isConfigurationSupported(inspection.configuration) ||
                        inspection.cellCount > inspection.configuration.maxCellCount ->
                        StoredInspection.Invalid(
                            PlayFailureCause.Storage(
                                StorageError(
                                    StorageErrorCode.TOO_LARGE,
                                    "Substrate declares an unsupported simulation size"
                                )
                            )
                        )
                    else -> StoredInspection.Valid(inspection.configuration)
                }
            }
        }

    private suspend fun execute(command: SimulationCommand): PlaySessionCommandResult {
        val gateway = activeGateway.value
            ?: return PlaySessionCommandResult.Failure(notReadyCause())
        return when (val result = gateway.execute(command)) {
            SimulationResult.Queued -> PlaySessionCommandResult.Accepted(
                gateway.contentState.value
            )
            is SimulationResult.Failure -> PlaySessionCommandResult.Failure(
                PlayFailureCause.Simulation(result.error)
            )
        }
    }

    private fun notReadyCause(): PlayFailureCause = PlayFailureCause.Simulation(
        CellularityError(
            CellularityErrorCode.SESSION_NOT_READY,
            "Simulation session is unavailable"
        )
    )

    private fun isConfigurationSupported(configuration: SimulationConfiguration): Boolean =
        SimulationContract.isConfigurationValid(configuration) &&
            NativeCellularityControllerBridge.isConfigurationSupported(configuration)

    private sealed interface StoredInspection {
        data class Valid(val configuration: SimulationConfiguration) : StoredInspection
        data class Invalid(val cause: PlayFailureCause) : StoredInspection
    }
}
