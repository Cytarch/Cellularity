package com.cytarch.cellularity.shared.files.release

import com.cytarch.cellularity.shared.model.CellularityErrorCode

/**
 * Frozen release-container primitives.
 *
 * Public compatibility begins only when the first stable content schema is
 * explicitly published.
 */
internal object SaveContainerContract {
    const val BOOTSTRAP_BYTES = 112
    const val CONTAINER_MAJOR = 1
    const val CONTAINER_MINOR = 0
    const val DIRECTORY_FORMAT_VERSION = 1

    const val MAXIMUM_FILE_BYTES = 2L * 1024L * 1024L * 1024L
    const val MAXIMUM_METADATA_BYTES = 64 * 1024
    const val MAXIMUM_METADATA_ENTRIES = 128
    const val MAXIMUM_PREVIEW_BYTES = 8L * 1024L * 1024L
    const val MAXIMUM_PREVIEW_DIMENSION = 4096
    const val MAXIMUM_PREVIEW_PIXELS = 16L * 1024L * 1024L
    const val MAXIMUM_DIRECTORY_BYTES = 4 * 1024 * 1024
    const val MAXIMUM_SECTIONS = 64
    const val MAXIMUM_COLUMNS_PER_SECTION = 512
    const val MAXIMUM_CHUNKS = 65536
    const val MAXIMUM_DECODED_CHUNK_BYTES = 4L * 1024L * 1024L
    const val MAXIMUM_NAME_BYTES = 63

    const val CRITICAL_FLAG = 1
    const val DESCRIPTOR_CRITICAL_FLAG = 1

    object MetadataField {
        const val CONTENT_SCHEMA_MAJOR = 1
        const val CONTENT_SCHEMA_MINOR = 2
        const val SAVED_AT_UNIX_MILLIS = 3

        const val SUBSTRATE_SIMULATION_TIME_HOURS = 0x1001
        const val SUBSTRATE_CELL_COUNT = 0x1002
        const val SUBSTRATE_FOOD_COUNT = 0x1003
        const val SUBSTRATE_FOOD_MASS = 0x1004
        const val SUBSTRATE_DISH_RADIUS = 0x1005
        const val SUBSTRATE_MAXIMUM_CELL_COUNT = 0x1006
        const val SUBSTRATE_MAXIMUM_FOOD_COUNT = 0x1007
        const val SUBSTRATE_GENOME_COUNT = 0x1008
        const val SUBSTRATE_UNIQUE_GENE_COUNT = 0x1009
        const val SUBSTRATE_TOPOLOGY_EDGE_COUNT = 0x100A
        const val SUBSTRATE_DEAD_SPLASH_COUNT = 0x100B
        const val SUBSTRATE_CREATED_AT_UNIX_MILLIS = 0x100C
        const val SUBSTRATE_KNOWN_ORGANISM_COUNT = 0x100D
        const val SUBSTRATE_ORGANISM_IDENTITY_COMPLETE = 0x100E
        const val SUBSTRATE_NEXT_ORGANISM_ID = 0x100F

        const val GENOME_GENE_COUNT = 0x2001
        const val GENOME_INITIAL_GENE_INDEX = 0x2002
        const val GENOME_UNIQUE_GENE_COUNT = 0x2003
        const val GENOME_CELL_TYPE_MASK = 0x2004
    }

    object SectionId {
        const val ENVIRONMENT = 1
        const val GENES = 2
        const val GENOMES = 3
        const val CELLS = 4
        const val TOPOLOGY_EDGES = 5
        const val TOPOLOGY_BOUNDARIES = 6
        const val FOOD = 7
        const val DEAD_SPLASHES = 8
    }
}

internal enum class DocumentKind(val wireId: Int) {
    SUBSTRATE(1),
    GENOME(2);

    companion object {
        fun fromWireId(wireId: Int): DocumentKind =
            entries.firstOrNull { it.wireId == wireId }
                ?: formatFailure(
                    CellularityErrorCode.UNSUPPORTED_FEATURE,
                    "Unsupported release save document kind"
                )
    }
}

internal enum class MetadataValueType(val wireId: Int, val byteCount: Int) {
    U32(1, 4),
    U64(2, 8),
    I64(3, 8),
    F64(4, 8);

    companion object {
        fun fromWireId(wireId: Int): MetadataValueType? =
            entries.firstOrNull { it.wireId == wireId }
    }
}

internal enum class PreviewCodec(val wireId: Int) {
    NONE(0),
    PNG(1),
    JPEG(2),
    WEBP_LOSSY(3),
    WEBP_LOSSLESS(4),
    AVIF(5)
}

internal enum class PreviewDisposition {
    NONE,
    AVAILABLE,
    PLACEHOLDER
}

internal enum class CompressionCodec(val wireId: Int) {
    NONE(0),
    ZLIB(1),
    ZSTD(2)
}

internal enum class PrimitiveType(val wireId: Int, val byteCount: Int) {
    U8(1, 1),
    U16(2, 2),
    U32(3, 4),
    U64(4, 8),
    I32(5, 4),
    I64(6, 8),
    F32(7, 4),
    F64(8, 8),
    I8(9, 1);

    companion object {
        fun fromWireId(wireId: Int): PrimitiveType? =
            entries.firstOrNull { it.wireId == wireId }
    }
}

internal enum class PhysicalEncoding(val wireId: Int) {
    PLAIN(0),
    CONSTANT(1);

    companion object {
        fun fromWireId(wireId: Int): PhysicalEncoding? =
            entries.firstOrNull { it.wireId == wireId }
    }
}

internal data class BootstrapHeader(
    val containerMajor: Int = SaveContainerContract.CONTAINER_MAJOR,
    val containerMinor: Int = SaveContainerContract.CONTAINER_MINOR,
    val documentKind: DocumentKind = DocumentKind.SUBSTRATE,
    val headerBytes: Int = SaveContainerContract.BOOTSTRAP_BYTES,
    val requiredFeatureBits: Long = 0,
    val optionalFeatureBits: Long = 0,
    val headerFlags: Long = 0,
    val declaredFileBytes: Long = 0,
    val metadataOffset: Long = 0,
    val metadataBytes: Long = 0,
    val metadataCrc32: Int = 0,
    val previewCodec: Int = PreviewCodec.NONE.wireId,
    val previewFlags: Int = 0,
    val previewPixelWidth: Long = 0,
    val previewPixelHeight: Long = 0,
    val previewOffset: Long = 0,
    val previewBytes: Long = 0,
    val previewCrc32: Int = 0,
    val reserved0: Long = 0,
    val directoryOffset: Long = 0,
    val directoryBytes: Long = 0,
    val directoryCrc32: Int = 0
)

internal data class MetadataEntry(
    val fieldId: Int,
    val valueType: MetadataValueType,
    val critical: Boolean,
    val canonicalName: String,
    val rawBits: Long
) {
    fun asU32(): Long {
        require(valueType == MetadataValueType.U32)
        return rawBits
    }

    fun asU64(): Long {
        require(valueType == MetadataValueType.U64)
        return rawBits
    }

    fun asI64(): Long {
        require(valueType == MetadataValueType.I64)
        return rawBits
    }

    fun asF64(): Double {
        require(valueType == MetadataValueType.F64)
        return Double.fromBits(rawBits)
    }

    companion object {
        fun u32(
            fieldId: Int,
            canonicalName: String,
            value: Long,
            critical: Boolean
        ) = MetadataEntry(
            fieldId,
            MetadataValueType.U32,
            critical,
            canonicalName,
            value
        )

        fun u64(
            fieldId: Int,
            canonicalName: String,
            value: Long,
            critical: Boolean
        ) = MetadataEntry(
            fieldId,
            MetadataValueType.U64,
            critical,
            canonicalName,
            value
        )

        fun i64(
            fieldId: Int,
            canonicalName: String,
            value: Long,
            critical: Boolean
        ) = MetadataEntry(
            fieldId,
            MetadataValueType.I64,
            critical,
            canonicalName,
            value
        )

        fun f64(
            fieldId: Int,
            canonicalName: String,
            value: Double,
            critical: Boolean
        ) = MetadataEntry(
            fieldId,
            MetadataValueType.F64,
            critical,
            canonicalName,
            value.toBits()
        )
    }
}

internal data class ChunkDescriptor(
    val firstLogicalValue: Long,
    val logicalValueCount: Long,
    val compressionCodec: Int = CompressionCodec.NONE.wireId,
    val compressionFlags: Int = 0,
    val codecParameter: Long = 0,
    val fileOffset: Long,
    val storedBytes: Long,
    val decodedBytes: Long,
    val decodedCrc32: Int,
    val reserved: Long = 0
)

internal data class ColumnDescriptor(
    val fieldId: Int,
    val primitiveType: Int,
    val physicalEncoding: Int,
    val fieldFlags: Int = SaveContainerContract.DESCRIPTOR_CRITICAL_FLAG,
    val logicalValueCount: Long,
    val canonicalName: String,
    val chunks: List<ChunkDescriptor>
)

internal data class SectionDescriptor(
    val sectionId: Int,
    val schemaMajor: Int = 1,
    val schemaMinor: Int = 0,
    val sectionFlags: Long = SaveContainerContract.DESCRIPTOR_CRITICAL_FLAG.toLong(),
    val logicalRowCount: Long,
    val canonicalName: String,
    val columns: List<ColumnDescriptor>
)

class SaveContainerFormatException(
    val code: CellularityErrorCode,
    message: String
) : IllegalArgumentException(message)

internal object SaveContainerCodec {
    fun crc32(bytes: ByteArray, endExclusive: Int = bytes.size): Int {
        require(endExclusive in 0..bytes.size)
        var crc = -1
        for (index in 0 until endExclusive) {
            crc = crc xor (bytes[index].toInt() and 0xff)
            repeat(8) {
                crc = (crc ushr 1) xor (0xedb88320.toInt() and -(crc and 1))
            }
        }
        return crc.inv()
    }

    fun encodeBootstrap(header: BootstrapHeader): ByteArray {
        if (header.headerBytes != SaveContainerContract.BOOTSTRAP_BYTES) {
            formatFailure(
                CellularityErrorCode.INVALID_LENGTH,
                "Release writer supports only the 112-byte bootstrap"
            )
        }
        validateBootstrap(header, header.declaredFileBytes)
        val writer = LittleEndianWriter(SaveContainerContract.BOOTSTRAP_BYTES)
        writer.raw("CLTY".encodeToByteArray())
        writer.u16(header.containerMajor)
        writer.u16(header.containerMinor)
        writer.u16(header.documentKind.wireId)
        writer.u16(header.headerBytes)
        writer.u32(header.requiredFeatureBits)
        writer.u32(header.optionalFeatureBits)
        writer.u32(header.headerFlags)
        writer.u64(header.declaredFileBytes)
        writer.u64(header.metadataOffset)
        writer.u64(header.metadataBytes)
        writer.u32Bits(header.metadataCrc32)
        writer.u16(header.previewCodec)
        writer.u16(header.previewFlags)
        writer.u32(header.previewPixelWidth)
        writer.u32(header.previewPixelHeight)
        writer.u64(header.previewOffset)
        writer.u64(header.previewBytes)
        writer.u32Bits(header.previewCrc32)
        writer.u32(header.reserved0)
        writer.u64(header.directoryOffset)
        writer.u64(header.directoryBytes)
        writer.u32Bits(header.directoryCrc32)
        writer.u32(0)
        val bytes = writer.toByteArray()
        check(bytes.size == SaveContainerContract.BOOTSTRAP_BYTES)
        writer.patchU32Bits(108, crc32(bytes, 108))
        return writer.toByteArray()
    }

    fun decodeBootstrap(
        bootstrapBytes: ByteArray,
        actualFileBytes: Long
    ): BootstrapHeader {
        if (bootstrapBytes.size != SaveContainerContract.BOOTSTRAP_BYTES) {
            formatFailure(
                if (bootstrapBytes.size < SaveContainerContract.BOOTSTRAP_BYTES) {
                    CellularityErrorCode.TRUNCATED_INPUT
                } else {
                    CellularityErrorCode.INVALID_LENGTH
                },
                "Release bootstrap must contain exactly 112 bytes"
            )
        }
        if (!bootstrapBytes.copyOfRange(0, 4).contentEquals("CLTY".encodeToByteArray())) {
            formatFailure(
                CellularityErrorCode.INVALID_HEADER,
                "Release bootstrap magic is invalid"
            )
        }
        val expectedCrc = crc32(bootstrapBytes, 108)
        val reader = LittleEndianReader(bootstrapBytes)
        reader.skip(4)
        val containerMajor = reader.u16()
        val containerMinor = reader.u16()
        val documentKind = DocumentKind.fromWireId(reader.u16())
        val headerBytes = reader.u16()
        val requiredFeatureBits = reader.u32()
        val optionalFeatureBits = reader.u32()
        val headerFlags = reader.u32()
        val declaredFileBytes = reader.u64()
        val metadataOffset = reader.u64()
        val metadataBytes = reader.u64()
        val metadataCrc32 = reader.u32().toInt()
        val previewCodec = reader.u16()
        val previewFlags = reader.u16()
        val previewPixelWidth = reader.u32()
        val previewPixelHeight = reader.u32()
        val previewOffset = reader.u64()
        val previewBytes = reader.u64()
        val previewCrc32 = reader.u32().toInt()
        val reserved0 = reader.u32()
        val directoryOffset = reader.u64()
        val directoryBytes = reader.u64()
        val directoryCrc32 = reader.u32().toInt()
        val storedCrc = reader.u32().toInt()
        if (reader.remaining != 0 || storedCrc != expectedCrc) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release bootstrap CRC is invalid"
            )
        }
        return BootstrapHeader(
            containerMajor = containerMajor,
            containerMinor = containerMinor,
            documentKind = documentKind,
            headerBytes = headerBytes,
            requiredFeatureBits = requiredFeatureBits,
            optionalFeatureBits = optionalFeatureBits,
            headerFlags = headerFlags,
            declaredFileBytes = declaredFileBytes,
            metadataOffset = metadataOffset,
            metadataBytes = metadataBytes,
            metadataCrc32 = metadataCrc32,
            previewCodec = previewCodec,
            previewFlags = previewFlags,
            previewPixelWidth = previewPixelWidth,
            previewPixelHeight = previewPixelHeight,
            previewOffset = previewOffset,
            previewBytes = previewBytes,
            previewCrc32 = previewCrc32,
            reserved0 = reserved0,
            directoryOffset = directoryOffset,
            directoryBytes = directoryBytes,
            directoryCrc32 = directoryCrc32
        ).also { validateBootstrap(it, actualFileBytes) }
    }

    fun classifyPreview(
        header: BootstrapHeader,
        storedPreviewBytes: ByteArray
    ): PreviewDisposition {
        if (header.previewCodec == PreviewCodec.NONE.wireId) {
            return PreviewDisposition.NONE
        }
        val supportedCodec = when (header.previewCodec) {
            PreviewCodec.PNG.wireId,
            PreviewCodec.JPEG.wireId,
            PreviewCodec.WEBP_LOSSY.wireId,
            PreviewCodec.WEBP_LOSSLESS.wireId -> true
            else -> false
        }
        return if (
            supportedCodec &&
            header.previewFlags == 0 &&
            header.previewBytes == storedPreviewBytes.size.toLong() &&
            header.previewCrc32 == crc32(storedPreviewBytes)
        ) {
            PreviewDisposition.AVAILABLE
        } else {
            PreviewDisposition.PLACEHOLDER
        }
    }

    fun encodeMetadata(
        documentKind: DocumentKind,
        entries: List<MetadataEntry>
    ): ByteArray {
        if (entries.size > SaveContainerContract.MAXIMUM_METADATA_ENTRIES) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release metadata contains too many entries"
            )
        }
        val sorted = entries.sortedBy(MetadataEntry::fieldId)
        val ids = mutableSetOf<Int>()
        val names = mutableSetOf<String>()
        val writer = LittleEndianWriter(SaveContainerContract.MAXIMUM_METADATA_BYTES)
        writer.u32(sorted.size.toLong())
        sorted.forEach { entry ->
            validateMetadataEntry(documentKind, entry, allowUnknownOptional = false)
            if (!ids.add(entry.fieldId) || !names.add(entry.canonicalName)) {
                formatFailure(
                    CellularityErrorCode.INVALID_VALUE,
                    "Release metadata contains a duplicate field"
                )
            }
            writer.u32(entry.fieldId.toLong())
            writer.u8(entry.valueType.wireId)
            writer.u8(if (entry.critical) SaveContainerContract.CRITICAL_FLAG else 0)
            writer.u16(entry.canonicalName.length)
            writer.u32(entry.valueType.byteCount.toLong())
            writer.raw(entry.canonicalName.encodeToByteArray())
            if (entry.valueType == MetadataValueType.U32) {
                writer.u32(entry.rawBits)
            } else {
                writer.i64Bits(entry.rawBits)
            }
        }
        requireMetadataFields(documentKind, ids)
        return writer.toByteArray()
    }

    fun decodeMetadata(
        documentKind: DocumentKind,
        bytes: ByteArray,
        expectedCrc32: Int
    ): List<MetadataEntry> {
        if (bytes.size > SaveContainerContract.MAXIMUM_METADATA_BYTES) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release metadata exceeds its byte limit"
            )
        }
        if (crc32(bytes) != expectedCrc32) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release metadata CRC is invalid"
            )
        }
        val reader = LittleEndianReader(bytes)
        val count = reader.u32AsInt("metadata entry count")
        if (count > SaveContainerContract.MAXIMUM_METADATA_ENTRIES) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release metadata contains too many entries"
            )
        }
        val ids = mutableSetOf<Int>()
        val names = mutableSetOf<String>()
        val entries = ArrayList<MetadataEntry>(count)
        repeat(count) {
            val fieldId = reader.u32AsInt("metadata field ID")
            val valueTypeId = reader.u8()
            val flags = reader.u8()
            val nameBytes = reader.u16()
            val valueBytes = reader.u32AsInt("metadata value size")
            if (
                flags and SaveContainerContract.CRITICAL_FLAG.inv() != 0 ||
                nameBytes > SaveContainerContract.MAXIMUM_NAME_BYTES
            ) {
                formatFailure(
                    CellularityErrorCode.INVALID_VALUE,
                    "Release metadata entry has invalid flags or name size"
                )
            }
            val name = reader.text(nameBytes)
            validateName(name, "metadata field")
            if (!ids.add(fieldId) || !names.add(name)) {
                formatFailure(
                    CellularityErrorCode.INVALID_VALUE,
                    "Release metadata contains a duplicate field"
                )
            }
            val critical = flags and SaveContainerContract.CRITICAL_FLAG != 0
            val definition = metadataFields.firstOrNull { it.id == fieldId }
            if (definition == null) {
                if (critical) {
                    formatFailure(
                        CellularityErrorCode.UNSUPPORTED_FEATURE,
                        "Release metadata contains an unknown critical field"
                    )
                }
                reader.skip(valueBytes)
                return@repeat
            }
            val valueType = MetadataValueType.fromWireId(valueTypeId)
            if (
                documentKind !in definition.documentKinds ||
                name != definition.name ||
                valueType != definition.type ||
                critical != definition.required ||
                valueBytes != definition.type.byteCount
            ) {
                formatFailure(
                    CellularityErrorCode.INVALID_VALUE,
                    "Release metadata field does not match its registered schema"
                )
            }
            val rawBits = if (valueBytes == 4) reader.u32() else reader.i64Bits()
            val entry = MetadataEntry(
                fieldId,
                checkNotNull(valueType),
                critical,
                name,
                rawBits
            )
            validateMetadataEntry(documentKind, entry, allowUnknownOptional = true)
            entries += entry
        }
        if (reader.remaining != 0) {
            formatFailure(
                CellularityErrorCode.TRAILING_DATA,
                "Release metadata contains trailing bytes"
            )
        }
        requireMetadataFields(documentKind, ids)
        return entries
    }

    fun encodeDirectory(sections: List<SectionDescriptor>): ByteArray {
        validateDirectoryModel(sections, header = null)
        val writer = LittleEndianWriter(SaveContainerContract.MAXIMUM_DIRECTORY_BYTES)
        writer.u32(SaveContainerContract.DIRECTORY_FORMAT_VERSION.toLong())
        writer.u32(sections.size.toLong())
        sections.sortedBy(SectionDescriptor::sectionId).forEach { section ->
            writer.u32(section.sectionId.toLong())
            writer.u16(section.schemaMajor)
            writer.u16(section.schemaMinor)
            writer.u32(section.sectionFlags)
            writer.u64(section.logicalRowCount)
            writer.u32(section.canonicalName.length.toLong())
            writer.u32(section.columns.size.toLong())
            writer.raw(section.canonicalName.encodeToByteArray())
            section.columns.sortedBy(ColumnDescriptor::fieldId).forEach { column ->
                writer.u32(column.fieldId.toLong())
                writer.u8(column.primitiveType)
                writer.u8(column.physicalEncoding)
                writer.u16(column.fieldFlags)
                writer.u64(column.logicalValueCount)
                writer.u32(column.canonicalName.length.toLong())
                writer.u32(column.chunks.size.toLong())
                writer.raw(column.canonicalName.encodeToByteArray())
                column.chunks.forEach { chunk ->
                    writer.u64(chunk.firstLogicalValue)
                    writer.u64(chunk.logicalValueCount)
                    writer.u16(chunk.compressionCodec)
                    writer.u16(chunk.compressionFlags)
                    writer.u32(chunk.codecParameter)
                    writer.u64(chunk.fileOffset)
                    writer.u64(chunk.storedBytes)
                    writer.u64(chunk.decodedBytes)
                    writer.u32Bits(chunk.decodedCrc32)
                    writer.u32(chunk.reserved)
                }
            }
        }
        return writer.toByteArray()
    }

    fun decodeDirectory(
        bytes: ByteArray,
        expectedCrc32: Int,
        header: BootstrapHeader
    ): List<SectionDescriptor> {
        validateBootstrap(header, header.declaredFileBytes)
        if (bytes.size > SaveContainerContract.MAXIMUM_DIRECTORY_BYTES) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release directory exceeds its byte limit"
            )
        }
        if (
            bytes.size.toLong() != header.directoryBytes ||
            expectedCrc32 != header.directoryCrc32 ||
            crc32(bytes) != expectedCrc32
        ) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release directory size or CRC is invalid"
            )
        }
        val reader = LittleEndianReader(bytes)
        val version = reader.u32AsInt("directory version")
        if (version == 0) {
            formatFailure(
                CellularityErrorCode.INVALID_VERSION,
                "Release directory version is zero"
            )
        }
        if (version > SaveContainerContract.DIRECTORY_FORMAT_VERSION) {
            formatFailure(
                CellularityErrorCode.UNSUPPORTED_FEATURE,
                "Release directory requires a newer decoder"
            )
        }
        val sectionCount = reader.u32AsInt("section count")
        if (sectionCount > SaveContainerContract.MAXIMUM_SECTIONS) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release directory contains too many sections"
            )
        }
        val sections = ArrayList<SectionDescriptor>(sectionCount)
        repeat(sectionCount) {
            val sectionId = reader.u32AsInt("section ID")
            val schemaMajor = reader.u16()
            val schemaMinor = reader.u16()
            val sectionFlags = reader.u32()
            val logicalRowCount = reader.u64()
            val nameBytes = reader.u32AsInt("section name size")
            val columnCount = reader.u32AsInt("column count")
            if (
                nameBytes > SaveContainerContract.MAXIMUM_NAME_BYTES ||
                columnCount > SaveContainerContract.MAXIMUM_COLUMNS_PER_SECTION
            ) {
                formatFailure(
                    CellularityErrorCode.LIMIT_EXCEEDED,
                    "Release section descriptor exceeds its limit"
                )
            }
            val name = reader.text(nameBytes)
            val columns = ArrayList<ColumnDescriptor>(columnCount)
            repeat(columnCount) {
                val fieldId = reader.u32AsInt("field ID")
                val primitiveType = reader.u8()
                val physicalEncoding = reader.u8()
                val fieldFlags = reader.u16()
                val logicalValueCount = reader.u64()
                val columnNameBytes = reader.u32AsInt("column name size")
                val chunkCount = reader.u32AsInt("chunk count")
                if (
                    columnNameBytes > SaveContainerContract.MAXIMUM_NAME_BYTES ||
                    chunkCount > SaveContainerContract.MAXIMUM_CHUNKS
                ) {
                    formatFailure(
                        CellularityErrorCode.LIMIT_EXCEEDED,
                        "Release column descriptor exceeds its limit"
                    )
                }
                val columnName = reader.text(columnNameBytes)
                val chunks = ArrayList<ChunkDescriptor>(chunkCount)
                repeat(chunkCount) {
                    chunks += ChunkDescriptor(
                        firstLogicalValue = reader.u64(),
                        logicalValueCount = reader.u64(),
                        compressionCodec = reader.u16(),
                        compressionFlags = reader.u16(),
                        codecParameter = reader.u32(),
                        fileOffset = reader.u64(),
                        storedBytes = reader.u64(),
                        decodedBytes = reader.u64(),
                        decodedCrc32 = reader.u32().toInt(),
                        reserved = reader.u32()
                    )
                }
                columns += ColumnDescriptor(
                    fieldId,
                    primitiveType,
                    physicalEncoding,
                    fieldFlags,
                    logicalValueCount,
                    columnName,
                    chunks
                )
            }
            sections += SectionDescriptor(
                sectionId,
                schemaMajor,
                schemaMinor,
                sectionFlags,
                logicalRowCount,
                name,
                columns
            )
        }
        if (reader.remaining != 0) {
            formatFailure(
                CellularityErrorCode.TRAILING_DATA,
                "Release directory contains trailing bytes"
            )
        }
        validateDirectoryModel(sections, header)
        return sections
    }
}

private data class MetadataFieldDefinition(
    val id: Int,
    val name: String,
    val type: MetadataValueType,
    val documentKinds: Set<DocumentKind>,
    val required: Boolean
)

private val bothDocumentKinds = DocumentKind.entries.toSet()

private val metadataFields = listOf(
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.CONTENT_SCHEMA_MAJOR,
        "content_schema_major",
        MetadataValueType.U32,
        bothDocumentKinds,
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.CONTENT_SCHEMA_MINOR,
        "content_schema_minor",
        MetadataValueType.U32,
        bothDocumentKinds,
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SAVED_AT_UNIX_MILLIS,
        "saved_at_unix_millis",
        MetadataValueType.I64,
        bothDocumentKinds,
        false
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_SIMULATION_TIME_HOURS,
        "simulation_time_hours",
        MetadataValueType.F64,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_CELL_COUNT,
        "cell_count",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_FOOD_COUNT,
        "food_count",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_FOOD_MASS,
        "food_mass",
        MetadataValueType.F64,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_DISH_RADIUS,
        "dish_radius",
        MetadataValueType.F64,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_MAXIMUM_CELL_COUNT,
        "max_cell_count",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_MAXIMUM_FOOD_COUNT,
        "max_food_count",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_GENOME_COUNT,
        "genome_count",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        false
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_UNIQUE_GENE_COUNT,
        "unique_gene_count",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        false
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_TOPOLOGY_EDGE_COUNT,
        "topology_edge_count",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        false
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_DEAD_SPLASH_COUNT,
        "dead_splash_count",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        false
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_CREATED_AT_UNIX_MILLIS,
        "created_at_unix_millis",
        MetadataValueType.I64,
        setOf(DocumentKind.SUBSTRATE),
        false
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_KNOWN_ORGANISM_COUNT,
        "known_organism_count",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_ORGANISM_IDENTITY_COMPLETE,
        "organism_identity_complete",
        MetadataValueType.U32,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.SUBSTRATE_NEXT_ORGANISM_ID,
        "next_organism_id",
        MetadataValueType.U64,
        setOf(DocumentKind.SUBSTRATE),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.GENOME_GENE_COUNT,
        "gene_count",
        MetadataValueType.U32,
        setOf(DocumentKind.GENOME),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.GENOME_INITIAL_GENE_INDEX,
        "initial_gene_index",
        MetadataValueType.U32,
        setOf(DocumentKind.GENOME),
        true
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.GENOME_UNIQUE_GENE_COUNT,
        "unique_gene_count",
        MetadataValueType.U32,
        setOf(DocumentKind.GENOME),
        false
    ),
    MetadataFieldDefinition(
        SaveContainerContract.MetadataField.GENOME_CELL_TYPE_MASK,
        "cell_type_mask",
        MetadataValueType.U32,
        setOf(DocumentKind.GENOME),
        false
    )
)

private val sectionNames = mapOf(
    SaveContainerContract.SectionId.ENVIRONMENT to "environment",
    SaveContainerContract.SectionId.GENES to "genes",
    SaveContainerContract.SectionId.GENOMES to "genomes",
    SaveContainerContract.SectionId.CELLS to "cells",
    SaveContainerContract.SectionId.TOPOLOGY_EDGES to "topology_edges",
    SaveContainerContract.SectionId.TOPOLOGY_BOUNDARIES to "topology_boundaries",
    SaveContainerContract.SectionId.FOOD to "food",
    SaveContainerContract.SectionId.DEAD_SPLASHES to "dead_splashes"
)

private fun validateMetadataEntry(
    documentKind: DocumentKind,
    entry: MetadataEntry,
    allowUnknownOptional: Boolean
) {
    validateName(entry.canonicalName, "metadata field")
    val definition = metadataFields.firstOrNull { it.id == entry.fieldId }
    if (definition == null) {
        if (entry.critical || !allowUnknownOptional) {
            formatFailure(
                CellularityErrorCode.UNSUPPORTED_FEATURE,
                "Release metadata contains an unknown critical field"
            )
        }
        return
    }
    if (
        documentKind !in definition.documentKinds ||
        entry.canonicalName != definition.name ||
        entry.valueType != definition.type ||
        entry.critical != definition.required
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_VALUE,
            "Release metadata field does not match its registered schema"
        )
    }
    if (
        entry.valueType == MetadataValueType.U32 &&
        entry.rawBits !in 0..0xffff_ffffL
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_VALUE,
            "Release metadata U32 value is out of range"
        )
    }
    if (
        entry.valueType == MetadataValueType.U64 &&
        entry.rawBits < 0
    ) {
        formatFailure(
            CellularityErrorCode.LIMIT_EXCEEDED,
            "The Kotlin release codec supports U64 metadata up to Long.MAX_VALUE"
        )
    }
    if (
        entry.valueType == MetadataValueType.F64 &&
        !Double.fromBits(entry.rawBits).isFinite()
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_VALUE,
            "Release metadata floating value must be finite"
        )
    }
}

private fun requireMetadataFields(
    documentKind: DocumentKind,
    present: Set<Int>
) {
    metadataFields.forEach { field ->
        if (field.required && documentKind in field.documentKinds && field.id !in present) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release metadata is missing required field ${field.name}"
            )
        }
    }
}

private fun validateBootstrap(header: BootstrapHeader, actualFileBytes: Long) {
    if (header.containerMajor == 0) {
        formatFailure(
            CellularityErrorCode.INVALID_VERSION,
            "Release container major version is zero"
        )
    }
    if (header.containerMajor > SaveContainerContract.CONTAINER_MAJOR) {
        formatFailure(
            CellularityErrorCode.NEWER_SNAPSHOT_VERSION,
            "Release container requires a newer major version"
        )
    }
    if (header.headerBytes !in SaveContainerContract.BOOTSTRAP_BYTES..4096) {
        formatFailure(
            CellularityErrorCode.INVALID_LENGTH,
            "Release bootstrap byte count is invalid"
        )
    }
    if (header.requiredFeatureBits != 0L) {
        formatFailure(
            CellularityErrorCode.UNSUPPORTED_FEATURE,
            "Release container requires an unsupported feature"
        )
    }
    if (header.headerFlags != 0L || header.reserved0 != 0L) {
        formatFailure(
            CellularityErrorCode.INVALID_VALUE,
            "Release bootstrap has nonzero reserved flags"
        )
    }
    if (
        actualFileBytes < header.headerBytes ||
        actualFileBytes > SaveContainerContract.MAXIMUM_FILE_BYTES ||
        header.declaredFileBytes != actualFileBytes
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_LENGTH,
            "Release declared file size does not match the input"
        )
    }
    if (
        header.metadataBytes !in 1..SaveContainerContract.MAXIMUM_METADATA_BYTES.toLong() ||
        header.directoryBytes !in 8..SaveContainerContract.MAXIMUM_DIRECTORY_BYTES.toLong()
    ) {
        formatFailure(
            CellularityErrorCode.LIMIT_EXCEEDED,
            "Release metadata or directory exceeds its structural limit"
        )
    }

    val bootstrap = Region(0, header.headerBytes.toLong())
    val metadata = checkedRegion(
        header.metadataOffset,
        header.metadataBytes,
        actualFileBytes,
        "metadata"
    )
    val directory = checkedRegion(
        header.directoryOffset,
        header.directoryBytes,
        actualFileBytes,
        "directory"
    )
    if (
        bootstrap.overlaps(metadata) ||
        bootstrap.overlaps(directory) ||
        metadata.overlaps(directory)
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_LENGTH,
            "Release bootstrap regions overlap"
        )
    }

    if (header.previewCodec == PreviewCodec.NONE.wireId) {
        if (
            header.previewFlags != 0 ||
            header.previewPixelWidth != 0L ||
            header.previewPixelHeight != 0L ||
            header.previewOffset != 0L ||
            header.previewBytes != 0L ||
            header.previewCrc32 != 0
        ) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release NONE preview has nonzero descriptor fields"
            )
        }
        return
    }

    val pixels = checkedMultiply(
        header.previewPixelWidth,
        header.previewPixelHeight,
        "preview pixel count"
    )
    if (
        header.previewPixelWidth !in 1..SaveContainerContract.MAXIMUM_PREVIEW_DIMENSION.toLong() ||
        header.previewPixelHeight !in 1..SaveContainerContract.MAXIMUM_PREVIEW_DIMENSION.toLong() ||
        pixels > SaveContainerContract.MAXIMUM_PREVIEW_PIXELS ||
        header.previewBytes !in 1..SaveContainerContract.MAXIMUM_PREVIEW_BYTES
    ) {
        formatFailure(
            CellularityErrorCode.LIMIT_EXCEEDED,
            "Release preview exceeds its structural limit"
        )
    }
    val preview = checkedRegion(
        header.previewOffset,
        header.previewBytes,
        actualFileBytes,
        "preview"
    )
    if (
        bootstrap.overlaps(preview) ||
        metadata.overlaps(preview) ||
        directory.overlaps(preview)
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_LENGTH,
            "Release preview overlaps another container region"
        )
    }
}

private fun validateDirectoryModel(
    sections: List<SectionDescriptor>,
    header: BootstrapHeader?
) {
    if (sections.size > SaveContainerContract.MAXIMUM_SECTIONS) {
        formatFailure(
            CellularityErrorCode.LIMIT_EXCEEDED,
            "Release directory contains too many sections"
        )
    }
    val sectionIds = mutableSetOf<Int>()
    val sectionCanonicalNames = mutableSetOf<String>()
    val liveRegions = mutableListOf<Region>()
    if (header != null) {
        liveRegions += Region(0, header.headerBytes.toLong())
        liveRegions += Region(header.metadataOffset, header.metadataBytes)
        liveRegions += Region(header.directoryOffset, header.directoryBytes)
        if (header.previewCodec != PreviewCodec.NONE.wireId) {
            liveRegions += Region(header.previewOffset, header.previewBytes)
        }
    }

    var totalChunks = 0
    sections.forEach { section ->
        validateName(section.canonicalName, "section")
        if (!sectionIds.add(section.sectionId) || !sectionCanonicalNames.add(section.canonicalName)) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release directory contains a duplicate section"
            )
        }
        if (
            section.sectionFlags and
            SaveContainerContract.DESCRIPTOR_CRITICAL_FLAG.toLong().inv() != 0L ||
            section.schemaMajor == 0
        ) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release section has invalid flags or schema version"
            )
        }
        val sectionCritical =
            section.sectionFlags and SaveContainerContract.DESCRIPTOR_CRITICAL_FLAG.toLong() != 0L
        val knownName = sectionNames[section.sectionId]
        if (knownName == null) {
            if (sectionCritical) {
                formatFailure(
                    CellularityErrorCode.UNSUPPORTED_FEATURE,
                    "Release directory contains an unknown critical section"
                )
            }
        } else {
            if (section.canonicalName != knownName) {
                formatFailure(
                    CellularityErrorCode.INVALID_VALUE,
                    "Release section ID and name do not match"
                )
            }
            if (section.schemaMajor > 1 && sectionCritical) {
                formatFailure(
                    CellularityErrorCode.UNSUPPORTED_FEATURE,
                    "Release critical section requires a newer schema"
                )
            }
        }
        if (section.columns.size > SaveContainerContract.MAXIMUM_COLUMNS_PER_SECTION) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release section contains too many columns"
            )
        }

        val fieldIds = mutableSetOf<Int>()
        val fieldNames = mutableSetOf<String>()
        section.columns.forEach { column ->
            validateName(column.canonicalName, "column")
            if (!fieldIds.add(column.fieldId) || !fieldNames.add(column.canonicalName)) {
                formatFailure(
                    CellularityErrorCode.INVALID_VALUE,
                    "Release section contains a duplicate column"
                )
            }
            if (
                column.fieldFlags and
                SaveContainerContract.DESCRIPTOR_CRITICAL_FLAG.inv() != 0
            ) {
                formatFailure(
                    CellularityErrorCode.INVALID_VALUE,
                    "Release column has nonzero reserved flags"
                )
            }
            if (
                column.chunks.size > SaveContainerContract.MAXIMUM_CHUNKS ||
                totalChunks > SaveContainerContract.MAXIMUM_CHUNKS - column.chunks.size
            ) {
                formatFailure(
                    CellularityErrorCode.LIMIT_EXCEEDED,
                    "Release directory contains too many chunks"
                )
            }
            totalChunks += column.chunks.size
            val fieldCritical =
                column.fieldFlags and SaveContainerContract.DESCRIPTOR_CRITICAL_FLAG != 0
            validateChunkShape(column, sectionCritical && fieldCritical)

            if (header != null) {
                column.chunks.forEach { chunk ->
                    val region = checkedRegion(
                        chunk.fileOffset,
                        chunk.storedBytes,
                        header.declaredFileBytes,
                        "chunk"
                    )
                    if (liveRegions.any { it.overlaps(region) }) {
                        formatFailure(
                            CellularityErrorCode.INVALID_LENGTH,
                            "Release chunk overlaps another live region"
                        )
                    }
                    liveRegions += region
                }
            }
        }
    }
}

private fun validateChunkShape(
    column: ColumnDescriptor,
    effectiveCritical: Boolean
) {
    val primitiveType = PrimitiveType.fromWireId(column.primitiveType)
    val encoding = PhysicalEncoding.fromWireId(column.physicalEncoding)
    if ((primitiveType == null || encoding == null) && effectiveCritical) {
        formatFailure(
            CellularityErrorCode.UNSUPPORTED_FEATURE,
            "Release critical column uses an unsupported representation"
        )
    }
    if (column.logicalValueCount == 0L) {
        if (column.chunks.isNotEmpty()) {
            formatFailure(
                CellularityErrorCode.INVALID_COUNT,
                "Release empty column contains chunks"
            )
        }
        return
    }
    if (column.logicalValueCount < 0 || column.chunks.isEmpty()) {
        formatFailure(
            CellularityErrorCode.INVALID_COUNT,
            "Release nonempty column has no chunks"
        )
    }

    var expectedFirst = 0L
    column.chunks.forEach { chunk ->
        if (
            chunk.firstLogicalValue != expectedFirst ||
            chunk.logicalValueCount <= 0 ||
            chunk.logicalValueCount > Long.MAX_VALUE - expectedFirst
        ) {
            formatFailure(
                CellularityErrorCode.INVALID_COUNT,
                "Release chunk logical ranges are not contiguous"
            )
        }
        expectedFirst += chunk.logicalValueCount
        if (expectedFirst > column.logicalValueCount) {
            formatFailure(
                CellularityErrorCode.INVALID_COUNT,
                "Release chunk logical ranges are not contiguous"
            )
        }
        val codec = CompressionCodec.entries.firstOrNull {
            it.wireId == chunk.compressionCodec
        }
        if (codec == null && effectiveCritical) {
            formatFailure(
                CellularityErrorCode.UNSUPPORTED_FEATURE,
                "Release critical column requires an unsupported codec"
            )
        }
        if (chunk.compressionFlags != 0 || chunk.reserved != 0L) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release chunk has nonzero reserved fields"
            )
        }
        if (chunk.storedBytes <= 0) {
            formatFailure(
                CellularityErrorCode.INVALID_LENGTH,
                "Release nonempty chunk has no stored bytes"
            )
        }
        if (primitiveType != null && encoding != null) {
            val decodedBytes = checkedMultiply(
                primitiveType.byteCount.toLong(),
                chunk.logicalValueCount,
                "chunk decoded size"
            )
            if (
                decodedBytes > SaveContainerContract.MAXIMUM_DECODED_CHUNK_BYTES ||
                chunk.decodedBytes != decodedBytes
            ) {
                formatFailure(
                    CellularityErrorCode.LIMIT_EXCEEDED,
                    "Release chunk decoded size is invalid"
                )
            }
            if (
                encoding == PhysicalEncoding.CONSTANT &&
                codec != null &&
                (codec != CompressionCodec.NONE ||
                    chunk.storedBytes != primitiveType.byteCount.toLong())
            ) {
                formatFailure(
                    CellularityErrorCode.INVALID_COMPRESSION,
                    "Release constant chunk must store one raw value"
                )
            }
            if (
                encoding == PhysicalEncoding.PLAIN &&
                codec == CompressionCodec.NONE &&
                chunk.storedBytes != decodedBytes
            ) {
                formatFailure(
                    CellularityErrorCode.INVALID_COMPRESSION,
                    "Release raw chunk size does not match decoded size"
                )
            }
        }
    }
    if (expectedFirst != column.logicalValueCount) {
        formatFailure(
            CellularityErrorCode.INVALID_COUNT,
            "Release chunks do not cover their column"
        )
    }
}

private fun validateName(name: String, context: String) {
    if (name.isEmpty() || name.length > SaveContainerContract.MAXIMUM_NAME_BYTES) {
        formatFailure(
            CellularityErrorCode.INVALID_LENGTH,
            "Invalid release $context name length"
        )
    }
    name.forEachIndexed { index, character ->
        val valid = character in 'a'..'z' ||
            (index > 0 && character in '0'..'9') ||
            (index > 0 && character == '_')
        if (!valid) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release $context name must be lowercase ASCII snake_case"
            )
        }
    }
}

private data class Region(val offset: Long, val bytes: Long) {
    fun overlaps(other: Region): Boolean =
        offset < other.offset + other.bytes && other.offset < offset + bytes
}

private fun checkedRegion(
    offset: Long,
    bytes: Long,
    fileBytes: Long,
    name: String
): Region {
    if (offset < 0 || bytes < 0 || offset > Long.MAX_VALUE - bytes || offset + bytes > fileBytes) {
        formatFailure(
            CellularityErrorCode.INVALID_LENGTH,
            "Release $name region is out of bounds"
        )
    }
    return Region(offset, bytes)
}

private fun checkedMultiply(left: Long, right: Long, name: String): Long {
    if (left < 0 || right < 0 || left != 0L && right > Long.MAX_VALUE / left) {
        formatFailure(
            CellularityErrorCode.LIMIT_EXCEEDED,
            "Release $name overflows"
        )
    }
    return left * right
}

private class LittleEndianWriter(private val maximumBytes: Int) {
    private var bytes = ByteArray(minOf(maximumBytes, 256))
    private var size = 0

    fun u8(value: Int) {
        require(value in 0..0xff)
        ensure(1)
        bytes[size++] = value.toByte()
    }

    fun u16(value: Int) {
        require(value in 0..0xffff)
        u8(value)
        u8(value ushr 8)
    }

    fun u32(value: Long) {
        if (value !in 0..0xffff_ffffL) {
            formatFailure(
                CellularityErrorCode.INVALID_VALUE,
                "Release U32 value is out of range"
            )
        }
        u8(value.toInt() and 0xff)
        u8((value ushr 8).toInt() and 0xff)
        u8((value ushr 16).toInt() and 0xff)
        u8((value ushr 24).toInt() and 0xff)
    }

    fun u32Bits(value: Int) {
        u32(value.toLong() and 0xffff_ffffL)
    }

    fun u64(value: Long) {
        if (value < 0) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "The Kotlin release codec supports U64 values up to Long.MAX_VALUE"
            )
        }
        i64Bits(value)
    }

    fun i64Bits(value: Long) {
        u32(value and 0xffff_ffffL)
        u32((value ushr 32) and 0xffff_ffffL)
    }

    fun raw(value: ByteArray) {
        ensure(value.size)
        value.copyInto(bytes, size)
        size += value.size
    }

    fun patchU32Bits(offset: Int, value: Int) {
        if (offset < 0 || offset > size - 4) {
            error("Release writer patch is out of bounds")
        }
        bytes[offset] = value.toByte()
        bytes[offset + 1] = (value ushr 8).toByte()
        bytes[offset + 2] = (value ushr 16).toByte()
        bytes[offset + 3] = (value ushr 24).toByte()
    }

    fun toByteArray(): ByteArray = bytes.copyOf(size)

    private fun ensure(additionalBytes: Int) {
        if (additionalBytes < 0 || additionalBytes > maximumBytes - size) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release save output exceeds its structural limit"
            )
        }
        val required = size + additionalBytes
        if (required <= bytes.size) return
        var capacity = maxOf(1, bytes.size)
        while (capacity < required) {
            capacity = minOf(maximumBytes, maxOf(required, capacity * 2))
        }
        bytes = bytes.copyOf(capacity)
    }
}

private class LittleEndianReader(private val bytes: ByteArray) {
    private var position = 0

    val remaining: Int
        get() = bytes.size - position

    fun u8(): Int {
        requireBytes(1)
        return bytes[position++].toInt() and 0xff
    }

    fun u16(): Int = u8() or (u8() shl 8)

    fun u32(): Long =
        u16().toLong() or (u16().toLong() shl 16)

    fun u32AsInt(name: String): Int {
        val value = u32()
        if (value > Int.MAX_VALUE) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release $name exceeds the supported range"
            )
        }
        return value.toInt()
    }

    fun u64(): Long {
        val low = u32()
        val high = u32()
        if (high > 0x7fff_ffffL) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "The Kotlin release codec supports U64 values up to Long.MAX_VALUE"
            )
        }
        return low or (high shl 32)
    }

    fun i64Bits(): Long = u32() or (u32() shl 32)

    fun text(count: Int): String {
        requireBytes(count)
        val result = bytes.copyOfRange(position, position + count).decodeToString()
        position += count
        return result
    }

    fun skip(count: Int) {
        requireBytes(count)
        position += count
    }

    private fun requireBytes(count: Int) {
        if (count < 0 || count > remaining) {
            formatFailure(
                CellularityErrorCode.TRUNCATED_INPUT,
                "Release save structure ended unexpectedly"
            )
        }
    }
}

internal fun formatFailure(code: CellularityErrorCode, message: String): Nothing =
    throw SaveContainerFormatException(code, message)
