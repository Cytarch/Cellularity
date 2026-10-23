package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.ParametricValueDraft
import com.cytarch.cellularity.shared.model.immutableListCopy

enum class GenomeIntegerField(val id: GeneIntegerId) {
    VIRUS_FROM(GeneIntegerId(GenomeContract.VIRUS_FROM_INTEGER_INDEX)),
    VIRUS_TO(GeneIntegerId(GenomeContract.VIRUS_TO_INTEGER_INDEX)),
    SMELL_OUTPUT(GeneIntegerId(GenomeContract.SMELL_OUT_INTEGER_INDEX)),
    SMELL_TYPE(GeneIntegerId(GenomeContract.SMELL_TYPE_INTEGER_INDEX)),
    SECRETION_TYPE(GeneIntegerId(GenomeContract.SECRETE_TYPE_INTEGER_INDEX)),
    SIGNAL_OUTPUT_1(GeneIntegerId(GenomeContract.GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX)),
    SIGNAL_OUTPUT_2(GeneIntegerId(GenomeContract.GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + 1)),
    SIGNAL_OUTPUT_3(GeneIntegerId(GenomeContract.GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + 2)),
    SIGNAL_OUTPUT_4(GeneIntegerId(GenomeContract.GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + 3)),
    MAX_CONNECTIONS(GeneIntegerId(GenomeContract.MAX_CONNECTION_COUNT_INDEX)),
    TELOMERASE_LENGTH(GeneIntegerId(GenomeContract.TELOMERASE_LENGTH_INDEX))
}

sealed interface GenomeEditorSectionState

class GenomeGeneOption(
    val id: GeneIndex,
    val cellType: CellType,
    color: Iterable<Float>
) {
    val color: List<Float> = immutableListCopy(color)
}

class GenomeGeneCatalog(options: Iterable<GenomeGeneOption>) {
    val options: List<GenomeGeneOption> = immutableListCopy(options)
}

class GenomeOverviewSectionState(
    val gene: GeneIndex,
    val cellType: CellType,
    color: Iterable<Float>,
    val stayAlive: Boolean,
    val initial: Boolean,
    val dirty: Boolean
) : GenomeEditorSectionState {
    val color: List<Float> = immutableListCopy(color)
}

data class GenomeStructureSectionState(
    val cellType: CellType,
    val splitMass: Float,
    val splitRatio: Float,
    val splitAngle: Float,
    val massFlowPriority: Float,
    val makeLink: Boolean,
    val linkStrength: Float,
    val adhesinLength: Float,
    val cytoskeleton: Float,
    val maxConnections: Int,
    val snapValues: Boolean
) : GenomeEditorSectionState {
    val supportsDivision: Boolean
        get() = cellType != CellType.GAMETE
    val divisionEnabled: Boolean
        get() = supportsDivision && splitMass < CellDataConstants.MAX_MASS.toFloat()
}

data class GenomeChildRuleState(
    val child: GenomeChild,
    val targetGene: GeneIndex,
    val angle: Float,
    val keepLinks: Boolean,
    val mirror: Boolean
)

data class GenomeChildRulesSectionState(
    val first: GenomeChildRuleState,
    val second: GenomeChildRuleState,
    val genes: GenomeGeneCatalog
) : GenomeEditorSectionState

data class GenomeParametricValueState(
    val id: ParametricValueId,
    val value: ParametricValueDraft,
    val minimum: Float,
    val maximum: Float
)

data class GenomeNeuralOutputState(
    val outputIndex: Int,
    val channelField: GenomeIntegerField,
    val valueId: ParametricValueId,
    val channel: Int,
    val value: ParametricValueDraft
)

class GenomeSignalsSectionState(
    val cellType: CellType,
    val smellType: Int,
    val smellOutputChannel: Int,
    val smellAmplitude: Float,
    val smellRed: Float,
    val smellGreen: Float,
    val smellBlue: Float,
    val smellThreshold: Float,
    val secretionType: Int,
    val oscillatorPresetSource: GenomeOscillatorPresetSource?,
    neuralOutputs: Iterable<GenomeNeuralOutputState>
) : GenomeEditorSectionState {
    val neuralOutputs: List<GenomeNeuralOutputState> = immutableListCopy(neuralOutputs)
    val showsSensing: Boolean
        get() = cellType == CellType.SENSEOCYTE || cellType == CellType.STEREOCYTE
    val showsCellColorFilter: Boolean
        get() = showsSensing && smellType == CellDataConstants.SMELL_TYPE_CELL
    val showsSecretion: Boolean
        get() = cellType == CellType.SECROCYTE
    val showsNeuralOutputs: Boolean
        get() = cellType == CellType.NEUROCYTE
    val hasVisibleControls: Boolean
        get() = showsSensing || showsSecretion || showsNeuralOutputs
}

enum class GenomeCellSpecificParameter {
    VIRUS_COPY_FROM,
    VIRUS_COPY_TO,
    SWIM_FORCE,
    DENSITY,
    MYOCYTE_CONTRACTION,
    MYOCYTE_BENDING,
    MYOCYTE_LIFTING,
    STEM_MODE_1,
    STEM_THRESHOLD_1,
    STEM_MODE_2,
    STEM_THRESHOLD_2,
    GAMETE_COMPATIBLE_MODE,
    GAMETE_MODE_AFTER_FERTILIZATION,
    GAMETE_TELOMERASE_LENGTH,
    CILIOCYTE_LEFT_SPEED,
    CILIOCYTE_RIGHT_SPEED
}

sealed interface GenomeCellSpecificValue {
    val parameter: GenomeCellSpecificParameter

    data class GeneReference(
        override val parameter: GenomeCellSpecificParameter,
        val field: GenomeIntegerField,
        val selectedGene: GeneIndex
    ) : GenomeCellSpecificValue

    data class Parametric(
        override val parameter: GenomeCellSpecificParameter,
        val value: GenomeParametricValueState
    ) : GenomeCellSpecificValue

    data class IntegerChoice(
        override val parameter: GenomeCellSpecificParameter,
        val field: GenomeIntegerField,
        val selectedIndex: Int
    ) : GenomeCellSpecificValue
}

class GenomeCellSpecificSectionState(
    val cellType: CellType,
    val genes: GenomeGeneCatalog,
    values: Iterable<GenomeCellSpecificValue>
) : GenomeEditorSectionState {
    val values: List<GenomeCellSpecificValue> = immutableListCopy(values)
    val parameters: List<GenomeCellSpecificParameter> =
        immutableListCopy(this.values.map(GenomeCellSpecificValue::parameter))
}

/** Projects narrow immutable slices so section UI cannot reach unrelated editor fields. */
object GenomeEditorSections {
    fun geneCatalog(state: GenomeEditorState): GenomeGeneCatalog = GenomeGeneCatalog(
        state.genome.genes.mapIndexed { index, gene ->
            GenomeGeneOption(GeneIndex(index), gene.cellType, gene.color)
        }
    )

    fun overview(state: GenomeEditorState): GenomeOverviewSectionState {
        val gene = state.selectedGeneDraft
        return GenomeOverviewSectionState(
            gene = state.selectedGene,
            cellType = gene.cellType,
            color = gene.color,
            stayAlive = gene.stayAlive,
            initial = gene.initial,
            dirty = state.dirty
        )
    }

    fun structure(state: GenomeEditorState): GenomeStructureSectionState {
        val gene = state.selectedGeneDraft
        return GenomeStructureSectionState(
            cellType = gene.cellType,
            splitMass = gene.splitMass,
            splitRatio = gene.splitRatio,
            splitAngle = gene.splitAngle,
            massFlowPriority = gene.massFlowPriority,
            makeLink = gene.makeLink,
            linkStrength = gene.linkStrength,
            adhesinLength = gene.floatValues.adhesinLength,
            cytoskeleton = gene.floatValues.cytoskeleton,
            maxConnections = gene.integerValues[GenomeContract.MAX_CONNECTION_COUNT_INDEX],
            snapValues = state.snapValues
        )
    }

    fun childRules(state: GenomeEditorState): GenomeChildRulesSectionState {
        val gene = state.selectedGeneDraft
        return GenomeChildRulesSectionState(
            first = GenomeChildRuleState(
                child = GenomeChild.FIRST,
                targetGene = GeneIndex(gene.child1GeneIndex),
                angle = gene.child1Angle,
                keepLinks = gene.child1KeepLinks,
                mirror = gene.child1Mirror
            ),
            second = GenomeChildRuleState(
                child = GenomeChild.SECOND,
                targetGene = GeneIndex(gene.child2GeneIndex),
                angle = gene.child2Angle,
                keepLinks = gene.child2KeepLinks,
                mirror = gene.child2Mirror
            ),
            genes = geneCatalog(state)
        )
    }

    fun signals(state: GenomeEditorState): GenomeSignalsSectionState {
        val gene = state.selectedGeneDraft
        return GenomeSignalsSectionState(
            cellType = gene.cellType,
            smellType = gene.integerValues[GenomeContract.SMELL_TYPE_INTEGER_INDEX],
            smellOutputChannel = gene.integerValues[GenomeContract.SMELL_OUT_INTEGER_INDEX],
            smellAmplitude = gene.floatValues.smellAmplitude,
            smellRed = gene.floatValues.smellRed,
            smellGreen = gene.floatValues.smellGreen,
            smellBlue = gene.floatValues.smellBlue,
            smellThreshold = gene.floatValues.smellThreshold,
            secretionType = gene.integerValues[GenomeContract.SECRETE_TYPE_INTEGER_INDEX],
            oscillatorPresetSource = if (gene.cellType == CellType.NEUROCYTE) {
                GenomeOscillatorPreset.sourceFrom(gene)
            } else {
                null
            },
            neuralOutputs = (0 until CellDataConstants.NEUROCYTE_OUTPUT_COUNT).map { outputIndex ->
                val channelField = GenomeIntegerField.entries.first {
                    it.id.value == GenomeContract.GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + outputIndex
                }
                val valueId = ParametricValueId(
                    GenomeContract.GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + outputIndex
                )
                GenomeNeuralOutputState(
                    outputIndex = outputIndex,
                    channelField = channelField,
                    valueId = valueId,
                    channel = gene.integerValues[channelField.id.value],
                    value = gene.parametricValues[valueId.value]
                )
            }
        )
    }

    fun cellSpecific(state: GenomeEditorState): GenomeCellSpecificSectionState {
        val cellType = state.selectedGeneDraft.cellType
        val gene = state.selectedGeneDraft
        fun reference(
            parameter: GenomeCellSpecificParameter,
            field: GenomeIntegerField
        ) = GenomeCellSpecificValue.GeneReference(
            parameter,
            field,
            GeneIndex(gene.integerValues[field.id.value])
        )
        fun parametric(
            parameter: GenomeCellSpecificParameter,
            id: ParametricValueId
        ) = GenomeCellSpecificValue.Parametric(
            parameter,
            GenomeParametricValueState(
                id = id,
                value = gene.parametricValues[id.value],
                minimum = GenomeContract.parametricMinimums[id.value],
                maximum = GenomeContract.parametricMaximums[id.value]
            )
        )
        val values = when (cellType) {
            CellType.VIROCYTE -> listOf(
                reference(GenomeCellSpecificParameter.VIRUS_COPY_FROM, GenomeIntegerField.VIRUS_FROM),
                reference(GenomeCellSpecificParameter.VIRUS_COPY_TO, GenomeIntegerField.VIRUS_TO)
            )
            CellType.FLAGELLOCYTE -> listOf(
                parametric(
                    GenomeCellSpecificParameter.SWIM_FORCE,
                    ParametricValueId(GenomeContract.SWIM_FORCE_VALUE_INDEX)
                )
            )
            CellType.BUOYOCYTE -> listOf(
                parametric(
                    GenomeCellSpecificParameter.DENSITY,
                    ParametricValueId(GenomeContract.DENSITY_VALUE_INDEX)
                )
            )
            CellType.MYOCYTE -> listOf(
                parametric(
                    GenomeCellSpecificParameter.MYOCYTE_CONTRACTION,
                    ParametricValueId(GenomeContract.MYOCYTE_CONTRACTION_VALUE_INDEX)
                ),
                parametric(
                    GenomeCellSpecificParameter.MYOCYTE_BENDING,
                    ParametricValueId(GenomeContract.MYOCYTE_BENDING_VALUE_INDEX)
                ),
                parametric(
                    GenomeCellSpecificParameter.MYOCYTE_LIFTING,
                    ParametricValueId(GenomeContract.MYOCYTE_LIFTING_VALUE_INDEX)
                )
            )
            CellType.STEMOCYTE -> listOf(
                reference(GenomeCellSpecificParameter.STEM_MODE_1, GenomeIntegerField.VIRUS_FROM),
                parametric(
                    GenomeCellSpecificParameter.STEM_THRESHOLD_1,
                    ParametricValueId(GenomeContract.STEM_THRESHOLD_1_VALUE_INDEX)
                ),
                reference(GenomeCellSpecificParameter.STEM_MODE_2, GenomeIntegerField.VIRUS_TO),
                parametric(
                    GenomeCellSpecificParameter.STEM_THRESHOLD_2,
                    ParametricValueId(GenomeContract.STEM_THRESHOLD_2_VALUE_INDEX)
                )
            )
            CellType.GAMETE -> listOf(
                reference(
                    GenomeCellSpecificParameter.GAMETE_COMPATIBLE_MODE,
                    GenomeIntegerField.VIRUS_TO
                ),
                reference(
                    GenomeCellSpecificParameter.GAMETE_MODE_AFTER_FERTILIZATION,
                    GenomeIntegerField.VIRUS_FROM
                ),
                GenomeCellSpecificValue.IntegerChoice(
                    GenomeCellSpecificParameter.GAMETE_TELOMERASE_LENGTH,
                    GenomeIntegerField.TELOMERASE_LENGTH,
                    gene.integerValues[GenomeIntegerField.TELOMERASE_LENGTH.id.value]
                )
            )
            CellType.CILIOCYTE -> listOf(
                parametric(
                    GenomeCellSpecificParameter.CILIOCYTE_LEFT_SPEED,
                    ParametricValueId(GenomeContract.MYOCYTE_BENDING_VALUE_INDEX)
                ),
                parametric(
                    GenomeCellSpecificParameter.CILIOCYTE_RIGHT_SPEED,
                    ParametricValueId(GenomeContract.MYOCYTE_LIFTING_VALUE_INDEX)
                )
            )
            else -> emptyList()
        }
        return GenomeCellSpecificSectionState(cellType, geneCatalog(state), values)
    }

}
