package com.cytarch.cellularity.shared.ui.play.substrate_info

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.EnvironmentFieldType
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.model.immutableListCopy
import com.cytarch.cellularity.shared.simulation.EnvironmentPatchRejectionReason

enum class EnvironmentCheckboxType {
    ADD_RANDOM,
    DIE_EDGE,
    TWEAK,
    MUTATION_ONLY_AFTER_SPLIT,
    AGING,
    MOBILE_FOOD
}

enum class SubstrateSliderType {
    RADIATION,
    TEST_MULTICELLULAR_COEF,
    TEST_MUTATION_COEF,
    TEST_FOOD_DECAY,
    TEST_IDLE_METABOLISM_SCALE,
    TEST_FLAGELLOCYTE_COST_SCALE,
    TEST_PHAGOCYTE_EAT_SPEED_SCALE,
    VISCOSITY,
    FRICTION_DYNAMIC,
    FRICTION_STATIC,
    FOOD_SPEED,
    FOOD_SIZE,
    FOOD_LUMPINESS,
    FOOD_LUMP_SIZE,
    FOOD_LIPID_LAYER,
    SALINITY,
    LIGHT_INTENSITY,
    LIGHT_RANGE,
    LIGHT_SPEED,
    GRAVITY,
    DENSITY,
    DENSITY_GRADIENT,
    NITRATES
}

enum class DensityFieldValueType {
    BASE,
    STRENGTH,
    ANGLE,
    OFFSET,
    SOFTNESS
}

enum class EnvironmentFieldTarget {
    DENSITY,
    SALINITY,
    FOOD_LIPID_LAYER,
    GRAVITY
}

enum class SubstrateInfoSection {
    MECHANICS,
    SOURCES,
    EVOLUTION,
    STATS
}

data class SubstrateSliderValue(
    val type: SubstrateSliderType,
    val value: Double,
    val min: Double,
    val max: Double
)

data class SubstrateFieldRanges(
    val baseMin: Double,
    val baseMax: Double,
    val strengthMin: Double,
    val strengthMax: Double
)

data class SubstrateFieldUiState(
    val target: EnvironmentFieldTarget,
    val type: EnvironmentFieldType,
    val base: Double,
    val strength: Double,
    val angle: Double,
    val offset: Double,
    val softness: Double,
    val ranges: SubstrateFieldRanges
) {
    init {
        require(
            (target == EnvironmentFieldTarget.GRAVITY && type is VectorFieldType) ||
                (target != EnvironmentFieldTarget.GRAVITY && type is ScalarFieldType)
        ) { "Environment field type does not match $target" }
    }
}

data class SubstrateFieldsSectionState(
    val density: SubstrateFieldUiState,
    val gravity: SubstrateFieldUiState,
    val salinity: SubstrateFieldUiState,
    val foodLipidLayer: SubstrateFieldUiState
) {
    fun field(target: EnvironmentFieldTarget): SubstrateFieldUiState = when (target) {
        EnvironmentFieldTarget.DENSITY -> density
        EnvironmentFieldTarget.GRAVITY -> gravity
        EnvironmentFieldTarget.SALINITY -> salinity
        EnvironmentFieldTarget.FOOD_LIPID_LAYER -> foodLipidLayer
    }
}

class SubstrateMechanicsSectionState(
    val dieEdge: Boolean,
    sliders: Iterable<SubstrateSliderValue>
) {
    val sliders: List<SubstrateSliderValue> = immutableListCopy(sliders)
}

class SubstrateNutritionSectionState(
    val mobileFood: Boolean,
    sliders: Iterable<SubstrateSliderValue>
) {
    val sliders: List<SubstrateSliderValue> = immutableListCopy(sliders)
}

class SubstrateEvolutionSectionState(
    val addRandom: Boolean,
    val onlyTweakMutations: Boolean,
    val mutationOnlyAfterSplit: Boolean,
    val aging: Boolean,
    val radiation: SubstrateSliderValue,
    mutationWeights: Iterable<Double>,
    genePool: Iterable<Boolean>,
    genePoolEnabled: Iterable<Boolean>
) {
    val mutationWeights: List<Double> = immutableListCopy(mutationWeights)
    val genePool: List<Boolean> = immutableListCopy(genePool)
    val genePoolEnabled: List<Boolean> = immutableListCopy(genePoolEnabled)
}

class SubstrateExperimentalSectionState(
    sliders: Iterable<SubstrateSliderValue>
) {
    val sliders: List<SubstrateSliderValue> = immutableListCopy(sliders)
}

data class SubstrateStatsSectionState(
    val time: Double = 0.0,
    val cellCount: Int = 0,
    val foodMass: Double = 0.0
)

data class SubstrateEnvironmentUiState(
    val revision: Long,
    val mechanics: SubstrateMechanicsSectionState,
    val fields: SubstrateFieldsSectionState,
    val nutrition: SubstrateNutritionSectionState,
    val evolution: SubstrateEvolutionSectionState,
    val experimental: SubstrateExperimentalSectionState
)

sealed interface SubstrateEditTarget {
    data class Checkbox(val type: EnvironmentCheckboxType) : SubstrateEditTarget
    data class Slider(val type: SubstrateSliderType) : SubstrateEditTarget
    data class FieldType(val field: EnvironmentFieldTarget) : SubstrateEditTarget
    data class FieldValue(
        val field: EnvironmentFieldTarget,
        val component: DensityFieldValueType
    ) : SubstrateEditTarget
    data class MutationWeight(val index: Int) : SubstrateEditTarget
    data object ResetMutationWeights : SubstrateEditTarget
    data class GenePool(val index: Int) : SubstrateEditTarget
}

sealed interface SubstrateEditStatus {
    data object Idle : SubstrateEditStatus

    data class Pending(val target: SubstrateEditTarget) : SubstrateEditStatus

    data class Applied(
        val target: SubstrateEditTarget,
        val revision: Long
    ) : SubstrateEditStatus

    class Rejected(
        val target: SubstrateEditTarget,
        val currentRevision: Long,
        reasons: Iterable<EnvironmentPatchRejectionReason>
    ) : SubstrateEditStatus {
        val reasons: List<EnvironmentPatchRejectionReason> = immutableListCopy(reasons)

        init {
            require(this.reasons.isNotEmpty()) { "A rejected edit must explain its rejection" }
        }
    }

    data class Failed(
        val target: SubstrateEditTarget,
        val error: CellularityError
    ) : SubstrateEditStatus
}

enum class SubstrateEditFeedbackKind {
    PENDING,
    APPLIED,
    REVISION_CONFLICT,
    INVALID_VALUE,
    EMPTY_GENE_POOL,
    REJECTED,
    FAILED
}

fun SubstrateEditStatus.feedbackKind(): SubstrateEditFeedbackKind? = when (this) {
    SubstrateEditStatus.Idle -> null
    is SubstrateEditStatus.Pending -> SubstrateEditFeedbackKind.PENDING
    is SubstrateEditStatus.Applied -> SubstrateEditFeedbackKind.APPLIED
    is SubstrateEditStatus.Failed -> SubstrateEditFeedbackKind.FAILED
    is SubstrateEditStatus.Rejected -> when {
        EnvironmentPatchRejectionReason.EMPTY_GENE_POOL in reasons ->
            SubstrateEditFeedbackKind.EMPTY_GENE_POOL
        EnvironmentPatchRejectionReason.REVISION_CONFLICT in reasons ->
            SubstrateEditFeedbackKind.REVISION_CONFLICT
        EnvironmentPatchRejectionReason.NON_FINITE_VALUE in reasons ||
            EnvironmentPatchRejectionReason.OUT_OF_RANGE in reasons ->
            SubstrateEditFeedbackKind.INVALID_VALUE
        else -> SubstrateEditFeedbackKind.REJECTED
    }
}

internal fun SubstrateEditStatus.visibleFeedbackKind(): SubstrateEditFeedbackKind? =
    feedbackKind()?.takeUnless { it == SubstrateEditFeedbackKind.APPLIED }

/** Retains navigation-only state; confirmed environment and stats stay immutable inputs. */
class SubstrateInfoState {
    var selectedSection by mutableStateOf(SubstrateInfoSection.MECHANICS)
        private set

    var editingFieldTarget by mutableStateOf<EnvironmentFieldTarget?>(null)
        private set

    var mutationWeightsVisible by mutableStateOf(false)
        private set

    private val scrollOffsets = IntArray(SubstrateInfoSection.entries.size)

    fun updateSelectedSection(section: SubstrateInfoSection) {
        selectedSection = section
        val fieldSection = when (editingFieldTarget) {
            EnvironmentFieldTarget.DENSITY,
            EnvironmentFieldTarget.GRAVITY -> SubstrateInfoSection.MECHANICS
            EnvironmentFieldTarget.SALINITY,
            EnvironmentFieldTarget.FOOD_LIPID_LAYER -> SubstrateInfoSection.SOURCES
            null -> null
        }
        if (fieldSection != null && fieldSection != section) {
            editingFieldTarget = null
        }
        if (section != SubstrateInfoSection.EVOLUTION) {
            mutationWeightsVisible = false
        }
    }

    fun openFieldEditor(target: EnvironmentFieldTarget) {
        editingFieldTarget = target
    }

    fun closeFieldEditor() {
        editingFieldTarget = null
    }

    fun showMutationWeights() {
        mutationWeightsVisible = true
    }

    fun hideMutationWeights() {
        mutationWeightsVisible = false
    }

    fun getScrollOffset(section: SubstrateInfoSection): Int = scrollOffsets[section.ordinal]

    fun updateScrollOffset(section: SubstrateInfoSection, offset: Int) {
        scrollOffsets[section.ordinal] = offset.coerceAtLeast(0)
    }
}
