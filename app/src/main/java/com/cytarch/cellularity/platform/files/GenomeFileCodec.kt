package com.cytarch.cellularity.platform.files

import com.cytarch.cellularity.CellularityDataException
import com.cytarch.cellularity.NativeCellularityControllerBridge
import com.cytarch.cellularity.shared.files.CellularityFileLimits
import com.cytarch.cellularity.shared.files.release.EmbeddedPreviewFormat
import com.cytarch.cellularity.shared.files.release.ReleaseGenomeFileInspector
import com.cytarch.cellularity.shared.files.release.ReleaseGenomeFileSummary
import com.cytarch.cellularity.shared.files.release.ReleaseGenomeRangeReader
import com.cytarch.cellularity.shared.files.release.SaveContainerFormatException
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GeneFloatValues
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.model.GenomeDraftWireFormatException
import com.cytarch.cellularity.shared.model.LinkConstants
import com.cytarch.cellularity.shared.model.ParametricValueDraft
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.EOFException
import java.io.File
import java.io.FileInputStream
import java.io.IOException
import java.io.InputStream
import java.io.ObjectInputStream
import java.io.OutputStream
import java.io.PushbackInputStream
import java.io.RandomAccessFile
import java.io.StreamCorruptedException
import kotlin.math.PI
import kotlin.math.floor

internal data class EncodedGenomePreview(
    val codec: Int,
    val pixelWidth: Int,
    val pixelHeight: Int,
    val bytes: ByteArray
)

internal data class GenomeFileInspection(
    /** Product version for Java legacy, content-schema major for release CLTY. */
    val familyLocalVersion: Int,
    val loadSupported: Boolean,
    val releaseSummary: ReleaseGenomeFileSummary?
)

internal data class DecodedEmbeddedFilePreview(
    val format: EmbeddedPreviewFormat,
    val pixelWidth: Int,
    val pixelHeight: Int,
    val bytes: ByteArray
)

internal interface ReleaseGenomeCodecBridge {
    fun encode(genomeDraftWire: ByteArray, preview: EncodedGenomePreview?): ByteArray
    fun decode(releaseGenome: ByteArray): ByteArray
}

private object NativeReleaseGenomeCodecBridge : ReleaseGenomeCodecBridge {
    override fun encode(
        genomeDraftWire: ByteArray,
        preview: EncodedGenomePreview?
    ): ByteArray = NativeCellularityControllerBridge.encodeReleaseGenome(
        genomeDraftWire,
        preview?.bytes,
        preview?.codec ?: PREVIEW_CODEC_NONE,
        preview?.pixelWidth ?: 0,
        preview?.pixelHeight ?: 0
    )

    override fun decode(releaseGenome: ByteArray): ByteArray =
        NativeCellularityControllerBridge.decodeReleaseGenome(releaseGenome)
}

/** Release CLTY codec with a frozen read-only original-game import path. */
internal object GenomeFileCodec {
    data class Decoded(
        /** Product version for Java legacy, content-schema major for release CLTY. */
        val familyLocalVersion: Int,
        val genome: GenomeDraft
    )

    fun read(
        source: InputStream,
        releaseCodec: ReleaseGenomeCodecBridge = NativeReleaseGenomeCodecBridge
    ): Decoded = decodeIo {
        val input = sniffable(source)
        when (sniffMagic(input)) {
            RELEASE_MAGIC -> Decoded(
                familyLocalVersion = RELEASE_CONTENT_SCHEMA_MAJOR,
                genome = decodeWire(
                    releaseCodec.decode(readBoundedReleaseBytes(input))
                )
            )
            JAVA_STREAM_MAGIC -> readLegacy(input)
            GenomeContract.WIRE_MAGIC -> throw dataError(
                CellularityErrorCode.UNSUPPORTED_FEATURE,
                "Internal CGDW messages are not persistent genome files"
            )
            DISCARDED_NATIVE_GENOME_MAGIC -> throw dataError(
                CellularityErrorCode.UNSUPPORTED_FEATURE,
                "Pre-release CLGN genome files are not supported"
            )
            else -> throw dataError(
                CellularityErrorCode.INVALID_HEADER,
                "Unknown genome file header"
            )
        }
    }

    fun write(
        destination: OutputStream,
        genome: GenomeDraft,
        preview: EncodedGenomePreview? = null,
        releaseCodec: ReleaseGenomeCodecBridge = NativeReleaseGenomeCodecBridge
    ) {
        val releaseBytes = releaseCodec.encode(GenomeDraftWireCodec.encode(genome), preview)
        if (releaseBytes.size.toLong() > CellularityFileLimits.MAX_GENOME_SIZE_BYTES) {
            throw dataError(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Genome exceeds the release file size limit"
            )
        }
        destination.write(releaseBytes)
        destination.flush()
    }

    /** Reads only CLTY bootstrap/metadata, or only the Java legacy outer version. */
    fun inspect(file: File): GenomeFileInspection = decodeIo {
        requireBoundedFile(file)
        RandomAccessFile(file, "r").use { input ->
            when (readMagic(input)) {
                RELEASE_MAGIC -> {
                    val summary = ReleaseGenomeFileInspector.inspectMetadata(
                        fileByteCount = input.length(),
                        reader = input.rangeReader()
                    )
                    GenomeFileInspection(
                        familyLocalVersion = summary.contentSchemaMajor,
                        loadSupported = summary.bodySupported,
                        releaseSummary = summary
                    )
                }
                JAVA_STREAM_MAGIC -> GenomeFileInspection(
                    familyLocalVersion = FileInputStream(file).use { source ->
                        val stream = sniffable(source)
                        if (sniffMagic(stream) != JAVA_STREAM_MAGIC) {
                            throw dataError(
                                CellularityErrorCode.IO_FAILURE,
                                "Genome file changed while it was being inspected"
                            )
                        }
                        ObjectInputStream(stream).use(::readAndValidateOuterVersion)
                    },
                    loadSupported = true,
                    releaseSummary = null
                )
                GenomeContract.WIRE_MAGIC -> throw dataError(
                    CellularityErrorCode.UNSUPPORTED_FEATURE,
                    "Internal CGDW messages are not persistent genome files"
                )
                DISCARDED_NATIVE_GENOME_MAGIC -> throw dataError(
                    CellularityErrorCode.UNSUPPORTED_FEATURE,
                    "Pre-release CLGN genome files are not supported"
                )
                else -> throw dataError(
                    CellularityErrorCode.INVALID_HEADER,
                    "Unknown genome file header"
                )
            }
        }
    }

    /** Reads and CRC-checks only the image range described by current metadata. */
    fun readPreview(
        file: File,
        summary: ReleaseGenomeFileSummary
    ): DecodedEmbeddedFilePreview? = decodeIo {
        requireBoundedFile(file)
        RandomAccessFile(file, "r").use { input ->
            val bytes = ReleaseGenomeFileInspector.readPreview(
                summary,
                input.rangeReader()
            ) ?: return@decodeIo null
            DecodedEmbeddedFilePreview(
                format = summary.preview.format,
                pixelWidth = summary.preview.pixelWidth,
                pixelHeight = summary.preview.pixelHeight,
                bytes = bytes
            )
        }
    }

    private fun sniffable(source: InputStream): PushbackInputStream = PushbackInputStream(
        if (source is BufferedInputStream) source else BufferedInputStream(source),
        Int.SIZE_BYTES
    )

    private fun sniffMagic(input: PushbackInputStream): Int {
        val header = ByteArray(Int.SIZE_BYTES)
        var count = 0
        while (count < header.size) {
            val read = input.read(header, count, header.size - count)
            if (read < 0) break
            count += read
        }
        if (count > 0) input.unread(header, 0, count)
        if (count != header.size) {
            throw dataError(
                CellularityErrorCode.TRUNCATED_INPUT,
                "Genome file header is truncated"
            )
        }
        return header.fold(0) { value, byte ->
            (value shl 8) or (byte.toInt() and 0xff)
        }
    }

    private fun readBoundedReleaseBytes(input: InputStream): ByteArray {
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(8 * 1024)
        var total = 0
        while (true) {
            val read = input.read(buffer)
            if (read < 0) break
            total += read
            if (total.toLong() > CellularityFileLimits.MAX_GENOME_SIZE_BYTES) {
                throw dataError(
                    CellularityErrorCode.LIMIT_EXCEEDED,
                    "Genome exceeds the release file size limit"
                )
            }
            output.write(buffer, 0, read)
        }
        return output.toByteArray()
    }

    private fun requireBoundedFile(file: File) {
        if (file.length() > CellularityFileLimits.MAX_GENOME_SIZE_BYTES) {
            throw dataError(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Genome exceeds the release file size limit"
            )
        }
    }

    private fun readMagic(input: RandomAccessFile): Int {
        val header = readRange(input, 0, Int.SIZE_BYTES)
        if (header.size != Int.SIZE_BYTES) {
            throw dataError(
                CellularityErrorCode.TRUNCATED_INPUT,
                "Genome file header is truncated"
            )
        }
        return header.fold(0) { value, byte ->
            (value shl 8) or (byte.toInt() and 0xff)
        }
    }

    private fun RandomAccessFile.rangeReader(): ReleaseGenomeRangeReader =
        ReleaseGenomeRangeReader { offset, byteCount -> readRange(this, offset, byteCount) }

    private fun readRange(
        input: RandomAccessFile,
        offset: Long,
        byteCount: Int
    ): ByteArray {
        if (
            offset < 0 || byteCount < 0 || offset > input.length() ||
            byteCount.toLong() > input.length() - offset
        ) {
            return byteArrayOf()
        }
        val bytes = ByteArray(byteCount)
        input.seek(offset)
        input.readFully(bytes)
        return bytes
    }

    private fun decodeWire(bytes: ByteArray): GenomeDraft = try {
        GenomeDraftWireCodec.decode(bytes)
    } catch (failure: GenomeDraftWireFormatException) {
        throw dataError(
            failure.code,
            failure.message ?: "Invalid Cellularity genome",
            failure
        )
    } catch (failure: IllegalArgumentException) {
        throw dataError(
            CellularityErrorCode.INVALID_VALUE,
            failure.message ?: "Invalid Cellularity genome",
            failure
        )
    }

    private fun readLegacy(input: InputStream): Decoded =
        ObjectInputStream(input).use { legacy ->
            val productVersion = readAndValidateOuterVersion(legacy)
            val genes = MutableList(serializedGeneCount(productVersion)) {
                readGene(legacy, productVersion)
            }
            while (genes.size < LegacyGenomeFormat.EFFECTIVE_IMPORTED_GENE_COUNT) {
                genes += genes.first().deepCopy().also { it.initial = false }
            }
            normalizeInitialGene(genes)
            if (legacy.read() != -1) {
                throw dataError(
                    CellularityErrorCode.TRAILING_DATA,
                    "Unexpected trailing genome data"
                )
            }
            Decoded(
                familyLocalVersion = productVersion,
                genome = GenomeDraft(genes.map(MutableLegacyGene::toDraft))
            )
        }

    private fun readAndValidateOuterVersion(input: ObjectInputStream): Int {
        val version = input.readInt()
        if (version < LegacyGenomeFormat.MIN_PRODUCT_VERSION) {
            throw dataError(
                CellularityErrorCode.INVALID_VERSION,
                "Invalid legacy genome format version: $version"
            )
        }
        if (version > LegacyGenomeFormat.MAX_PRODUCT_VERSION) {
            throw dataError(
                CellularityErrorCode.NEWER_PRODUCT_VERSION,
                "Unsupported newer legacy genome format version: $version"
            )
        }
        return version
    }

    private fun serializedGeneCount(productVersion: Int): Int =
        LegacyGenomeFormat.serializedGeneCount(productVersion)

    private fun readGene(input: ObjectInputStream, cellVersion: Int): MutableLegacyGene {
        val version = if (cellVersion >= 3) input.readInt() else 1
        if (version < LegacyGenomeFormat.MIN_PRODUCT_VERSION) {
            throw dataError(
                CellularityErrorCode.INVALID_VERSION,
                "Invalid legacy gene format version: $version"
            )
        }
        if (version > LegacyGenomeFormat.MAX_PRODUCT_VERSION) {
            throw dataError(
                CellularityErrorCode.NEWER_PRODUCT_VERSION,
                "Unsupported newer legacy gene format version: $version"
            )
        }

        val gene = MutableLegacyGene()
        var density = 0.0f
        var swimForce = 0.0f
        var virusFromGeneIndex = 0
        var virusToGeneIndex = 0

        repeat(GenomeContract.COLOR_COUNT) { index -> gene.color[index] = input.readFloat() }

        if (version >= 7) {
            gene.splitMass = input.readFloat()
            gene.splitRatio = input.readFloat()
            gene.splitAngle = input.readFloat()
            gene.child1Angle = input.readFloat()
            gene.child2Angle = input.readFloat()
            if (version < 9) {
                gene.splitAngle = (gene.splitAngle + PI).toFloat()
                gene.child1Angle = (gene.child1Angle + PI).toFloat()
                gene.child2Angle = (gene.child2Angle + PI).toFloat()
                gene.splitRatio = 1.0f - gene.splitRatio
            }
            gene.massFlowPriority = input.readFloat()
            gene.child1GeneIndex = input.readInt()
            gene.child2GeneIndex = input.readInt()
            if (version >= LegacyGenomeFormat.OBSOLETE_EXTENSION_PRODUCT_VERSION) {
                repeat(LegacyGenomeFormat.OBSOLETE_GENE_EXTENSION_FIELD_COUNT) {
                    input.readInt()
                }
            }
            gene.makeLink = input.readBoolean()
            gene.child1KeepLinks = input.readBoolean()
            gene.child2KeepLinks = input.readBoolean()
            gene.cellType = readCellType(input)
            input.readInt() // Removed historical type field.
            gene.stayAlive = input.readBoolean()
            if (version < 9) {
                density = input.readFloat()
                virusFromGeneIndex = input.readInt()
                virusToGeneIndex = input.readInt()
            }
            gene.initial = input.readBoolean()
            gene.child1Mirror = input.readBoolean()
            gene.child2Mirror = input.readBoolean()
            gene.linkStrength = if (version >= 16) {
                input.readFloat()
            } else {
                input.readFloat() * LinkConstants.MAX_STIFFNESS
            }
        } else {
            gene.splitMass = input.readDouble().toFloat()
            gene.splitRatio = input.readDouble().toFloat()
            gene.splitAngle = input.readDouble().toFloat()
            gene.child1Angle = if (version == 5) {
                (input.readDouble() + PI).toFloat()
            } else {
                input.readDouble().toFloat()
            }
            gene.child2Angle = input.readDouble().toFloat()
            if (version < 9) {
                gene.splitAngle = (gene.splitAngle + PI).toFloat()
                gene.child1Angle = (gene.child1Angle + PI).toFloat()
                gene.child2Angle = (gene.child2Angle + PI).toFloat()
            }
            gene.massFlowPriority = input.readDouble().toFloat()
            gene.child1GeneIndex = input.readInt()
            gene.child2GeneIndex = input.readInt()
            gene.makeLink = input.readBoolean()
            gene.child1KeepLinks = input.readBoolean()
            gene.child2KeepLinks = input.readBoolean()
            gene.cellType = readCellType(input)
            input.readInt() // Removed historical type field.
            gene.stayAlive = input.readBoolean()
            density = input.readDouble().toFloat()
            if (version >= 3) {
                virusFromGeneIndex = input.readInt()
                virusToGeneIndex = input.readInt()
            }
            if (version >= 4) {
                gene.initial = input.readBoolean()
                gene.child1Mirror = input.readBoolean()
                gene.child2Mirror = input.readBoolean()
                gene.linkStrength = input.readDouble().toFloat()
            }
        }

        if (version < 8) {
            swimForce = 0.4f
        } else if (version == 8) {
            swimForce = input.readFloat()
        }

        gene.child1GeneIndex = gene.child1GeneIndex.coerceIn(
            0,
            LegacyGenomeFormat.EFFECTIVE_IMPORTED_GENE_COUNT - 1
        )
        gene.child2GeneIndex = gene.child2GeneIndex.coerceIn(
            0,
            LegacyGenomeFormat.EFFECTIVE_IMPORTED_GENE_COUNT - 1
        )

        if (version >= 9) {
            repeat(GenomeContract.PARAMETRIC_VALUE_COUNT) { index ->
                val value = gene.parametricValues[index]
                value.signalChannel = input.readShort()
                if (version >= 17) {
                    value.formulaType = input.readShort()
                } else if (value.signalChannel.toInt() == -1) {
                    value.signalChannel = 0
                    value.formulaType = 0
                } else if (value.signalChannel < CellDataConstants.SIGNAL_SUBSTANCE_COUNT) {
                    value.formulaType = 1
                } else {
                    value.signalChannel =
                        (value.signalChannel - CellDataConstants.SIGNAL_SUBSTANCE_COUNT).toShort()
                    value.formulaType = 2
                }
                value.variableA = input.readFloat()
                value.variableB = input.readFloat()
                value.variableC = input.readFloat()
            }
            repeat(GenomeContract.INTEGER_VALUE_COUNT) { index ->
                gene.integerValues[index] = when {
                    version == 10 && index == GenomeContract.SECRETE_TYPE_INTEGER_INDEX -> 1
                    version < 15 && index == GenomeContract.MAX_CONNECTION_COUNT_INDEX ->
                        CellDataConstants.MAX_NEIGHBORS - 2
                    version < 20 && index == GenomeContract.TELOMERASE_LENGTH_INDEX -> 0
                    else -> input.readInt()
                }
            }
            gene.smellAmplitude = input.readFloat()
            gene.smellRed = input.readFloat()
            gene.smellGreen = input.readFloat()
            gene.smellBlue = input.readFloat()
            gene.smellThreshold = input.readFloat()
            gene.adhesinLength = 0.0f
            gene.cytoskeleton = 1.0f
            if (version >= 16) gene.adhesinLength = input.readFloat()
            if (version >= 14) gene.cytoskeleton = input.readFloat()
        } else {
            gene.parametricValues[GenomeContract.SWIM_FORCE_VALUE_INDEX].variableB =
                normalizedLegacyValue(swimForce, GenomeContract.SWIM_FORCE_VALUE_INDEX)
            // This intentionally preserves the historical v1-v8 density conversion.
            gene.parametricValues[GenomeContract.DENSITY_VALUE_INDEX].variableB =
                normalizedLegacyValue(density, GenomeContract.SWIM_FORCE_VALUE_INDEX)
            gene.integerValues[GenomeContract.VIRUS_FROM_INTEGER_INDEX] = virusFromGeneIndex
            gene.integerValues[GenomeContract.VIRUS_TO_INTEGER_INDEX] = virusToGeneIndex
            gene.cytoskeleton = 1.0f
            gene.integerValues[GenomeContract.MAX_CONNECTION_COUNT_INDEX] =
                GenomeContract.integerUpperBounds[GenomeContract.MAX_CONNECTION_COUNT_INDEX] - 1
        }

        gene.fix()
        return gene
    }

    private fun readCellType(input: ObjectInputStream): CellType {
        val ordinal = input.readInt()
        return try {
            CellType.fromNativeValue(ordinal)
        } catch (failure: IllegalArgumentException) {
            throw dataError(
                CellularityErrorCode.INVALID_VALUE,
                "Invalid legacy gene cell type ordinal: $ordinal",
                failure
            )
        }
    }

    private fun normalizeInitialGene(genes: List<MutableLegacyGene>) {
        var foundInitial = false
        genes.forEach { gene ->
            if (foundInitial) {
                gene.initial = false
            } else if (gene.initial) {
                foundInitial = true
            }
        }
        if (!foundInitial) genes.first().initial = true
    }

    private fun normalizedLegacyValue(value: Float, rangeIndex: Int): Float {
        val minimum = GenomeContract.parametricMinimums[rangeIndex]
        val maximum = GenomeContract.parametricMaximums[rangeIndex]
        return -1.0f + 2.0f * (value - minimum) / (maximum - minimum)
    }

    private inline fun <T> decodeIo(block: () -> T): T = try {
        block()
    } catch (failure: CellularityDataException) {
        throw failure
    } catch (failure: SaveContainerFormatException) {
        throw dataError(
            failure.code,
            failure.message ?: "Invalid release genome",
            failure
        )
    } catch (failure: EOFException) {
        throw dataError(
            CellularityErrorCode.TRUNCATED_INPUT,
            "Legacy genome ended unexpectedly",
            failure
        )
    } catch (failure: StreamCorruptedException) {
        val truncated = failure.message?.contains("EOF", ignoreCase = true) == true
        throw dataError(
            if (truncated) CellularityErrorCode.TRUNCATED_INPUT
            else CellularityErrorCode.INVALID_HEADER,
            if (truncated) "Legacy genome ended unexpectedly"
            else "Invalid legacy genome stream header",
            failure
        )
    } catch (failure: IOException) {
        throw dataError(
            CellularityErrorCode.IO_FAILURE,
            failure.message ?: "Could not read legacy genome",
            failure
        )
    }

    private fun dataError(
        code: CellularityErrorCode,
        message: String,
        cause: Throwable? = null
    ): CellularityDataException = CellularityDataException(code, message).also { error ->
        if (cause != null) error.initCause(cause)
    }

    private const val RELEASE_MAGIC = 0x434c5459 // CLTY
    private const val RELEASE_CONTENT_SCHEMA_MAJOR = 1
    private const val DISCARDED_NATIVE_GENOME_MAGIC = 0x434c474e // CLGN
    private const val JAVA_STREAM_MAGIC = 0xaced0005.toInt()
}

private const val PREVIEW_CODEC_NONE = 0
internal const val PREVIEW_CODEC_PNG = 1

private data class MutableLegacyParametricValue(
    var variableA: Float = 0.0f,
    var variableB: Float = 0.0f,
    var variableC: Float = 0.0f,
    var signalChannel: Short = 0,
    var formulaType: Short = 0
)

private class MutableLegacyGene {
    val color = MutableList(GenomeContract.COLOR_COUNT) { 0.0f }
    var splitMass = 0.0f
    var splitRatio = 0.0f
    var splitAngle = 0.0f
    var child1Angle = 0.0f
    var child2Angle = 0.0f
    var massFlowPriority = 0.0f
    var linkStrength = 0.0f
    var child1GeneIndex = 0
    var child2GeneIndex = 0
    var makeLink = false
    var child1KeepLinks = false
    var child2KeepLinks = false
    var stayAlive = false
    var initial = false
    var child1Mirror = false
    var child2Mirror = false
    var cellType = CellType.PHAGOCYTE
    val parametricValues = MutableList(GenomeContract.PARAMETRIC_VALUE_COUNT) {
        MutableLegacyParametricValue()
    }
    val integerValues = MutableList(GenomeContract.INTEGER_VALUE_COUNT) { 0 }
    var smellAmplitude = 0.0f
    var smellRed = 0.0f
    var smellGreen = 0.0f
    var smellBlue = 0.0f
    var smellThreshold = 0.0f
    var adhesinLength = 0.0f
    var cytoskeleton = 0.0f

    fun deepCopy(): MutableLegacyGene = MutableLegacyGene().also { copy ->
        color.forEachIndexed { index, value -> copy.color[index] = value }
        copy.splitMass = splitMass
        copy.splitRatio = splitRatio
        copy.splitAngle = splitAngle
        copy.child1Angle = child1Angle
        copy.child2Angle = child2Angle
        copy.massFlowPriority = massFlowPriority
        copy.linkStrength = linkStrength
        copy.child1GeneIndex = child1GeneIndex
        copy.child2GeneIndex = child2GeneIndex
        copy.makeLink = makeLink
        copy.child1KeepLinks = child1KeepLinks
        copy.child2KeepLinks = child2KeepLinks
        copy.stayAlive = stayAlive
        copy.initial = initial
        copy.child1Mirror = child1Mirror
        copy.child2Mirror = child2Mirror
        copy.cellType = cellType
        parametricValues.forEachIndexed { index, value ->
            copy.parametricValues[index] = value.copy()
        }
        integerValues.forEachIndexed { index, value -> copy.integerValues[index] = value }
        copy.smellAmplitude = smellAmplitude
        copy.smellRed = smellRed
        copy.smellGreen = smellGreen
        copy.smellBlue = smellBlue
        copy.smellThreshold = smellThreshold
        copy.adhesinLength = adhesinLength
        copy.cytoskeleton = cytoskeleton
    }

    fun fix() {
        splitAngle = normalizedAngle(splitAngle)
        child1Angle = normalizedAngle(child1Angle)
        child2Angle = normalizedAngle(child2Angle)
        smellAmplitude = clampLikeLegacy(smellAmplitude, -60.0f, 60.0f)
        smellRed = clampLikeLegacy(smellRed, 0.0f, 1.0f)
        smellGreen = clampLikeLegacy(smellGreen, 0.0f, 1.0f)
        smellBlue = clampLikeLegacy(smellBlue, 0.0f, 1.0f)
        smellThreshold = clampLikeLegacy(smellThreshold, 0.0f, kotlin.math.sqrt(3.0).toFloat())
        adhesinLength = clampLikeLegacy(adhesinLength, 0.0f, 0.01f)
        cytoskeleton = clampLikeLegacy(cytoskeleton, 0.5f, 2.0f)

        integerValues.indices.forEach { index ->
            integerValues[index] = integerValues[index].coerceIn(
                0,
                GenomeContract.integerUpperBounds[index] - 1
            )
        }
        parametricValues.forEach { value ->
            value.variableA = clampLikeLegacy(
                value.variableA,
                -CellDataConstants.VALUE_VARIABLE_MAX,
                CellDataConstants.VALUE_VARIABLE_MAX
            )
            value.variableB = clampLikeLegacy(
                value.variableB,
                -CellDataConstants.VALUE_VARIABLE_MAX,
                CellDataConstants.VALUE_VARIABLE_MAX
            )
            value.variableC = clampLikeLegacy(
                value.variableC,
                -CellDataConstants.VALUE_VARIABLE_MAX,
                CellDataConstants.VALUE_VARIABLE_MAX
            )
            value.formulaType = value.formulaType.toInt().coerceIn(0, 3).toShort()
            value.signalChannel = value.signalChannel.toInt().coerceIn(
                0,
                CellDataConstants.SIGNAL_SUBSTANCE_COUNT +
                    CellDataConstants.AUX_SIGNAL_SUBSTANCE_COUNT - 1
            ).toShort()
        }
        splitRatio = clampLikeLegacy(
            splitRatio,
            CellDataConstants.MIN_RATIO,
            1.0f - CellDataConstants.MIN_RATIO
        )
        massFlowPriority = clampLikeLegacy(
            massFlowPriority,
            CellDataConstants.MIN_MASS_FLOW_PRIORITY,
            CellDataConstants.MAX_MASS_FLOW_PRIORITY
        )
    }

    fun toDraft(): GeneDraft = GeneDraft(
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
        parametricValues = parametricValues.map { value ->
            ParametricValueDraft(
                variableA = value.variableA,
                variableB = value.variableB,
                variableC = value.variableC,
                signalSubstanceChannelIndex = value.signalChannel,
                formulaType = value.formulaType
            )
        },
        integerValues = integerValues,
        floatValues = GeneFloatValues(
            smellAmplitude = smellAmplitude,
            smellRed = smellRed,
            smellGreen = smellGreen,
            smellBlue = smellBlue,
            smellThreshold = smellThreshold,
            adhesinLength = adhesinLength,
            cytoskeleton = cytoskeleton
        )
    )

    private fun normalizedAngle(value: Float): Float {
        val twoPi = 2.0 * PI
        return (value - twoPi * floor(value / twoPi)).toFloat()
    }

    private fun clampLikeLegacy(value: Float, minimum: Float, maximum: Float): Float {
        var result = value
        if (result > maximum) result = maximum
        if (result < minimum) result = minimum
        return result
    }
}
