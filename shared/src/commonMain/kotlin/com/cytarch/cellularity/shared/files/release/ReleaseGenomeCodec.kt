package com.cytarch.cellularity.shared.files.release

import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GeneFloatValues
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.model.GenomeInitialGeneCompatibility
import com.cytarch.cellularity.shared.model.ParametricValueDraft

enum class EmbeddedPreviewFormat {
    NONE,
    PNG,
    JPEG,
    WEBP_LOSSY,
    WEBP_LOSSLESS,
    UNKNOWN
}

data class EmbeddedPreviewDescriptor(
    val format: EmbeddedPreviewFormat,
    val pixelWidth: Int,
    val pixelHeight: Int,
    val byteOffset: Long,
    val byteCount: Long,
    val encodedCrc32: Long,
    val usable: Boolean
)

data class ReleaseGenomeFileSummary(
    val containerMajor: Int,
    val containerMinor: Int,
    val contentSchemaMajor: Int,
    val contentSchemaMinor: Int,
    val geneCount: Int,
    val initialGeneIndex: Int,
    val uniqueGeneCount: Int?,
    val cellTypeMask: Long?,
    val bodySupported: Boolean,
    val preview: EmbeddedPreviewDescriptor
)

fun interface ReleaseGenomeRangeReader {
    fun read(offset: Long, byteCount: Int): ByteArray
}

/** Bounded metadata/preview inspection that never reads body columns. */
object ReleaseGenomeFileInspector {
    fun inspect(bytes: ByteArray): ReleaseGenomeFileSummary {
        val reader = ReleaseGenomeRangeReader { offset, byteCount ->
            bytes.checkedSlice(offset, byteCount.toLong(), "range")
        }
        val summary = inspectMetadata(bytes.size.toLong(), reader)
        if (!summary.preview.usable) return summary
        return summary.copy(
            preview = summary.preview.copy(
                usable = readPreview(summary, reader) != null
            )
        )
    }

    /** Reads only the fixed bootstrap and the bounded metadata block. */
    fun inspectMetadata(
        fileByteCount: Long,
        reader: ReleaseGenomeRangeReader
    ): ReleaseGenomeFileSummary {
        val bootstrap = reader.read(0, SaveContainerContract.BOOTSTRAP_BYTES)
        if (bootstrap.size != SaveContainerContract.BOOTSTRAP_BYTES) {
            formatFailure(
                CellularityErrorCode.TRUNCATED_INPUT,
                "Release genome bootstrap is truncated"
            )
        }
        val header = SaveContainerCodec.decodeBootstrap(
            bootstrap,
            fileByteCount
        )
        if (header.documentKind != DocumentKind.GENOME) {
            formatFailure(
                CellularityErrorCode.INVALID_HEADER,
                "CLTY document is not a standalone genome"
            )
        }
        val metadataByteCount = header.metadataBytes.toBoundedInt("metadata byte count")
        val metadataBytes = reader.read(header.metadataOffset, metadataByteCount)
        if (metadataBytes.size != metadataByteCount) {
            formatFailure(
                CellularityErrorCode.TRUNCATED_INPUT,
                "Release genome metadata is truncated"
            )
        }
        val metadata = SaveContainerCodec.decodeMetadata(
            DocumentKind.GENOME,
            metadataBytes,
            header.metadataCrc32
        )
        val card = metadata.genomeCard(requireSupportedSchema = false)
        val previewFormat = when (header.previewCodec) {
            PreviewCodec.NONE.wireId -> EmbeddedPreviewFormat.NONE
            PreviewCodec.PNG.wireId -> EmbeddedPreviewFormat.PNG
            PreviewCodec.JPEG.wireId -> EmbeddedPreviewFormat.JPEG
            PreviewCodec.WEBP_LOSSY.wireId -> EmbeddedPreviewFormat.WEBP_LOSSY
            PreviewCodec.WEBP_LOSSLESS.wireId -> EmbeddedPreviewFormat.WEBP_LOSSLESS
            else -> EmbeddedPreviewFormat.UNKNOWN
        }
        val previewPotentiallyUsable = previewFormat != EmbeddedPreviewFormat.NONE &&
            previewFormat != EmbeddedPreviewFormat.UNKNOWN &&
            header.previewFlags == 0
        return ReleaseGenomeFileSummary(
            containerMajor = header.containerMajor,
            containerMinor = header.containerMinor,
            contentSchemaMajor = card.schemaMajor,
            contentSchemaMinor = card.schemaMinor,
            geneCount = card.geneCount,
            initialGeneIndex = card.initialGeneIndex,
            uniqueGeneCount = card.uniqueGeneCount,
            cellTypeMask = card.cellTypeMask,
            bodySupported = card.schemaMajor == RELEASE_GENOME_SCHEMA_MAJOR,
            preview = EmbeddedPreviewDescriptor(
                format = previewFormat,
                pixelWidth = header.previewPixelWidth.toBoundedInt("preview width"),
                pixelHeight = header.previewPixelHeight.toBoundedInt("preview height"),
                byteOffset = header.previewOffset,
                byteCount = header.previewBytes,
                encodedCrc32 = header.previewCrc32.toLong() and 0xffff_ffffL,
                usable = previewPotentiallyUsable
            )
        )
    }

    /** Reads and verifies only the embedded image selected by [summary]. */
    fun readPreview(
        summary: ReleaseGenomeFileSummary,
        reader: ReleaseGenomeRangeReader
    ): ByteArray? {
        val preview = summary.preview
        if (!preview.usable || preview.byteCount <= 0 || preview.byteCount > Int.MAX_VALUE) {
            return null
        }
        val bytes = reader.read(preview.byteOffset, preview.byteCount.toInt())
        if (bytes.size.toLong() != preview.byteCount) return null
        val actualCrc32 = SaveContainerCodec.crc32(bytes).toLong() and 0xffff_ffffL
        return bytes.takeIf { actualCrc32 == preview.encodedCrc32 }
    }
}

/** Deterministic Kotlin reference implementation used for raw golden parity. */
internal object ReleaseGenomeRawCodec {
    fun encode(
        genome: GenomeDraft,
        preview: EncodedPreview = EncodedPreview()
    ): ByteArray = SaveContainerBodyCodec.encodeRaw(
        snapshot = genome.toLogicalSnapshot(),
        preview = preview
    )

    fun decode(bytes: ByteArray): GenomeDraft {
        val decoded = SaveContainerBodyCodec.decodeRaw(bytes)
        if (decoded.header.documentKind != DocumentKind.GENOME) {
            formatFailure(
                CellularityErrorCode.INVALID_HEADER,
                "CLTY document is not a standalone genome"
            )
        }
        val card = decoded.metadata.genomeCard(requireSupportedSchema = true)
        if (decoded.sections.size != 2) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release genome has an invalid section inventory"
            )
        }
        val genesSection = decoded.sections.requiredSection(
            SaveContainerContract.SectionId.GENES,
            "genes",
            GENE_COLUMN_COUNT
        )
        val genomesSection = decoded.sections.requiredSection(
            SaveContainerContract.SectionId.GENOMES,
            "genomes",
            3
        )
        val references = genomesSection.requiredColumn(
            2,
            PrimitiveType.U32,
            "gene_refs",
            count = null
        )
        val bodyGeneCount = references.logicalValueCount.toBoundedInt("body gene count")
        val uniqueCount = genesSection.logicalRowCount.toBoundedInt("unique gene count")
        if (
            bodyGeneCount !in GenomeContract.MIN_GENE_COUNT..GenomeContract.MAX_GENE_COUNT ||
            card.geneCount != bodyGeneCount ||
            uniqueCount !in 1..bodyGeneCount ||
            genomesSection.logicalRowCount != 1L ||
            card.uniqueGeneCount != null && card.uniqueGeneCount != uniqueCount
        ) {
            formatFailure(
                CellularityErrorCode.INVALID_COUNT,
                "Release genome gene/genome counts are inconsistent"
            )
        }

        val offsets = genomesSection.requiredColumn(
            1,
            PrimitiveType.U64,
            "gene_ref_offsets",
            2
        )
        val initial = genomesSection.requiredColumn(
            3,
            PrimitiveType.U16,
            "initial_gene",
            1
        )
        if (
            offsets.u64(0) != 0L ||
            offsets.u64(1) != bodyGeneCount.toLong() ||
            initial.u16(0) != card.initialGeneIndex
        ) {
            formatFailure(
                CellularityErrorCode.INVALID_COUNT,
                "Release genome CSR offsets or initial index are invalid"
            )
        }

        val columns = GeneColumnSet.bind(genesSection, uniqueCount)
        val rows = List(uniqueCount) { row -> columns.readRow(row) }
        val used = BooleanArray(uniqueCount)
        val genes = List(bodyGeneCount) { localIndex ->
            val row = references.u32(localIndex)
            if (row !in rows.indices) {
                formatFailure(
                    CellularityErrorCode.INVALID_VALUE,
                    "Release genome contains an out-of-range file gene reference"
                )
            }
            used[row] = true
            rows[row].materialize(localIndex, localIndex == card.initialGeneIndex)
        }
        if (used.any { !it }) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release genome contains an unreferenced unique gene row"
            )
        }
        val genome = GenomeDraft(genes)
        val canonical = genome.toLogicalSnapshot()
        if (!canonical.sections.logicalEquals(decoded.sections)) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release genome body is not in canonical deterministic form"
            )
        }
        val computedMask = canonical.metadata
            .single { it.fieldId == SaveContainerContract.MetadataField.GENOME_CELL_TYPE_MASK }
            .asU32()
        if (card.cellTypeMask != null && card.cellTypeMask != computedMask) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release genome cell type metadata does not match its body"
            )
        }
        return genome
    }
}

private const val SELF_GENE_REFERENCE = 0xffff
private const val GENE_COLUMN_COUNT = 87
private const val KNOWN_BEHAVIOR_FLAGS = 0x3f

private val parametricNames = listOf(
    "swim_force",
    "density",
    "myocyte_bending",
    "myocyte_contraction",
    "myocyte_lifting",
    "stem_threshold_1",
    "stem_threshold_2",
    "signal_output_0",
    "signal_output_1",
    "signal_output_2",
    "signal_output_3"
)

private val integerNames = listOf(
    "virus_from_gene",
    "virus_to_gene",
    "smell_output",
    "smell_type",
    "secretion_type",
    "signal_output_channel_0",
    "signal_output_channel_1",
    "signal_output_channel_2",
    "signal_output_channel_3",
    "max_connection_count",
    "telomerase_length"
)

private val floatNames = listOf(
    "smell_amplitude",
    "smell_red",
    "smell_green",
    "smell_blue",
    "smell_threshold",
    "adhesin_length",
    "cytoskeleton"
)

private data class GenomeCard(
    val schemaMajor: Int,
    val schemaMinor: Int,
    val geneCount: Int,
    val initialGeneIndex: Int,
    val uniqueGeneCount: Int?,
    val cellTypeMask: Long?
)

private fun List<MetadataEntry>.genomeCard(
    requireSupportedSchema: Boolean
): GenomeCard {
    fun required(id: Int): MetadataEntry = firstOrNull { it.fieldId == id }
        ?: formatFailure(
            CellularityErrorCode.INVALID_VALUE,
            "Release genome is missing required metadata"
        )
    fun optional(id: Int): MetadataEntry? = firstOrNull { it.fieldId == id }

    val schemaMajor = required(
        SaveContainerContract.MetadataField.CONTENT_SCHEMA_MAJOR
    ).asU32().toBoundedInt("content schema major")
    val schemaMinor = required(
        SaveContainerContract.MetadataField.CONTENT_SCHEMA_MINOR
    ).asU32().toBoundedInt("content schema minor")
    if (schemaMajor == 0) {
        formatFailure(
            CellularityErrorCode.INVALID_VERSION,
            "Release genome content schema major version is zero"
        )
    }
    if (requireSupportedSchema && schemaMajor != RELEASE_GENOME_SCHEMA_MAJOR) {
        formatFailure(
            CellularityErrorCode.NEWER_SNAPSHOT_VERSION,
            "Release genome content schema is unsupported"
        )
    }
    val geneCount = required(
        SaveContainerContract.MetadataField.GENOME_GENE_COUNT
    ).asU32().toBoundedInt("gene count")
    val initialGene = required(
        SaveContainerContract.MetadataField.GENOME_INITIAL_GENE_INDEX
    ).asU32().toBoundedInt("initial gene index")
    val unique = optional(
        SaveContainerContract.MetadataField.GENOME_UNIQUE_GENE_COUNT
    )?.asU32()?.toBoundedInt("unique gene count")
    val mask = optional(
        SaveContainerContract.MetadataField.GENOME_CELL_TYPE_MASK
    )?.asU32()
    if (
        geneCount !in GenomeContract.MIN_GENE_COUNT..GenomeContract.MAX_GENE_COUNT ||
        initialGene !in 0 until geneCount ||
        unique != null && unique !in 1..geneCount
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_COUNT,
            "Release genome metadata contains invalid counts"
        )
    }
    return GenomeCard(schemaMajor, schemaMinor, geneCount, initialGene, unique, mask)
}

private const val RELEASE_GENOME_SCHEMA_MAJOR = 1

private data class CanonicalGenome(
    val uniqueGenes: List<GeneDraft>,
    val references: List<Int>,
    val initialGeneIndex: Int
)

private fun GenomeDraft.canonicalGenome(): CanonicalGenome {
    val metadata = GenomeInitialGeneCompatibility.extract(this)
        ?: formatFailure(
            CellularityErrorCode.INVALID_COUNT,
            "Release genome must contain exactly one initial gene"
        )
    val unique = mutableListOf<GeneDraft>()
    val keys = mutableListOf<ByteArray>()
    val references = genes.mapIndexed { owner, gene ->
        requireGeneReferences(gene, genes.size)
        val canonical = gene.canonicalized(owner)
        val key = GenomeDraftWireCodec.encode(GenomeDraft(listOf(canonical)))
        val known = keys.indexOfFirst { it.contentEquals(key) }
        if (known >= 0) {
            known
        } else {
            keys += key
            unique += canonical
            unique.lastIndex
        }
    }
    return CanonicalGenome(unique, references, metadata.initialGeneIndex)
}

private fun GenomeDraft.toLogicalSnapshot(): LogicalSnapshot {
    val genome = canonicalGenome()
    var cellTypes = 0L
    genome.uniqueGenes.forEach { gene ->
        if (gene.cellType.nativeValue in 0 until 32) {
            cellTypes = cellTypes or (1L shl gene.cellType.nativeValue)
        }
    }
    val metadata = listOf(
        MetadataEntry.u32(
            SaveContainerContract.MetadataField.CONTENT_SCHEMA_MAJOR,
            "content_schema_major",
            1,
            true
        ),
        MetadataEntry.u32(
            SaveContainerContract.MetadataField.CONTENT_SCHEMA_MINOR,
            "content_schema_minor",
            0,
            true
        ),
        MetadataEntry.u32(
            SaveContainerContract.MetadataField.GENOME_GENE_COUNT,
            "gene_count",
            genes.size.toLong(),
            true
        ),
        MetadataEntry.u32(
            SaveContainerContract.MetadataField.GENOME_INITIAL_GENE_INDEX,
            "initial_gene_index",
            genome.initialGeneIndex.toLong(),
            true
        ),
        MetadataEntry.u32(
            SaveContainerContract.MetadataField.GENOME_UNIQUE_GENE_COUNT,
            "unique_gene_count",
            genome.uniqueGenes.size.toLong(),
            false
        ),
        MetadataEntry.u32(
            SaveContainerContract.MetadataField.GENOME_CELL_TYPE_MASK,
            "cell_type_mask",
            cellTypes,
            false
        )
    )
    return LogicalSnapshot(
        documentKind = DocumentKind.GENOME,
        metadata = metadata,
        sections = listOf(genome.geneSection(), genome.genomeSection())
    )
}

private fun CanonicalGenome.geneSection(): LogicalSection {
    val columns = mutableListOf<LogicalColumn>()
    fun f32(id: Int, name: String, value: (GeneDraft) -> Float) {
        columns += scalarColumn(id, PrimitiveType.F32, name, uniqueGenes) {
            value(it).canonicalZero().toRawBits().toLong() and 0xffff_ffffL
        }
    }
    fun u16(id: Int, name: String, value: (GeneDraft) -> Int) {
        columns += scalarColumn(id, PrimitiveType.U16, name, uniqueGenes) {
            value(it).also { field ->
                if (field !in 0..0xffff) {
                    formatFailure(
                        CellularityErrorCode.INVALID_VALUE,
                        "Release genome U16 field is out of range"
                    )
                }
            }.toLong()
        }
    }
    fun u8(id: Int, name: String, value: (GeneDraft) -> Int) {
        columns += scalarColumn(id, PrimitiveType.U8, name, uniqueGenes) {
            value(it).also { field ->
                if (field !in 0..0xff) {
                    formatFailure(
                        CellularityErrorCode.INVALID_VALUE,
                        "Release genome U8 field is out of range"
                    )
                }
            }.toLong()
        }
    }
    f32(1, "color_r") { it.color[0] }
    f32(2, "color_g") { it.color[1] }
    f32(3, "color_b") { it.color[2] }
    f32(4, "split_mass", GeneDraft::splitMass)
    f32(5, "split_ratio", GeneDraft::splitRatio)
    f32(6, "split_angle", GeneDraft::splitAngle)
    f32(7, "child_1_angle", GeneDraft::child1Angle)
    f32(8, "child_2_angle", GeneDraft::child2Angle)
    f32(9, "mass_flow_priority", GeneDraft::massFlowPriority)
    f32(10, "link_strength", GeneDraft::linkStrength)
    u16(11, "child_1_gene", GeneDraft::child1GeneIndex)
    u16(12, "child_2_gene", GeneDraft::child2GeneIndex)
    u8(13, "behavior_flags", GeneDraft::behaviorFlags)
    u8(14, "cell_type") { it.cellType.nativeValue }

    parametricNames.forEachIndexed { index, prefix ->
        val firstId = 100 + index * 5
        f32(firstId, "${prefix}_variable_a") { it.parametricValues[index].variableA }
        f32(firstId + 1, "${prefix}_variable_b") { it.parametricValues[index].variableB }
        f32(firstId + 2, "${prefix}_variable_c") { it.parametricValues[index].variableC }
        u8(firstId + 3, "${prefix}_signal_channel") {
            it.parametricValues[index].signalSubstanceChannelIndex.toInt()
        }
        u8(firstId + 4, "${prefix}_formula_type") {
            it.parametricValues[index].formulaType.toInt()
        }
    }
    integerNames.forEachIndexed { index, name ->
        u16(200 + index, name) { it.integerValues[index] }
    }
    floatNames.forEachIndexed { index, name ->
        f32(300 + index, name) { gene -> gene.floatValues.asList()[index] }
    }
    check(columns.size == GENE_COLUMN_COUNT)
    return LogicalSection(
        sectionId = SaveContainerContract.SectionId.GENES,
        canonicalName = "genes",
        logicalRowCount = uniqueGenes.size.toLong(),
        columns = columns
    )
}

private fun CanonicalGenome.genomeSection(): LogicalSection = LogicalSection(
    sectionId = SaveContainerContract.SectionId.GENOMES,
    canonicalName = "genomes",
    logicalRowCount = 1,
    columns = listOf(
        scalarColumn(
            1,
            PrimitiveType.U64,
            "gene_ref_offsets",
            listOf(0L, references.size.toLong())
        ) { it },
        scalarColumn(
            2,
            PrimitiveType.U32,
            "gene_refs",
            references
        ) { it.toLong() },
        scalarColumn(
            3,
            PrimitiveType.U16,
            "initial_gene",
            listOf(initialGeneIndex)
        ) { it.toLong() }
    )
)

private fun <T> scalarColumn(
    id: Int,
    type: PrimitiveType,
    name: String,
    values: List<T>,
    value: (T) -> Long
): LogicalColumn {
    val bytes = ByteArray(values.size * type.byteCount)
    values.forEachIndexed { index, item ->
        var bits = value(item)
        repeat(type.byteCount) { byte ->
            bytes[index * type.byteCount + byte] = bits.toByte()
            bits = bits ushr 8
        }
    }
    return LogicalColumn(id, type, name, values.size.toLong(), bytes)
}

private fun GeneDraft.canonicalized(owner: Int): GeneDraft {
    fun reference(value: Int): Int = if (value == owner) SELF_GENE_REFERENCE else value
    val canonicalIntegers = integerValues.mapIndexed { index, value ->
        if (
            index == GenomeContract.VIRUS_FROM_INTEGER_INDEX ||
            index == GenomeContract.VIRUS_TO_INTEGER_INDEX
        ) reference(value) else value
    }
    return copy(
        color = color.map(Float::canonicalZero),
        splitMass = splitMass.canonicalZero(),
        splitRatio = splitRatio.canonicalZero(),
        splitAngle = splitAngle.canonicalZero(),
        child1Angle = child1Angle.canonicalZero(),
        child2Angle = child2Angle.canonicalZero(),
        massFlowPriority = massFlowPriority.canonicalZero(),
        linkStrength = linkStrength.canonicalZero(),
        child1GeneIndex = reference(child1GeneIndex),
        child2GeneIndex = reference(child2GeneIndex),
        initial = false,
        parametricValues = parametricValues.map { parameter ->
            parameter.copy(
                variableA = parameter.variableA.canonicalZero(),
                variableB = parameter.variableB.canonicalZero(),
                variableC = parameter.variableC.canonicalZero()
            )
        },
        integerValues = canonicalIntegers,
        floatValues = GeneFloatValues(
            smellAmplitude = floatValues.smellAmplitude.canonicalZero(),
            smellRed = floatValues.smellRed.canonicalZero(),
            smellGreen = floatValues.smellGreen.canonicalZero(),
            smellBlue = floatValues.smellBlue.canonicalZero(),
            smellThreshold = floatValues.smellThreshold.canonicalZero(),
            adhesinLength = floatValues.adhesinLength.canonicalZero(),
            cytoskeleton = floatValues.cytoskeleton.canonicalZero()
        )
    )
}

private fun GeneDraft.materialize(owner: Int, initial: Boolean): GeneDraft {
    fun reference(value: Int): Int = if (value == SELF_GENE_REFERENCE) owner else value
    return copy(
        child1GeneIndex = reference(child1GeneIndex),
        child2GeneIndex = reference(child2GeneIndex),
        initial = initial,
        integerValues = integerValues.mapIndexed { index, value ->
            if (
                index == GenomeContract.VIRUS_FROM_INTEGER_INDEX ||
                index == GenomeContract.VIRUS_TO_INTEGER_INDEX
            ) reference(value) else value
        }
    )
}

private fun requireGeneReferences(gene: GeneDraft, geneCount: Int) {
    val references = listOf(
        gene.child1GeneIndex,
        gene.child2GeneIndex,
        gene.integerValues[GenomeContract.VIRUS_FROM_INTEGER_INDEX],
        gene.integerValues[GenomeContract.VIRUS_TO_INTEGER_INDEX]
    )
    if (references.any { it !in 0 until geneCount }) {
        formatFailure(
            CellularityErrorCode.INVALID_VALUE,
            "Release genome contains an invalid local gene reference"
        )
    }
}

private val GeneDraft.behaviorFlags: Int
    get() =
        (if (makeLink) 1 shl 0 else 0) or
            (if (child1KeepLinks) 1 shl 1 else 0) or
            (if (child2KeepLinks) 1 shl 2 else 0) or
            (if (stayAlive) 1 shl 3 else 0) or
            (if (child1Mirror) 1 shl 4 else 0) or
            (if (child2Mirror) 1 shl 5 else 0)

private fun GeneFloatValues.asList(): List<Float> = listOf(
    smellAmplitude,
    smellRed,
    smellGreen,
    smellBlue,
    smellThreshold,
    adhesinLength,
    cytoskeleton
)

private fun Float.canonicalZero(): Float = if (this == 0.0f) 0.0f else this

private class GeneColumnSet(
    private val core: List<LogicalColumn>,
    private val parametric: List<List<LogicalColumn>>,
    private val integers: List<LogicalColumn>,
    private val floats: List<LogicalColumn>
) {
    fun readRow(row: Int): GeneDraft {
        val flags = core[12].u8(row)
        if (flags and KNOWN_BEHAVIOR_FLAGS.inv() != 0) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release genome gene has unknown behavior flags"
            )
        }
        return try {
            GeneDraft(
                color = List(3) { core[it].f32(row) },
                splitMass = core[3].f32(row),
                splitRatio = core[4].f32(row),
                splitAngle = core[5].f32(row),
                child1Angle = core[6].f32(row),
                child2Angle = core[7].f32(row),
                massFlowPriority = core[8].f32(row),
                linkStrength = core[9].f32(row),
                child1GeneIndex = core[10].u16(row),
                child2GeneIndex = core[11].u16(row),
                makeLink = flags and (1 shl 0) != 0,
                child1KeepLinks = flags and (1 shl 1) != 0,
                child2KeepLinks = flags and (1 shl 2) != 0,
                stayAlive = flags and (1 shl 3) != 0,
                initial = false,
                child1Mirror = flags and (1 shl 4) != 0,
                child2Mirror = flags and (1 shl 5) != 0,
                cellType = CellType.fromNativeValue(core[13].u8(row)),
                parametricValues = parametric.map { values ->
                    ParametricValueDraft(
                        variableA = values[0].f32(row),
                        variableB = values[1].f32(row),
                        variableC = values[2].f32(row),
                        signalSubstanceChannelIndex = values[3].u8(row).toShort(),
                        formulaType = values[4].u8(row).toShort()
                    )
                },
                integerValues = integers.map { it.u16(row) },
                floatValues = GeneFloatValues(
                    smellAmplitude = floats[0].f32(row),
                    smellRed = floats[1].f32(row),
                    smellGreen = floats[2].f32(row),
                    smellBlue = floats[3].f32(row),
                    smellThreshold = floats[4].f32(row),
                    adhesinLength = floats[5].f32(row),
                    cytoskeleton = floats[6].f32(row)
                )
            )
        } catch (failure: IllegalArgumentException) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                failure.message ?: "Release genome contains an invalid gene row"
            )
        }
    }

    companion object {
        fun bind(section: LogicalSection, count: Int): GeneColumnSet {
            val coreNames = listOf(
                "color_r",
                "color_g",
                "color_b",
                "split_mass",
                "split_ratio",
                "split_angle",
                "child_1_angle",
                "child_2_angle",
                "mass_flow_priority",
                "link_strength",
                "child_1_gene",
                "child_2_gene",
                "behavior_flags",
                "cell_type"
            )
            val core = coreNames.mapIndexed { index, name ->
                val type = when (index) {
                    in 0..9 -> PrimitiveType.F32
                    10, 11 -> PrimitiveType.U16
                    else -> PrimitiveType.U8
                }
                section.requiredColumn(index + 1, type, name, count)
            }
            val parametric = parametricNames.mapIndexed { parameter, prefix ->
                val first = 100 + parameter * 5
                listOf(
                    section.requiredColumn(first, PrimitiveType.F32, "${prefix}_variable_a", count),
                    section.requiredColumn(first + 1, PrimitiveType.F32, "${prefix}_variable_b", count),
                    section.requiredColumn(first + 2, PrimitiveType.F32, "${prefix}_variable_c", count),
                    section.requiredColumn(first + 3, PrimitiveType.U8, "${prefix}_signal_channel", count),
                    section.requiredColumn(first + 4, PrimitiveType.U8, "${prefix}_formula_type", count)
                )
            }
            val integers = integerNames.mapIndexed { index, name ->
                section.requiredColumn(200 + index, PrimitiveType.U16, name, count)
            }
            val floats = floatNames.mapIndexed { index, name ->
                section.requiredColumn(300 + index, PrimitiveType.F32, name, count)
            }
            return GeneColumnSet(core, parametric, integers, floats)
        }
    }
}

private fun List<LogicalSection>.requiredSection(
    id: Int,
    name: String,
    columnCount: Int
): LogicalSection {
    val section = singleOrNull { it.sectionId == id }
    if (
        section == null || section.canonicalName != name ||
        section.schemaMajor != 1 ||
        section.columns.size != columnCount
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_VALUE,
            "Release genome section inventory does not match schema"
        )
    }
    return section
}

private fun LogicalSection.requiredColumn(
    id: Int,
    type: PrimitiveType,
    name: String,
    count: Int?
): LogicalColumn {
    val column = columns.singleOrNull { it.fieldId == id }
    if (
        column == null || column.primitiveType != type ||
        column.canonicalName != name ||
        count != null && column.logicalValueCount != count.toLong()
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_VALUE,
            "Release genome column inventory does not match schema"
        )
    }
    return column
}

private fun LogicalColumn.u8(index: Int): Int = unsigned(index, 1).toInt()
private fun LogicalColumn.u16(index: Int): Int = unsigned(index, 2).toInt()
private fun LogicalColumn.u32(index: Int): Int {
    val value = unsigned(index, 4)
    if (value > Int.MAX_VALUE) {
        formatFailure(
            CellularityErrorCode.LIMIT_EXCEEDED,
            "Release genome U32 value exceeds the runtime domain"
        )
    }
    return value.toInt()
}

private fun LogicalColumn.u64(index: Int): Long = unsigned(index, 8)
private fun LogicalColumn.f32(index: Int): Float = Float.fromBits(unsigned(index, 4).toInt())

private fun LogicalColumn.unsigned(index: Int, width: Int): Long {
    if (
        primitiveType.byteCount != width || index < 0 ||
        index >= logicalValueCount || index > Int.MAX_VALUE / width ||
        index * width > decodedBytes.size - width
    ) {
        formatFailure(
            CellularityErrorCode.TRUNCATED_INPUT,
            "Release genome column value is truncated"
        )
    }
    var value = 0L
    repeat(width) { byte ->
        value = value or ((decodedBytes[index * width + byte].toLong() and 0xffL) shl (byte * 8))
    }
    return value
}

private fun List<LogicalSection>.logicalEquals(other: List<LogicalSection>): Boolean {
    if (size != other.size) return false
    return indices.all { sectionIndex ->
        val left = this[sectionIndex]
        val right = other[sectionIndex]
        left.sectionId == right.sectionId &&
            left.schemaMajor == right.schemaMajor &&
            left.canonicalName == right.canonicalName &&
            left.logicalRowCount == right.logicalRowCount &&
            left.columns.size == right.columns.size &&
            left.columns.indices.all { columnIndex ->
                val a = left.columns[columnIndex]
                val b = right.columns[columnIndex]
                a.fieldId == b.fieldId &&
                    a.primitiveType == b.primitiveType &&
                    a.canonicalName == b.canonicalName &&
                    a.logicalValueCount == b.logicalValueCount &&
                    a.decodedBytes.contentEquals(b.decodedBytes)
            }
    }
}

private fun ByteArray.checkedSlice(offset: Long, count: Long, label: String): ByteArray {
    if (
        offset < 0 || count < 0 || offset > size.toLong() ||
        count > size.toLong() - offset || offset > Int.MAX_VALUE ||
        offset + count > Int.MAX_VALUE
    ) {
        formatFailure(
            CellularityErrorCode.TRUNCATED_INPUT,
            "Release genome $label range is truncated"
        )
    }
    return copyOfRange(offset.toInt(), (offset + count).toInt())
}

private fun Long.toBoundedInt(label: String): Int {
    if (this !in 0..Int.MAX_VALUE.toLong()) {
        formatFailure(
            CellularityErrorCode.LIMIT_EXCEEDED,
            "Release genome $label exceeds the runtime domain"
        )
    }
    return toInt()
}
