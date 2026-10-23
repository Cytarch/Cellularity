package com.cytarch.cellularity.shared.simulation

import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.EnvironmentContract
import com.cytarch.cellularity.shared.model.EnvironmentFieldType
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.SimulationStats
import com.cytarch.cellularity.shared.model.immutableListCopy
import kotlinx.coroutines.flow.StateFlow

/** Coarse application boundary for one live simulation session. */
interface SimulationGateway {
    val state: StateFlow<SimulationSessionState>
    val contentState: StateFlow<SimulationContentState>
    val environment: StateFlow<EnvironmentSnapshot>
    val stats: StateFlow<SimulationStats>

    suspend fun execute(command: SimulationCommand): SimulationResult

    suspend fun applyEnvironment(patch: EnvironmentPatch): ApplyResult

    suspend fun inspectCell(id: CellId): CellSnapshot?

    suspend fun save(target: StoredSubstrateId? = null): SaveResult

    suspend fun close()
}

/**
 * Monotonic dirty-state authority for one live world.
 *
 * [revision] advances when the world first diverges from its last durable snapshot
 * and whenever reset/reload replaces the complete world.
 * [persistedRevision] is updated only after a stored substrate has been opened,
 * successfully reloaded, or durably saved. A null persisted revision therefore
 * represents a new world which has never been saved.
 */
data class SimulationContentState(
    val revision: Long = 0,
    val persistedRevision: Long? = null
) {
    init {
        require(revision >= 0) { "Simulation content revision must be non-negative" }
        require(persistedRevision == null || persistedRevision in 0..revision) {
            "Persisted simulation revision must refer to current content history"
        }
    }

    val isDirty: Boolean
        get() = persistedRevision != revision

    companion object {
        fun newWorld(): SimulationContentState = SimulationContentState()

        fun storedWorld(): SimulationContentState = SimulationContentState(
            revision = 0,
            persistedRevision = 0
        )
    }
}

sealed interface SimulationSessionState {
    data object New : SimulationSessionState

    data object Opening : SimulationSessionState

    data object Ready : SimulationSessionState

    data object Paused : SimulationSessionState

    data object Saving : SimulationSessionState

    data object Closing : SimulationSessionState

    data object Closed : SimulationSessionState

    data class Failed(val error: CellularityError) : SimulationSessionState
}

data class CellId(val value: Int) {
    init {
        require(value >= 0) { "Cell ID must be non-negative" }
    }
}

sealed interface SimulationCommand {
    data object Reset : SimulationCommand

    data class Reload(val source: StoredSubstrateId) : SimulationCommand
}

sealed interface SimulationResult {
    data object Queued : SimulationResult

    data class Failure(val error: CellularityError) : SimulationResult
}

sealed interface ApplyResult {
    /** Native accepted the complete patch and advanced its authoritative revision once. */
    data class Applied(val revision: Long) : ApplyResult {
        init {
            require(revision >= 0) { "Applied environment revision must be non-negative" }
        }
    }

    /** Native rejected the complete patch; no contained change was committed. */
    class Rejected(
        val currentRevision: Long,
        rejections: Iterable<EnvironmentPatchRejection>
    ) : ApplyResult {
        val rejections: List<EnvironmentPatchRejection> = immutableListCopy(rejections)

        init {
            require(currentRevision >= 0) { "Current environment revision must be non-negative" }
            require(this.rejections.isNotEmpty()) { "A rejected patch must explain its rejection" }
        }
    }

    data class Failure(val error: CellularityError) : ApplyResult
}

sealed interface SaveResult {
    data class Saved(val target: StoredSubstrateId) : SaveResult

    data class Failure(val error: CellularityError) : SaveResult
}

/** A bounded set of typed environment changes based on an observed revision. */
class EnvironmentPatch(
    val schemaVersion: Int = EnvironmentContract.SCHEMA_VERSION,
    val baseRevision: Long,
    changes: Iterable<EnvironmentChange>
) {
    val changes: List<EnvironmentChange> = immutableListCopy(changes)

    init {
        require(schemaVersion > 0) { "Environment schema version must be positive" }
        require(baseRevision >= 0) {
            "Environment base revision must be non-negative"
        }
        require(this.changes.isNotEmpty()) { "Environment patch must not be empty" }
        require(this.changes.size <= EnvironmentContract.MAX_PATCH_CHANGES) {
            "Environment patch exceeds ${EnvironmentContract.MAX_PATCH_CHANGES} changes"
        }
    }

    companion object {
        fun single(
            change: EnvironmentChange,
            baseRevision: Long
        ): EnvironmentPatch = EnvironmentPatch(
            baseRevision = baseRevision,
            changes = listOf(change)
        )
    }
}

sealed interface EnvironmentChange {
    data class BooleanValue(
        val property: EnvironmentBooleanProperty,
        val value: Boolean
    ) : EnvironmentChange

    data class ScalarValue(
        val property: EnvironmentScalarProperty,
        val value: Double
    ) : EnvironmentChange

    data class FieldType(
        val field: EnvironmentField,
        val value: EnvironmentFieldType
    ) : EnvironmentChange

    data class FieldValue(
        val field: EnvironmentField,
        val component: EnvironmentFieldComponent,
        val value: Double
    ) : EnvironmentChange

    data class MutationWeight(val index: Int, val value: Double) : EnvironmentChange

    data object ResetMutationWeights : EnvironmentChange

    data class GenePoolEnabled(val index: Int, val enabled: Boolean) : EnvironmentChange
}

enum class EnvironmentBooleanProperty(val wireId: Int) {
    ADD_RANDOM(1),
    DIE_EDGE(2),
    ONLY_TWEAK_MUTATIONS(3),
    MUTATION_ONLY_AFTER_SPLIT(4),
    AGING(5),
    MOBILE_FOOD(6)
}

enum class EnvironmentScalarProperty(val wireId: Int) {
    RADIATION(1),
    TEST_MULTICELLULAR_COEF(2),
    TEST_MUTATION_COEF(3),
    TEST_FOOD_DECAY(4),
    TEST_IDLE_METABOLISM_SCALE(5),
    TEST_FLAGELLOCYTE_COST_SCALE(6),
    TEST_PHAGOCYTE_EAT_SPEED_SCALE(7),
    VISCOSITY(8),
    FRICTION_DYNAMIC(9),
    FRICTION_STATIC(10),
    FOOD_SPEED(11),
    FOOD_SIZE(12),
    FOOD_LUMPINESS(13),
    FOOD_LUMP_SIZE(14),
    FOOD_LIPID_LAYER(15),
    SALINITY(16),
    LIGHT_INTENSITY(17),
    LIGHT_RANGE(18),
    LIGHT_SPEED(19),
    GRAVITY(20),
    DENSITY(21),
    DENSITY_GRADIENT(22),
    NITRATES(23)
}

enum class EnvironmentField(val wireId: Int) {
    GRAVITY(1),
    DENSITY(2),
    SALINITY(3),
    FOOD_LIPID_LAYER(4)
}

enum class EnvironmentFieldComponent(val wireId: Int) {
    BASE(1),
    STRENGTH(2),
    ANGLE(3),
    OFFSET(4),
    SOFTNESS(5)
}

data class EnvironmentPatchRejection(
    /** Index in [EnvironmentPatch.changes], or -1 for a patch-level rejection. */
    val changeIndex: Int,
    val reason: EnvironmentPatchRejectionReason
) {
    init {
        require(changeIndex >= -1) { "Environment rejection index must be -1 or non-negative" }
    }
}

enum class EnvironmentPatchRejectionReason(val wireId: Int) {
    REVISION_CONFLICT(1),
    SCHEMA_MISMATCH(2),
    MALFORMED_PATCH(3),
    INVALID_CHANGE_KIND(4),
    INVALID_PROPERTY(5),
    INVALID_FIELD_TYPE(6),
    INVALID_FIELD_COMPONENT(7),
    NON_FINITE_VALUE(8),
    OUT_OF_RANGE(9),
    INVALID_INDEX(10),
    INVALID_COUNT(11),
    EMPTY_GENE_POOL(12);

    companion object {
        fun fromWireId(value: Int): EnvironmentPatchRejectionReason =
            entries.firstOrNull { it.wireId == value }
                ?: throw IllegalArgumentException("Unknown environment rejection reason: $value")
    }
}
