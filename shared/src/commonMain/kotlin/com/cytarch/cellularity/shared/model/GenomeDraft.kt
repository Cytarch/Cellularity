package com.cytarch.cellularity.shared.model

data class ParametricValueDraft(
    val variableA: Float,
    val variableB: Float,
    val variableC: Float,
    val signalSubstanceChannelIndex: Short,
    val formulaType: Short
)

data class GeneFloatValues(
    val smellAmplitude: Float,
    val smellRed: Float,
    val smellGreen: Float,
    val smellBlue: Float,
    val smellThreshold: Float,
    val adhesinLength: Float,
    val cytoskeleton: Float
) {
    operator fun get(type: GeneFloatType): Float = when (type) {
        GeneFloatType.SMELL_AMPLITUDE -> smellAmplitude
        GeneFloatType.SMELL_RED -> smellRed
        GeneFloatType.SMELL_GREEN -> smellGreen
        GeneFloatType.SMELL_BLUE -> smellBlue
        GeneFloatType.SMELL_THRESHOLD -> smellThreshold
        GeneFloatType.ADHESIN_LENGTH -> adhesinLength
        GeneFloatType.CYTOSKELETON -> cytoskeleton
    }

    fun withValue(type: GeneFloatType, value: Float): GeneFloatValues = when (type) {
        GeneFloatType.SMELL_AMPLITUDE -> copy(smellAmplitude = value)
        GeneFloatType.SMELL_RED -> copy(smellRed = value)
        GeneFloatType.SMELL_GREEN -> copy(smellGreen = value)
        GeneFloatType.SMELL_BLUE -> copy(smellBlue = value)
        GeneFloatType.SMELL_THRESHOLD -> copy(smellThreshold = value)
        GeneFloatType.ADHESIN_LENGTH -> copy(adhesinLength = value)
        GeneFloatType.CYTOSKELETON -> copy(cytoskeleton = value)
    }
}

/** Immutable editable representation. Runtime mutation remains native-owned. */
class GeneDraft(
    color: Iterable<Float>,
    val splitMass: Float,
    val splitRatio: Float,
    val splitAngle: Float,
    val child1Angle: Float,
    val child2Angle: Float,
    val massFlowPriority: Float,
    val linkStrength: Float,
    val child1GeneIndex: Int,
    val child2GeneIndex: Int,
    val makeLink: Boolean,
    val child1KeepLinks: Boolean,
    val child2KeepLinks: Boolean,
    val stayAlive: Boolean,
    val initial: Boolean,
    val child1Mirror: Boolean,
    val child2Mirror: Boolean,
    val cellType: CellType,
    parametricValues: Iterable<ParametricValueDraft>,
    integerValues: Iterable<Int>,
    val floatValues: GeneFloatValues
) {
    val color: List<Float> = immutableListCopy(color)
    val parametricValues: List<ParametricValueDraft> = immutableListCopy(parametricValues)
    val integerValues: List<Int> = immutableListCopy(integerValues)

    init {
        require(this.color.size == GenomeContract.COLOR_COUNT) {
            "A gene must contain ${GenomeContract.COLOR_COUNT} color components"
        }
        require(this.parametricValues.size == GenomeContract.PARAMETRIC_VALUE_COUNT) {
            "A gene must contain ${GenomeContract.PARAMETRIC_VALUE_COUNT} parametric values"
        }
        require(this.integerValues.size == GenomeContract.INTEGER_VALUE_COUNT) {
            "A gene must contain ${GenomeContract.INTEGER_VALUE_COUNT} integer values"
        }
    }

    @Suppress("LongParameterList")
    fun copy(
        color: Iterable<Float> = this.color,
        splitMass: Float = this.splitMass,
        splitRatio: Float = this.splitRatio,
        splitAngle: Float = this.splitAngle,
        child1Angle: Float = this.child1Angle,
        child2Angle: Float = this.child2Angle,
        massFlowPriority: Float = this.massFlowPriority,
        linkStrength: Float = this.linkStrength,
        child1GeneIndex: Int = this.child1GeneIndex,
        child2GeneIndex: Int = this.child2GeneIndex,
        makeLink: Boolean = this.makeLink,
        child1KeepLinks: Boolean = this.child1KeepLinks,
        child2KeepLinks: Boolean = this.child2KeepLinks,
        stayAlive: Boolean = this.stayAlive,
        initial: Boolean = this.initial,
        child1Mirror: Boolean = this.child1Mirror,
        child2Mirror: Boolean = this.child2Mirror,
        cellType: CellType = this.cellType,
        parametricValues: Iterable<ParametricValueDraft> = this.parametricValues,
        integerValues: Iterable<Int> = this.integerValues,
        floatValues: GeneFloatValues = this.floatValues
    ): GeneDraft = GeneDraft(
        color = color,
        splitMass = splitMass,
        splitRatio = splitRatio,
        splitAngle = splitAngle,
        child1Angle = child1Angle,
        child2Angle = child2Angle,
        massFlowPriority = massFlowPriority,
        linkStrength = linkStrength,
        child1GeneIndex = child1GeneIndex,
        child2GeneIndex = child2GeneIndex,
        makeLink = makeLink,
        child1KeepLinks = child1KeepLinks,
        child2KeepLinks = child2KeepLinks,
        stayAlive = stayAlive,
        initial = initial,
        child1Mirror = child1Mirror,
        child2Mirror = child2Mirror,
        cellType = cellType,
        parametricValues = parametricValues,
        integerValues = integerValues,
        floatValues = floatValues
    )

    fun withColor(index: Int, value: Float): GeneDraft {
        require(index in color.indices) { "Unknown gene color index: $index" }
        return copy(color = color.mapIndexed { currentIndex, current ->
            if (currentIndex == index) value else current
        })
    }

    fun withColor(red: Float, green: Float, blue: Float): GeneDraft =
        copy(color = listOf(red, green, blue))

    fun withParametricValue(index: Int, value: ParametricValueDraft): GeneDraft {
        require(index in parametricValues.indices) { "Unknown parametric value index: $index" }
        return copy(parametricValues = parametricValues.mapIndexed { currentIndex, current ->
            if (currentIndex == index) value else current
        })
    }

    fun withIntegerValue(index: Int, value: Int): GeneDraft {
        require(index in integerValues.indices) { "Unknown gene integer index: $index" }
        return copy(integerValues = integerValues.mapIndexed { currentIndex, current ->
            if (currentIndex == index) value else current
        })
    }

    fun withFloatValue(type: GeneFloatType, value: Float): GeneDraft =
        copy(floatValues = floatValues.withValue(type, value))

}

class GenomeDraft(genes: Iterable<GeneDraft>) {
    val genes: List<GeneDraft> = immutableListCopy(genes)

    init {
        require(this.genes.size in GenomeContract.MIN_GENE_COUNT..GenomeContract.MAX_GENE_COUNT) {
            "A genome must contain between ${GenomeContract.MIN_GENE_COUNT} and " +
                "${GenomeContract.MAX_GENE_COUNT} genes"
        }
    }

    fun withGene(index: Int, gene: GeneDraft): GenomeDraft {
        require(index in genes.indices) { "Unknown genome gene index: $index" }
        return GenomeDraft(genes.mapIndexed { currentIndex, current ->
            if (currentIndex == index) gene else current
        })
    }

    fun withOnlyInitial(index: Int): GenomeDraft {
        require(index in genes.indices) { "Unknown initial gene index: $index" }
        return GenomeInitialGeneCompatibility.apply(this, GenomeMetadata(index))
    }

    fun copyGene(from: Int, to: Int): GenomeDraft {
        require(from in genes.indices) { "Unknown source gene index: $from" }
        require(to in genes.indices) { "Unknown target gene index: $to" }
        return withGene(to, genes[from].copy(initial = genes[to].initial))
    }

    /** Inserts a gene expressed in the resulting index domain. */
    fun insertGene(index: Int, gene: GeneDraft): GenomeDraft {
        require(genes.size < GenomeContract.MAX_GENE_COUNT) { "Genome is at its safety limit" }
        require(index in 0..genes.size) { "Unknown insertion index: $index" }
        val oldToNew = IntArray(genes.size) { old -> if (old < index) old else old + 1 }
        val remapped = genes.mapIndexed { oldOwner, oldGene ->
            oldGene.remapReferences(oldOwner, oldToNew[oldOwner], oldToNew)
        }.toMutableList()
        remapped.add(index, gene.copy(initial = false))
        return GenomeDraft(remapped)
    }

    /** Duplicates a gene immediately after it; self-references stay self-references. */
    fun duplicateGene(index: Int): GenomeDraft {
        require(index in genes.indices) { "Unknown source gene index: $index" }
        require(genes.size < GenomeContract.MAX_GENE_COUNT) { "Genome is at its safety limit" }
        val insertionIndex = index + 1
        val oldToNew = IntArray(genes.size) { old -> if (old < insertionIndex) old else old + 1 }
        val duplicate = genes[index].remapReferences(
            oldOwner = index,
            newOwner = insertionIndex,
            oldToNew = oldToNew,
            selfReplacement = insertionIndex
        ).copy(initial = false)
        val remapped = genes.mapIndexed { oldOwner, gene ->
            gene.remapReferences(oldOwner, oldToNew[oldOwner], oldToNew)
        }.toMutableList()
        remapped.add(insertionIndex, duplicate)
        return GenomeDraft(remapped)
    }

    /**
     * Removes one gene and redirects every reference to it to an explicitly
     * selected surviving gene.
     */
    fun deleteGene(index: Int, replacement: Int): GenomeDraft {
        require(genes.size > GenomeContract.MIN_GENE_COUNT) { "The last gene cannot be deleted" }
        require(index in genes.indices) { "Unknown deleted gene index: $index" }
        require(replacement in genes.indices && replacement != index) {
            "Deletion replacement must be a different existing gene"
        }
        val replacementAfterDeletion = if (replacement < index) replacement else replacement - 1
        val oldToNew = IntArray(genes.size) { old ->
            when {
                old == index -> replacementAfterDeletion
                old < index -> old
                else -> old - 1
            }
        }
        val deletedWasInitial = genes[index].initial
        val remapped = genes.mapIndexedNotNull { oldOwner, gene ->
            if (oldOwner == index) {
                null
            } else {
                val newOwner = oldToNew[oldOwner]
                gene.remapReferences(oldOwner, newOwner, oldToNew).copy(
                    initial = if (deletedWasInitial) newOwner == replacementAfterDeletion else gene.initial
                )
            }
        }
        return GenomeDraft(remapped)
    }

    /** Moves one gene while preserving the meaning of every local reference. */
    fun moveGene(from: Int, to: Int): GenomeDraft {
        require(from in genes.indices) { "Unknown source gene index: $from" }
        require(to in genes.indices) { "Unknown target gene index: $to" }
        if (from == to) return this

        val order = genes.indices.toMutableList().also { indices ->
            indices.add(to, indices.removeAt(from))
        }
        val oldToNew = IntArray(genes.size)
        order.forEachIndexed { newIndex, oldIndex -> oldToNew[oldIndex] = newIndex }
        return GenomeDraft(order.mapIndexed { newOwner, oldOwner ->
            genes[oldOwner].remapReferences(oldOwner, newOwner, oldToNew)
        })
    }

    fun indexAfterDeletion(index: Int, deleted: Int, replacement: Int): Int = when {
        index == deleted -> if (replacement < deleted) replacement else replacement - 1
        index < deleted -> index
        else -> index - 1
    }

    fun indexAfterMove(index: Int, from: Int, to: Int): Int = when {
        index == from -> to
        from < to && index in (from + 1)..to -> index - 1
        to < from && index in to until from -> index + 1
        else -> index
    }

    companion object {
        fun editorDefaults(colorForGene: (Int) -> Iterable<Float>): GenomeDraft {
            val metadata = GenomeMetadata(initialGeneIndex = 0)
            return GenomeDraft(
                List(GenomeContract.MIN_GENE_COUNT) { geneIndex ->
                    EditorGeneTemplate.create(
                        geneIndex = geneIndex,
                        color = colorForGene(geneIndex),
                        metadata = metadata
                    )
                }
            )
        }
    }
}

private fun GeneDraft.remapReferences(
    oldOwner: Int,
    newOwner: Int,
    oldToNew: IntArray,
    selfReplacement: Int = newOwner
): GeneDraft {
    fun remap(reference: Int): Int {
        require(reference in oldToNew.indices) { "Gene reference is outside the source genome" }
        return if (reference == oldOwner) selfReplacement else oldToNew[reference]
    }

    val remappedIntegers = integerValues.toMutableList().also { values ->
        values[GenomeContract.VIRUS_FROM_INTEGER_INDEX] =
            remap(values[GenomeContract.VIRUS_FROM_INTEGER_INDEX])
        values[GenomeContract.VIRUS_TO_INTEGER_INDEX] =
            remap(values[GenomeContract.VIRUS_TO_INTEGER_INDEX])
    }
    return copy(
        child1GeneIndex = remap(child1GeneIndex),
        child2GeneIndex = remap(child2GeneIndex),
        integerValues = remappedIntegers
    )
}
