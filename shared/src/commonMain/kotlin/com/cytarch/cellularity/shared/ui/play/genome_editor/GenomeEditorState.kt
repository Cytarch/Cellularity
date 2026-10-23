package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.ParametricValueDraft
import com.cytarch.cellularity.shared.model.immutableListCopy

object GenomeEditorConfig {
    const val COARSE_STEPS = 24
    const val FINE_STEPS = 1200
    const val OSCILLATOR_PRESET_STEPS = 48
    const val PREVIEW_TIME_STEPS = 1200
    const val PREVIEW_MAX_TIME_HOURS = 48.0
    const val PREVIEW_TIME_BUTTON_STEP_HOURS = 0.02
    const val VARIABLE_MIN = 0.08246924f
}

data class GeneIndex(val value: Int) {
    init {
        require(value in 0 until GenomeContract.MAX_GENE_COUNT) { "Unknown gene index: $value" }
    }
}

data class ParametricValueId(val value: Int) {
    init {
        require(value in 0 until GenomeContract.PARAMETRIC_VALUE_COUNT) {
            "Unknown parametric value id: $value"
        }
    }
}

data class GeneIntegerId(val value: Int) {
    init {
        require(value in 0 until GenomeContract.INTEGER_VALUE_COUNT) {
            "Unknown gene integer id: $value"
        }
    }
}

enum class GenomeEditorSection {
    GENERAL,
    SPLITTING,
    SPECIFIC
}

enum class GenomeChild {
    FIRST,
    SECOND
}

enum class GenomeScalarField {
    SPLIT_MASS,
    SPLIT_RATIO,
    SPLIT_ANGLE,
    CHILD_1_ANGLE,
    CHILD_2_ANGLE,
    MASS_FLOW_PRIORITY,
    LINK_STRENGTH
}

enum class GenomeBooleanField {
    MAKE_LINK,
    CHILD_1_KEEP_LINKS,
    CHILD_2_KEEP_LINKS,
    STAY_ALIVE,
    CHILD_1_MIRROR,
    CHILD_2_MIRROR
}

enum class GenomePreviewOption {
    GRID,
    TREE,
    GHOST
}

enum class GenomeValidationReason {
    NON_FINITE,
    OUT_OF_RANGE,
    INVALID_INDEX,
    INVALID_ENUM,
    INVALID_COUNT
}

sealed interface GenomeValidationTarget {
    data object InitialGeneCount : GenomeValidationTarget
    data class Color(val gene: GeneIndex, val component: Int) : GenomeValidationTarget
    data class Scalar(val gene: GeneIndex, val field: GenomeScalarField) : GenomeValidationTarget
    data class ChildGene(val gene: GeneIndex, val child: GenomeChild) : GenomeValidationTarget
    data class CellTypeValue(val gene: GeneIndex) : GenomeValidationTarget
    data class ParametricVariable(
        val gene: GeneIndex,
        val value: ParametricValueId,
        val variable: Char
    ) : GenomeValidationTarget
    data class ParametricFormula(val gene: GeneIndex, val value: ParametricValueId) : GenomeValidationTarget
    data class ParametricSignal(val gene: GeneIndex, val value: ParametricValueId) : GenomeValidationTarget
    data class IntegerValue(val gene: GeneIndex, val value: GeneIntegerId) : GenomeValidationTarget
    data class FloatValue(val gene: GeneIndex, val value: GeneFloatType) : GenomeValidationTarget
}

data class GenomeValidationIssue(
    val target: GenomeValidationTarget,
    val reason: GenomeValidationReason
)

class GenomeValidation(issues: Iterable<GenomeValidationIssue>) {
    val issues: List<GenomeValidationIssue> = immutableListCopy(issues)
    val isValid: Boolean
        get() = issues.isEmpty()

    companion object {
        val Valid = GenomeValidation(emptyList())
    }
}

enum class GenomeFileFailureReason {
    BAD_NAME,
    NOT_FOUND,
    NEWER_VERSION,
    TOO_LARGE,
    INVALID_GENOME,
    NO_MICROSCOPE_CELL,
    READ_FAILED,
    WRITE_FAILED
}

sealed interface GenomeLoadChoice {
    data object Microscope : GenomeLoadChoice
    data object DefaultGenome : GenomeLoadChoice
    data class Stored(val name: String) : GenomeLoadChoice
}

sealed interface GenomeFileState {
    data object Idle : GenomeFileState
    data object Listing : GenomeFileState
    class Choosing(choices: Iterable<GenomeLoadChoice>) : GenomeFileState {
        val choices: List<GenomeLoadChoice> = immutableListCopy(choices)
    }
    data object EnteringSaveName : GenomeFileState
    data class Loading(val choice: GenomeLoadChoice) : GenomeFileState
    data class Saving(val name: String, val snapshot: GenomeDraft) : GenomeFileState
    data class Loaded(val name: String?) : GenomeFileState
    data class Saved(val name: String) : GenomeFileState
    data class Failed(
        val reason: GenomeFileFailureReason,
        val detail: String = ""
    ) : GenomeFileState
}

enum class GenomeLoadOrigin {
    STORED_FILE,
    MICROSCOPE,
    DEFAULT_GENOME
}

data class GenomePreviewState(
    val snapshot: GenomeDraft,
    val selectedGene: GeneIndex,
    val timeHours: Double,
    val revision: Long,
    val gridEnabled: Boolean,
    val treeEnabled: Boolean,
    val ghostEnabled: Boolean,
    val sourceIsValid: Boolean
)

data class GenomeEditorState(
    val genome: GenomeDraft,
    val selectedGene: GeneIndex,
    val selectedSection: GenomeEditorSection,
    val snapValues: Boolean,
    val dirty: Boolean,
    val validation: GenomeValidation,
    val preview: GenomePreviewState,
    val fileState: GenomeFileState,
    val genomeName: String
) {
    val selectedGeneDraft: GeneDraft
        get() = genome.genes[selectedGene.value]

    companion object {
        fun initial(genome: GenomeDraft): GenomeEditorState {
            val selectedGene = GeneIndex(0)
            val validation = GenomeDraftValidator.validate(genome)
            require(validation.isValid) { "Initial genome must be valid" }
            return GenomeEditorState(
                genome = genome,
                selectedGene = selectedGene,
                selectedSection = GenomeEditorSection.GENERAL,
                snapValues = true,
                dirty = false,
                validation = validation,
                preview = GenomePreviewState(
                    snapshot = genome,
                    selectedGene = selectedGene,
                    timeHours = 0.0,
                    revision = 0,
                    gridEnabled = true,
                    treeEnabled = false,
                    ghostEnabled = true,
                    sourceIsValid = true
                ),
                fileState = GenomeFileState.Idle,
                genomeName = ""
            )
        }
    }
}

sealed interface GenomeEditorAction {
    data class SelectSection(val section: GenomeEditorSection) : GenomeEditorAction
    data class SelectGene(val gene: GeneIndex) : GenomeEditorAction
    data class SetPreviewTime(val timeHours: Double) : GenomeEditorAction
    data class SetPreviewOption(
        val option: GenomePreviewOption,
        val enabled: Boolean
    ) : GenomeEditorAction
    data class SetSnapValues(val enabled: Boolean) : GenomeEditorAction

    data class SetCellType(val value: CellType) : GenomeEditorAction
    data class SetColor(val red: Float, val green: Float, val blue: Float) : GenomeEditorAction
    data class SetDivisionEnabled(val enabled: Boolean) : GenomeEditorAction
    data class SetScalar(val field: GenomeScalarField, val value: Float) : GenomeEditorAction
    data class SetBoolean(val field: GenomeBooleanField, val value: Boolean) : GenomeEditorAction
    data class SetChildGene(val child: GenomeChild, val gene: GeneIndex) : GenomeEditorAction
    data class SetInteger(val id: GeneIntegerId, val value: Int) : GenomeEditorAction
    data class SetFloat(val type: GeneFloatType, val value: Float) : GenomeEditorAction
    data class SetParametricValue(
        val id: ParametricValueId,
        val value: ParametricValueDraft
    ) : GenomeEditorAction
    data class ApplyOscillatorPreset(
        val configuration: GenomeOscillatorPresetConfiguration
    ) : GenomeEditorAction
    data class ReplaceSelectedGene(val value: GeneDraft) : GenomeEditorAction
    data class ResetGene(val gene: GeneIndex, val value: GeneDraft) : GenomeEditorAction
    data class CopyGene(val from: GeneIndex, val to: GeneIndex) : GenomeEditorAction
    data class AddGene(val after: GeneIndex, val value: GeneDraft) : GenomeEditorAction
    data class AddChildGene(val child: GenomeChild, val value: GeneDraft) : GenomeEditorAction
    data class DuplicateGene(val gene: GeneIndex) : GenomeEditorAction
    data class DeleteGene(val gene: GeneIndex, val replacement: GeneIndex) : GenomeEditorAction
    data class MoveGene(val from: GeneIndex, val to: GeneIndex) : GenomeEditorAction
    data class MakeInitial(val gene: GeneIndex) : GenomeEditorAction

    data object RequestLoad : GenomeEditorAction
    class LoadChoicesReady(storedNames: Iterable<String>) : GenomeEditorAction {
        val storedNames: List<String> = immutableListCopy(storedNames)
    }
    data object DismissLoad : GenomeEditorAction
    data class ChooseLoad(val choice: GenomeLoadChoice) : GenomeEditorAction
    data class LoadSucceeded(
        val genome: GenomeDraft,
        val origin: GenomeLoadOrigin,
        val name: String? = null
    ) : GenomeEditorAction
    data class LoadFailed(
        val reason: GenomeFileFailureReason,
        val detail: String = ""
    ) : GenomeEditorAction

    data object RequestSave : GenomeEditorAction
    data object DismissSave : GenomeEditorAction
    data class ConfirmSave(val name: String) : GenomeEditorAction
    data class SaveSucceeded(val name: String) : GenomeEditorAction
    data class SaveFailed(
        val reason: GenomeFileFailureReason,
        val detail: String = ""
    ) : GenomeEditorAction
    data object ClearFileFeedback : GenomeEditorAction
}

sealed interface GenomeEditorEffect {
    data object ListStoredGenomes : GenomeEditorEffect
    data class LoadGenome(val choice: GenomeLoadChoice) : GenomeEditorEffect
    data class SaveGenome(val name: String, val snapshot: GenomeDraft) : GenomeEditorEffect
}

class GenomeEditorTransition(
    val state: GenomeEditorState,
    effects: Iterable<GenomeEditorEffect> = emptyList()
) {
    val effects: List<GenomeEditorEffect> = immutableListCopy(effects)
}
