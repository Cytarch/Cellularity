package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeInitialGeneCompatibility
import com.cytarch.cellularity.shared.model.LinkConstants
import kotlin.math.PI

/** Mirrors the semantic checks at the native GenomeDraft boundary for immediate UI feedback. */
object GenomeDraftValidator {
    fun validate(genome: GenomeDraft): GenomeValidation {
        val issues = mutableListOf<GenomeValidationIssue>()
        genome.genes.forEachIndexed { index, gene ->
            validateGene(GeneIndex(index), gene, genome.genes.size, issues)
        }
        if (GenomeInitialGeneCompatibility.extract(genome) == null) {
            issues += GenomeValidationIssue(
                GenomeValidationTarget.InitialGeneCount,
                GenomeValidationReason.INVALID_COUNT
            )
        }
        return if (issues.isEmpty()) GenomeValidation.Valid else GenomeValidation(issues)
    }

    private fun validateGene(
        geneIndex: GeneIndex,
        gene: GeneDraft,
        geneCount: Int,
        issues: MutableList<GenomeValidationIssue>
    ) {
        gene.color.forEachIndexed { component, value ->
            finiteRange(
                value,
                0.0f,
                1.0f,
                GenomeValidationTarget.Color(geneIndex, component),
                issues
            )
        }
        scalar(
            geneIndex,
            GenomeScalarField.SPLIT_MASS,
            gene.splitMass,
            CellDataConstants.MIN_SPLIT_MASS,
            CellDataConstants.MAX_SPLIT_MASS,
            issues
        )
        scalar(
            geneIndex,
            GenomeScalarField.SPLIT_RATIO,
            gene.splitRatio,
            CellDataConstants.MIN_RATIO,
            1.0f - CellDataConstants.MIN_RATIO,
            issues
        )
        val twoPi = (2.0 * PI).toFloat()
        scalar(geneIndex, GenomeScalarField.SPLIT_ANGLE, gene.splitAngle, 0.0f, twoPi, issues)
        scalar(geneIndex, GenomeScalarField.CHILD_1_ANGLE, gene.child1Angle, 0.0f, twoPi, issues)
        scalar(geneIndex, GenomeScalarField.CHILD_2_ANGLE, gene.child2Angle, 0.0f, twoPi, issues)
        scalar(
            geneIndex,
            GenomeScalarField.MASS_FLOW_PRIORITY,
            gene.massFlowPriority,
            CellDataConstants.MIN_MASS_FLOW_PRIORITY,
            CellDataConstants.MAX_MASS_FLOW_PRIORITY,
            issues
        )
        scalar(
            geneIndex,
            GenomeScalarField.LINK_STRENGTH,
            gene.linkStrength,
            0.0f,
            LinkConstants.MAX_STIFFNESS,
            issues
        )

        index(
            gene.child1GeneIndex,
            geneCount,
            GenomeValidationTarget.ChildGene(geneIndex, GenomeChild.FIRST),
            issues
        )
        index(
            gene.child2GeneIndex,
            geneCount,
            GenomeValidationTarget.ChildGene(geneIndex, GenomeChild.SECOND),
            issues
        )
        if (gene.cellType.nativeValue !in CellType.entries.indices) {
            issues += GenomeValidationIssue(
                GenomeValidationTarget.CellTypeValue(geneIndex),
                GenomeValidationReason.INVALID_ENUM
            )
        }

        gene.parametricValues.forEachIndexed { index, value ->
            val valueId = ParametricValueId(index)
            finiteRange(
                value.variableA,
                -CellDataConstants.VALUE_VARIABLE_MAX,
                CellDataConstants.VALUE_VARIABLE_MAX,
                GenomeValidationTarget.ParametricVariable(geneIndex, valueId, 'A'),
                issues
            )
            finiteRange(
                value.variableB,
                -CellDataConstants.VALUE_VARIABLE_MAX,
                CellDataConstants.VALUE_VARIABLE_MAX,
                GenomeValidationTarget.ParametricVariable(geneIndex, valueId, 'B'),
                issues
            )
            finiteRange(
                value.variableC,
                -CellDataConstants.VALUE_VARIABLE_MAX,
                CellDataConstants.VALUE_VARIABLE_MAX,
                GenomeValidationTarget.ParametricVariable(geneIndex, valueId, 'C'),
                issues
            )
            integerRange(
                value.formulaType.toInt(),
                0,
                GenomeContract.FORMULA_TYPE_COUNT - 1,
                GenomeValidationTarget.ParametricFormula(geneIndex, valueId),
                issues,
                GenomeValidationReason.INVALID_ENUM
            )
            index(
                value.signalSubstanceChannelIndex.toInt(),
                GenomeContract.SIGNAL_INPUT_COUNT,
                GenomeValidationTarget.ParametricSignal(geneIndex, valueId),
                issues
            )
        }

        gene.integerValues.forEachIndexed { integerIndex, value ->
            index(
                value,
                if (integerIndex == GenomeContract.VIRUS_FROM_INTEGER_INDEX ||
                    integerIndex == GenomeContract.VIRUS_TO_INTEGER_INDEX
                ) geneCount else GenomeContract.integerUpperBounds[integerIndex],
                GenomeValidationTarget.IntegerValue(geneIndex, GeneIntegerId(integerIndex)),
                issues
            )
        }
        GeneFloatType.entries.forEach { type ->
            val range = GenomeContract.floatRange(type)
            finiteRange(
                gene.floatValues[type],
                range.min,
                range.max,
                GenomeValidationTarget.FloatValue(geneIndex, type),
                issues
            )
        }
    }

    private fun scalar(
        gene: GeneIndex,
        field: GenomeScalarField,
        value: Float,
        minimum: Float,
        maximum: Float,
        issues: MutableList<GenomeValidationIssue>
    ) = finiteRange(
        value,
        minimum,
        maximum,
        GenomeValidationTarget.Scalar(gene, field),
        issues
    )

    private fun finiteRange(
        value: Float,
        minimum: Float,
        maximum: Float,
        target: GenomeValidationTarget,
        issues: MutableList<GenomeValidationIssue>
    ) {
        val reason = when {
            !value.isFinite() -> GenomeValidationReason.NON_FINITE
            value < minimum || value > maximum -> GenomeValidationReason.OUT_OF_RANGE
            else -> null
        }
        if (reason != null) issues += GenomeValidationIssue(target, reason)
    }

    private fun index(
        value: Int,
        upperBound: Int,
        target: GenomeValidationTarget,
        issues: MutableList<GenomeValidationIssue>
    ) = integerRange(
        value,
        0,
        upperBound - 1,
        target,
        issues,
        GenomeValidationReason.INVALID_INDEX
    )

    private fun integerRange(
        value: Int,
        minimum: Int,
        maximum: Int,
        target: GenomeValidationTarget,
        issues: MutableList<GenomeValidationIssue>,
        reason: GenomeValidationReason = GenomeValidationReason.OUT_OF_RANGE
    ) {
        if (value !in minimum..maximum) {
            issues += GenomeValidationIssue(target, reason)
        }
    }
}
