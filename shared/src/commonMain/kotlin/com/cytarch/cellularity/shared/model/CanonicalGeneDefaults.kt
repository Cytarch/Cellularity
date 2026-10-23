package com.cytarch.cellularity.shared.model

/** Deterministic, position-independent default for the current pre-release schema. */
object CanonicalGeneDefaults {
    fun materialize(geneIndex: Int): GeneDraft {
        require(geneIndex in 0 until GenomeContract.MAX_GENE_COUNT) {
            "Unknown canonical gene index: $geneIndex"
        }
        val integers = MutableList(GenomeContract.INTEGER_VALUE_COUNT) { 0 }
        integers[GenomeContract.VIRUS_FROM_INTEGER_INDEX] = geneIndex
        integers[GenomeContract.VIRUS_TO_INTEGER_INDEX] = geneIndex
        integers[GenomeContract.MAX_CONNECTION_COUNT_INDEX] =
            GenomeContract.integerUpperBounds[GenomeContract.MAX_CONNECTION_COUNT_INDEX] - 1
        return GeneDraft(
            color = listOf(0.0f, 0.0f, 0.0f),
            splitMass = CellDataConstants.MIN_MASS.toFloat() * 3.5f,
            splitRatio = 0.5f,
            splitAngle = 0.0f,
            child1Angle = 0.0f,
            child2Angle = 0.0f,
            massFlowPriority = 1.0f,
            linkStrength = 0.0f,
            child1GeneIndex = geneIndex,
            child2GeneIndex = geneIndex,
            makeLink = false,
            child1KeepLinks = true,
            child2KeepLinks = true,
            stayAlive = true,
            initial = false,
            child1Mirror = false,
            child2Mirror = false,
            cellType = CellType.PHAGOCYTE,
            parametricValues = List(GenomeContract.PARAMETRIC_VALUE_COUNT) {
                ParametricValueDraft(0.0f, 0.0f, 0.0f, 0, 0)
            },
            integerValues = integers,
            floatValues = GeneFloatValues(
                smellAmplitude = 0.0f,
                smellRed = 0.0f,
                smellGreen = 0.0f,
                smellBlue = 0.0f,
                smellThreshold = 0.0f,
                adhesinLength = 0.0f,
                cytoskeleton = 1.0f
            )
        )
    }
}

/** The editor template adds only creation-time presentation choices. */
object EditorGeneTemplate {
    fun create(
        geneIndex: Int,
        color: Iterable<Float>,
        metadata: GenomeMetadata
    ): GeneDraft = CanonicalGeneDefaults.materialize(geneIndex).copy(
        color = color,
        initial = geneIndex == metadata.initialGeneIndex
    )
}
