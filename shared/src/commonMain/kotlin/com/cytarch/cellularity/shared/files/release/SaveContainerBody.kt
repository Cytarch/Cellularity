package com.cytarch.cellularity.shared.files.release

import com.cytarch.cellularity.shared.model.CellularityErrorCode

internal data class LogicalColumn(
    val fieldId: Int,
    val primitiveType: PrimitiveType,
    val canonicalName: String,
    val logicalValueCount: Long,
    val decodedBytes: ByteArray
)

internal data class LogicalSection(
    val sectionId: Int,
    val schemaMajor: Int = 1,
    val schemaMinor: Int = 0,
    val canonicalName: String,
    val logicalRowCount: Long,
    val columns: List<LogicalColumn>
)

internal data class LogicalSnapshot(
    val documentKind: DocumentKind,
    val metadata: List<MetadataEntry>,
    val sections: List<LogicalSection>
)

internal data class EncodedPreview(
    val codec: PreviewCodec = PreviewCodec.NONE,
    val pixelWidth: Int = 0,
    val pixelHeight: Int = 0,
    val bytes: ByteArray = byteArrayOf()
)

internal data class DecodedContainer(
    val header: BootstrapHeader,
    val previewDisposition: PreviewDisposition,
    val metadata: List<MetadataEntry>,
    val sections: List<LogicalSection>
)

/**
 * Common Kotlin implementation of the canonical raw physical representation.
 *
 * Production Android compression is native-owned. Keeping this implementation
 * raw makes the cross-language golden deterministic while still independently
 * validating bootstrap, metadata, directory, CONSTANT expansion, ranges, and
 * every decoded CRC.
 */
internal object SaveContainerBodyCodec {
    fun encodeRaw(
        snapshot: LogicalSnapshot,
        preview: EncodedPreview = EncodedPreview(),
        constantEncoding: Boolean = true
    ): ByteArray {
        validatePreviewForWrite(preview)
        val metadata = SaveContainerCodec.encodeMetadata(snapshot.documentKind, snapshot.metadata)
        val file = ByteAccumulator()
        file.append(ByteArray(SaveContainerContract.BOOTSTRAP_BYTES))
        val metadataOffset = file.size.toLong()
        file.append(metadata)
        file.padToEight()

        val previewOffset = if (preview.codec == PreviewCodec.NONE) {
            0L
        } else {
            file.size.toLong().also { file.append(preview.bytes) }
        }
        if (preview.codec != PreviewCodec.NONE) file.padToEight()

        var totalChunks = 0
        val descriptors = snapshot.sections
            .sortedBy(LogicalSection::sectionId)
            .map { section ->
                val columns = section.columns.sortedBy(LogicalColumn::fieldId).map { column ->
                    val width = column.primitiveType.byteCount
                    val expectedBytes = checkedMultiply(
                        column.logicalValueCount,
                        width.toLong(),
                        "column decoded byte count"
                    )
                    if (expectedBytes != column.decodedBytes.size.toLong()) {
                        formatFailure(
                            CellularityErrorCode.INVALID_LENGTH,
                            "Release logical column byte count is inconsistent"
                        )
                    }
                    val constant = constantEncoding &&
                        column.logicalValueCount > 0 &&
                        column.decodedBytes.isConstantValues(width)
                    val chunks = if (column.logicalValueCount == 0L) {
                        emptyList()
                    } else {
                        if (++totalChunks > SaveContainerContract.MAXIMUM_CHUNKS) {
                            formatFailure(
                                CellularityErrorCode.LIMIT_EXCEEDED,
                                "Release save contains too many chunks"
                            )
                        }
                        file.padToEight()
                        val stored = if (constant) {
                            column.decodedBytes.copyOfRange(0, width)
                        } else {
                            column.decodedBytes
                        }
                        val offset = file.size.toLong()
                        file.append(stored)
                        listOf(
                            ChunkDescriptor(
                                firstLogicalValue = 0,
                                logicalValueCount = column.logicalValueCount,
                                compressionCodec = CompressionCodec.NONE.wireId,
                                fileOffset = offset,
                                storedBytes = stored.size.toLong(),
                                decodedBytes = column.decodedBytes.size.toLong(),
                                decodedCrc32 = SaveContainerCodec.crc32(column.decodedBytes)
                            )
                        )
                    }
                    ColumnDescriptor(
                        fieldId = column.fieldId,
                        primitiveType = column.primitiveType.wireId,
                        physicalEncoding = if (constant) {
                            PhysicalEncoding.CONSTANT.wireId
                        } else {
                            PhysicalEncoding.PLAIN.wireId
                        },
                        logicalValueCount = column.logicalValueCount,
                        canonicalName = column.canonicalName,
                        chunks = chunks
                    )
                }
                SectionDescriptor(
                    sectionId = section.sectionId,
                    schemaMajor = section.schemaMajor,
                    schemaMinor = section.schemaMinor,
                    logicalRowCount = section.logicalRowCount,
                    canonicalName = section.canonicalName,
                    columns = columns
                )
            }

        val directory = SaveContainerCodec.encodeDirectory(descriptors)
        file.padToEight()
        val directoryOffset = file.size.toLong()
        file.append(directory)
        if (file.size.toLong() > SaveContainerContract.MAXIMUM_FILE_BYTES) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release save exceeds the complete file limit"
            )
        }
        val header = BootstrapHeader(
            documentKind = snapshot.documentKind,
            declaredFileBytes = file.size.toLong(),
            metadataOffset = metadataOffset,
            metadataBytes = metadata.size.toLong(),
            metadataCrc32 = SaveContainerCodec.crc32(metadata),
            previewCodec = preview.codec.wireId,
            previewPixelWidth = preview.pixelWidth.toLong(),
            previewPixelHeight = preview.pixelHeight.toLong(),
            previewOffset = previewOffset,
            previewBytes = preview.bytes.size.toLong(),
            previewCrc32 = if (preview.codec == PreviewCodec.NONE) {
                0
            } else {
                SaveContainerCodec.crc32(preview.bytes)
            },
            directoryOffset = directoryOffset,
            directoryBytes = directory.size.toLong(),
            directoryCrc32 = SaveContainerCodec.crc32(directory)
        )
        file.patch(0, SaveContainerCodec.encodeBootstrap(header))
        return file.toByteArray()
    }

    fun decodeRaw(bytes: ByteArray): DecodedContainer {
        if (bytes.size < SaveContainerContract.BOOTSTRAP_BYTES) {
            formatFailure(
                CellularityErrorCode.TRUNCATED_INPUT,
                "Release save bootstrap is truncated"
            )
        }
        val header = SaveContainerCodec.decodeBootstrap(
            bytes.copyOfRange(0, SaveContainerContract.BOOTSTRAP_BYTES),
            bytes.size.toLong()
        )
        val metadataBytes = bytes.sliceChecked(
            header.metadataOffset,
            header.metadataBytes,
            "metadata"
        )
        val metadata = SaveContainerCodec.decodeMetadata(
            header.documentKind,
            metadataBytes,
            header.metadataCrc32
        )
        val previewBytes = if (header.previewBytes == 0L) {
            byteArrayOf()
        } else {
            bytes.sliceChecked(header.previewOffset, header.previewBytes, "preview")
        }
        val previewDisposition = SaveContainerCodec.classifyPreview(header, previewBytes)
        val directoryBytes = bytes.sliceChecked(
            header.directoryOffset,
            header.directoryBytes,
            "directory"
        )
        val descriptors = SaveContainerCodec.decodeDirectory(
            directoryBytes,
            header.directoryCrc32,
            header
        )
        var aggregateDecodedBytes = 0L
        val sections = descriptors.mapNotNull { section ->
            if (section.sectionFlags and SaveContainerContract.DESCRIPTOR_CRITICAL_FLAG.toLong() == 0L) {
                return@mapNotNull null
            }
            val columns = section.columns.mapNotNull { column ->
                if (column.fieldFlags and SaveContainerContract.DESCRIPTOR_CRITICAL_FLAG == 0) {
                    return@mapNotNull null
                }
                val primitive = PrimitiveType.fromWireId(column.primitiveType)
                    ?: formatFailure(
                        CellularityErrorCode.UNSUPPORTED_FEATURE,
                        "Release column uses an unknown primitive type"
                    )
                val encoding = PhysicalEncoding.fromWireId(column.physicalEncoding)
                    ?: formatFailure(
                        CellularityErrorCode.UNSUPPORTED_FEATURE,
                        "Release column uses an unknown physical encoding"
                    )
                val decoded = ByteAccumulator()
                column.chunks.forEach { chunk ->
                    if (chunk.compressionCodec != CompressionCodec.NONE.wireId) {
                        formatFailure(
                            CellularityErrorCode.UNSUPPORTED_FEATURE,
                            "Kotlin raw decoder does not decode compressed body chunks"
                        )
                    }
                    val stored = bytes.sliceChecked(
                        chunk.fileOffset,
                        chunk.storedBytes,
                        "body chunk"
                    )
                    val expanded = when (encoding) {
                        PhysicalEncoding.PLAIN -> stored
                        PhysicalEncoding.CONSTANT -> expandConstant(
                            stored,
                            primitive.byteCount,
                            chunk.decodedBytes
                        )
                    }
                    if (SaveContainerCodec.crc32(expanded) != chunk.decodedCrc32) {
                        formatFailure(
                            CellularityErrorCode.INVALID_VALUE,
                            "Release body chunk CRC is invalid"
                        )
                    }
                    aggregateDecodedBytes = checkedAdd(
                        aggregateDecodedBytes,
                        expanded.size.toLong(),
                        "aggregate decoded body bytes"
                    )
                    if (aggregateDecodedBytes > SaveContainerContract.MAXIMUM_FILE_BYTES) {
                        formatFailure(
                            CellularityErrorCode.LIMIT_EXCEEDED,
                            "Release decoded body exceeds the allocation limit"
                        )
                    }
                    decoded.append(expanded)
                }
                LogicalColumn(
                    fieldId = column.fieldId,
                    primitiveType = primitive,
                    canonicalName = column.canonicalName,
                    logicalValueCount = column.logicalValueCount,
                    decodedBytes = decoded.toByteArray()
                )
            }
            LogicalSection(
                sectionId = section.sectionId,
                schemaMajor = section.schemaMajor,
                schemaMinor = section.schemaMinor,
                canonicalName = section.canonicalName,
                logicalRowCount = section.logicalRowCount,
                columns = columns
            )
        }
        return DecodedContainer(header, previewDisposition, metadata, sections)
    }

    private fun validatePreviewForWrite(preview: EncodedPreview) {
        if (preview.codec == PreviewCodec.NONE) {
            if (preview.pixelWidth != 0 || preview.pixelHeight != 0 || preview.bytes.isNotEmpty()) {
                formatFailure(
                    CellularityErrorCode.INVALID_VALUE,
                    "NONE preview must not contain dimensions or bytes"
                )
            }
            return
        }
        if (
            preview.pixelWidth !in 1..SaveContainerContract.MAXIMUM_PREVIEW_DIMENSION ||
            preview.pixelHeight !in 1..SaveContainerContract.MAXIMUM_PREVIEW_DIMENSION ||
            preview.bytes.isEmpty() ||
            preview.bytes.size.toLong() > SaveContainerContract.MAXIMUM_PREVIEW_BYTES ||
            preview.pixelWidth.toLong() * preview.pixelHeight >
            SaveContainerContract.MAXIMUM_PREVIEW_PIXELS
        ) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Encoded preview exceeds the release limits"
            )
        }
    }
}

private fun ByteArray.isConstantValues(width: Int): Boolean {
    if (isEmpty() || size % width != 0) return false
    var offset = width
    while (offset < size) {
        for (index in 0 until width) {
            if (this[index] != this[offset + index]) return false
        }
        offset += width
    }
    return true
}

private fun expandConstant(stored: ByteArray, width: Int, decodedBytes: Long): ByteArray {
    if (
        stored.size != width ||
        decodedBytes < 0 ||
        decodedBytes > Int.MAX_VALUE ||
        decodedBytes % width != 0L
    ) {
        formatFailure(
            CellularityErrorCode.INVALID_COMPRESSION,
            "Release constant chunk shape is invalid"
        )
    }
    return ByteArray(decodedBytes.toInt()) { index -> stored[index % width] }
}

private fun ByteArray.sliceChecked(offset: Long, count: Long, label: String): ByteArray {
    if (
        offset < 0 || count < 0 ||
        offset > size.toLong() || count > size.toLong() - offset ||
        offset > Int.MAX_VALUE || offset + count > Int.MAX_VALUE
    ) {
        formatFailure(
            CellularityErrorCode.TRUNCATED_INPUT,
            "Release $label range is truncated"
        )
    }
    return copyOfRange(offset.toInt(), (offset + count).toInt())
}

private fun checkedMultiply(left: Long, right: Long, label: String): Long {
    if (left < 0 || right < 0 || left != 0L && right > Long.MAX_VALUE / left) {
        formatFailure(CellularityErrorCode.LIMIT_EXCEEDED, "Release $label overflows")
    }
    return left * right
}

private fun checkedAdd(left: Long, right: Long, label: String): Long {
    if (left < 0 || right < 0 || right > Long.MAX_VALUE - left) {
        formatFailure(CellularityErrorCode.LIMIT_EXCEEDED, "Release $label overflows")
    }
    return left + right
}

private class ByteAccumulator(initialCapacity: Int = 256) {
    private var bytes = ByteArray(initialCapacity)
    var size: Int = 0
        private set

    fun append(value: ByteArray) {
        ensure(value.size)
        value.copyInto(bytes, size)
        size += value.size
    }

    fun padToEight() {
        val aligned = (size + 7) and 7.inv()
        ensure(aligned - size)
        while (size < aligned) bytes[size++] = 0
    }

    fun patch(offset: Int, value: ByteArray) {
        if (offset < 0 || offset > size - value.size) {
            error("Release byte patch is out of bounds")
        }
        value.copyInto(bytes, offset)
    }

    fun toByteArray(): ByteArray = bytes.copyOf(size)

    private fun ensure(additional: Int) {
        if (additional < 0 || additional > Int.MAX_VALUE - size) {
            formatFailure(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Release output exceeds addressable memory"
            )
        }
        val required = size + additional
        if (required <= bytes.size) return
        var next = maxOf(1, bytes.size)
        while (next < required) {
            next = if (next > Int.MAX_VALUE / 2) required else maxOf(required, next * 2)
        }
        bytes = bytes.copyOf(next)
    }
}
