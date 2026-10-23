package com.cytarch.cellularity.shared.model

import kotlin.math.sqrt

/** Stable indices, counts and ranges shared by the editor and the native genome bridge. */
object GenomeContract {
    const val WIRE_MAGIC = 0x43474457 // CGDW
    const val WIRE_SCHEMA_VERSION = 2
    const val MIN_GENE_COUNT = 1
    const val MAX_GENE_COUNT = CellDataConstants.MAX_GENOME_GENE_COUNT
    const val COLOR_COUNT = 3
    const val PARAMETRIC_VALUE_COUNT = 11
    const val INTEGER_VALUE_COUNT = 11
    const val FLOAT_VALUE_COUNT = 7
    const val WIRE_HEADER_BYTES = 6 * Int.SIZE_BYTES
    const val WIRE_BYTES_PER_GENE =
        (COLOR_COUNT + 7) * Float.SIZE_BYTES +
            2 * Int.SIZE_BYTES +
            7 +
            Int.SIZE_BYTES +
            PARAMETRIC_VALUE_COUNT * (3 * Float.SIZE_BYTES + 2 * Short.SIZE_BYTES) +
            INTEGER_VALUE_COUNT * Int.SIZE_BYTES +
            FLOAT_VALUE_COUNT * Float.SIZE_BYTES
    const val MAX_WIRE_BYTES = WIRE_HEADER_BYTES + MAX_GENE_COUNT * WIRE_BYTES_PER_GENE

    const val FORMULA_TYPE_COUNT = 4
    const val SIGNAL_INPUT_COUNT =
        CellDataConstants.SIGNAL_SUBSTANCE_COUNT + CellDataConstants.AUX_SIGNAL_SUBSTANCE_COUNT

    const val SWIM_FORCE_VALUE_INDEX = 0
    const val DENSITY_VALUE_INDEX = 1
    const val MYOCYTE_BENDING_VALUE_INDEX = 2
    const val MYOCYTE_CONTRACTION_VALUE_INDEX = 3
    const val MYOCYTE_LIFTING_VALUE_INDEX = 4
    const val STEM_THRESHOLD_1_VALUE_INDEX = 5
    const val STEM_THRESHOLD_2_VALUE_INDEX = 6
    const val GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX = 7

    const val VIRUS_FROM_INTEGER_INDEX = 0
    const val VIRUS_TO_INTEGER_INDEX = 1
    const val SMELL_OUT_INTEGER_INDEX = 2
    const val SMELL_TYPE_INTEGER_INDEX = 3
    const val SECRETE_TYPE_INTEGER_INDEX = 4
    const val GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX = 5
    const val MAX_CONNECTION_COUNT_INDEX =
        GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + CellDataConstants.NEUROCYTE_OUTPUT_COUNT
    const val TELOMERASE_LENGTH_INDEX = MAX_CONNECTION_COUNT_INDEX + 1

    const val SECRETE_TYPE_FOOD_SMELL = 0
    const val SECRETE_TYPE_CYANIDE = 1
    const val SECRETE_TYPE_LIPASE = 2
    const val SECRETE_TYPE_PROTEASE = 3
    const val SECRETE_TYPE_COATED_FOOD_SMELL = 4
    const val SECRETE_TYPE_LIGHT_SMELL = 5
    const val SECRETE_TYPE_WALL_SMELL = 6
    const val SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT = 7

    val telomeraseLengths: List<Int> = immutableListCopy(listOf(-1, 5, 10, 20, 40, 80))

    val integerUpperBounds: List<Int> = immutableListCopy(
        listOf(
            MAX_GENE_COUNT,
            MAX_GENE_COUNT,
            CellDataConstants.SIGNAL_SUBSTANCE_COUNT,
            CellDataConstants.SMELL_TYPE_COUNT,
            SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT + 2 * CellDataConstants.SIGNAL_SUBSTANCE_COUNT,
            CellDataConstants.SIGNAL_SUBSTANCE_COUNT,
            CellDataConstants.SIGNAL_SUBSTANCE_COUNT,
            CellDataConstants.SIGNAL_SUBSTANCE_COUNT,
            CellDataConstants.SIGNAL_SUBSTANCE_COUNT,
            CellDataConstants.MAX_NEIGHBORS,
            telomeraseLengths.size
        )
    )

    val parametricMinimums: List<Float> = immutableListCopy(
        listOf(
            0.0f,
            -3.0f,
            -1.0f,
            -0.5f,
            -1.0f,
            -1.0f,
            -1.0f,
            -CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
            -CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
            -CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
            -CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT
        )
    )

    val parametricMaximums: List<Float> = immutableListCopy(
        listOf(
            CellDataConstants.MAX_SWIM_FORCE,
            3.0f,
            1.0f,
            0.5f,
            1.0f,
            1.0f,
            1.0f,
            CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
            CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
            CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
            CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT
        )
    )

    fun floatRange(type: GeneFloatType): GenomeFloatRange = when (type) {
        GeneFloatType.SMELL_AMPLITUDE -> GenomeFloatRange(
            -3.0f * CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
            3.0f * CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT
        )
        GeneFloatType.SMELL_RED,
        GeneFloatType.SMELL_GREEN,
        GeneFloatType.SMELL_BLUE -> GenomeFloatRange(0.0f, 1.0f)
        GeneFloatType.SMELL_THRESHOLD -> GenomeFloatRange(0.0f, sqrt(3.0).toFloat())
        GeneFloatType.ADHESIN_LENGTH -> GenomeFloatRange(0.0f, 0.01f)
        GeneFloatType.CYTOSKELETON -> GenomeFloatRange(0.5f, 2.0f)
    }
}

enum class GeneFloatType(val wireIndex: Int) {
    SMELL_AMPLITUDE(0),
    SMELL_RED(1),
    SMELL_GREEN(2),
    SMELL_BLUE(3),
    SMELL_THRESHOLD(4),
    ADHESIN_LENGTH(5),
    CYTOSKELETON(6);

    companion object {
        fun fromWireIndex(index: Int): GeneFloatType =
            entries.firstOrNull { it.wireIndex == index }
                ?: throw IllegalArgumentException("Unknown gene float value index: $index")
    }
}

data class GenomeFloatRange(val min: Float, val max: Float)
