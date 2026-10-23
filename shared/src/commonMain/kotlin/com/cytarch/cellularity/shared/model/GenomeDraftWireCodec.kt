package com.cytarch.cellularity.shared.model

/**
 * Versioned, platform-neutral bridge format for sending an immutable [GenomeDraft]
 * to the native runtime. Numeric values are stored in network byte order.
 *
 * Semantic validation deliberately remains native-owned so every runtime entry
 * point applies the same limits before a draft becomes a runtime gene.
 */
object GenomeDraftWireCodec {
    fun encode(genome: GenomeDraft): ByteArray {
        val writer = GenomeDraftWireWriter(
            GenomeContract.WIRE_HEADER_BYTES +
                genome.genes.size * GenomeContract.WIRE_BYTES_PER_GENE
        )
        writer.writeInt(GenomeContract.WIRE_MAGIC)
        writer.writeInt(GenomeContract.WIRE_SCHEMA_VERSION)
        writer.writeInt(genome.genes.size)
        writer.writeInt(GenomeContract.PARAMETRIC_VALUE_COUNT)
        writer.writeInt(GenomeContract.INTEGER_VALUE_COUNT)
        writer.writeInt(GenomeContract.FLOAT_VALUE_COUNT)

        genome.genes.forEach(writer::writeGene)
        return writer.toByteArray()
    }

    fun decode(bytes: ByteArray): GenomeDraft {
        if (bytes.size > GenomeContract.MAX_WIRE_BYTES) {
            throw GenomeDraftWireFormatException(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "Genome draft exceeds the wire size limit"
            )
        }

        val reader = GenomeDraftWireReader(bytes)
        if (reader.readInt() != GenomeContract.WIRE_MAGIC) {
            throw GenomeDraftWireFormatException(
                CellularityErrorCode.INVALID_HEADER,
                "Invalid genome draft header"
            )
        }
        val schemaVersion = reader.readInt()
        if (schemaVersion != GenomeContract.WIRE_SCHEMA_VERSION) {
            throw GenomeDraftWireFormatException(
                CellularityErrorCode.INVALID_VERSION,
                "Unsupported genome draft schema version: $schemaVersion"
            )
        }
        val geneCount = reader.readBoundedCount(
            "gene",
            GenomeContract.MIN_GENE_COUNT,
            GenomeContract.MAX_GENE_COUNT
        )
        reader.requireCount("parametric value", GenomeContract.PARAMETRIC_VALUE_COUNT)
        reader.requireCount("integer value", GenomeContract.INTEGER_VALUE_COUNT)
        reader.requireCount("float value", GenomeContract.FLOAT_VALUE_COUNT)

        val genome = GenomeDraft(List(geneCount) { reader.readGene() })
        if (reader.remaining != 0) {
            throw GenomeDraftWireFormatException(
                CellularityErrorCode.TRAILING_DATA,
                "Genome draft contains trailing data"
            )
        }
        return genome
    }
}

class GenomeDraftWireFormatException(
    val code: CellularityErrorCode,
    message: String
) : IllegalArgumentException(message)

private class GenomeDraftWireWriter(expectedSize: Int) {
    private val bytes = ByteArray(expectedSize)
    private var position = 0

    fun writeGene(gene: GeneDraft) {
        gene.color.forEach(::writeFloat)
        writeFloat(gene.splitMass)
        writeFloat(gene.splitRatio)
        writeFloat(gene.splitAngle)
        writeFloat(gene.child1Angle)
        writeFloat(gene.child2Angle)
        writeFloat(gene.massFlowPriority)
        writeFloat(gene.linkStrength)

        writeInt(gene.child1GeneIndex)
        writeInt(gene.child2GeneIndex)

        writeBoolean(gene.makeLink)
        writeBoolean(gene.child1KeepLinks)
        writeBoolean(gene.child2KeepLinks)
        writeBoolean(gene.stayAlive)
        writeBoolean(gene.initial)
        writeBoolean(gene.child1Mirror)
        writeBoolean(gene.child2Mirror)
        writeInt(gene.cellType.nativeValue)

        gene.parametricValues.forEach { value ->
            writeFloat(value.variableA)
            writeFloat(value.variableB)
            writeFloat(value.variableC)
            writeShort(value.signalSubstanceChannelIndex)
            writeShort(value.formulaType)
        }
        gene.integerValues.forEach(::writeInt)
        writeFloat(gene.floatValues.smellAmplitude)
        writeFloat(gene.floatValues.smellRed)
        writeFloat(gene.floatValues.smellGreen)
        writeFloat(gene.floatValues.smellBlue)
        writeFloat(gene.floatValues.smellThreshold)
        writeFloat(gene.floatValues.adhesinLength)
        writeFloat(gene.floatValues.cytoskeleton)
    }

    fun writeInt(value: Int) {
        writeByte(value ushr 24)
        writeByte(value ushr 16)
        writeByte(value ushr 8)
        writeByte(value)
    }

    private fun writeShort(value: Short) {
        val bits = value.toInt() and 0xffff
        writeByte(bits ushr 8)
        writeByte(bits)
    }

    private fun writeFloat(value: Float) = writeInt(value.toRawBits())

    private fun writeBoolean(value: Boolean) = writeByte(if (value) 1 else 0)

    private fun writeByte(value: Int) {
        check(position < bytes.size) { "Genome draft writer exceeded its fixed payload size" }
        bytes[position++] = value.toByte()
    }

    fun toByteArray(): ByteArray {
        check(position == bytes.size) {
            "Genome draft payload has $position bytes, expected ${bytes.size}"
        }
        return bytes
    }
}

private class GenomeDraftWireReader(private val bytes: ByteArray) {
    private var position = 0

    val remaining: Int
        get() = bytes.size - position

    fun requireCount(name: String, expected: Int) {
        val actual = readInt()
        if (actual != expected) {
            throw GenomeDraftWireFormatException(
                CellularityErrorCode.INVALID_COUNT,
                "Genome draft $name count is $actual, expected $expected"
            )
        }
    }

    fun readBoundedCount(name: String, minimum: Int, maximum: Int): Int {
        val actual = readInt()
        if (actual !in minimum..maximum) {
            throw GenomeDraftWireFormatException(
                CellularityErrorCode.INVALID_COUNT,
                "Genome draft $name count is $actual, expected $minimum..$maximum"
            )
        }
        return actual
    }

    fun readGene(): GeneDraft = GeneDraft(
        color = List(GenomeContract.COLOR_COUNT) { readFloat() },
        splitMass = readFloat(),
        splitRatio = readFloat(),
        splitAngle = readFloat(),
        child1Angle = readFloat(),
        child2Angle = readFloat(),
        massFlowPriority = readFloat(),
        linkStrength = readFloat(),
        child1GeneIndex = readInt(),
        child2GeneIndex = readInt(),
        makeLink = readBoolean(),
        child1KeepLinks = readBoolean(),
        child2KeepLinks = readBoolean(),
        stayAlive = readBoolean(),
        initial = readBoolean(),
        child1Mirror = readBoolean(),
        child2Mirror = readBoolean(),
        cellType = readEnum("cell type") { CellType.fromNativeValue(readInt()) },
        parametricValues = List(GenomeContract.PARAMETRIC_VALUE_COUNT) {
            ParametricValueDraft(
                variableA = readFloat(),
                variableB = readFloat(),
                variableC = readFloat(),
                signalSubstanceChannelIndex = readShort(),
                formulaType = readShort()
            )
        },
        integerValues = List(GenomeContract.INTEGER_VALUE_COUNT) { readInt() },
        floatValues = GeneFloatValues(
            smellAmplitude = readFloat(),
            smellRed = readFloat(),
            smellGreen = readFloat(),
            smellBlue = readFloat(),
            smellThreshold = readFloat(),
            adhesinLength = readFloat(),
            cytoskeleton = readFloat()
        )
    )

    fun readInt(): Int =
        (readUnsignedByte() shl 24) or
            (readUnsignedByte() shl 16) or
            (readUnsignedByte() shl 8) or
            readUnsignedByte()

    private fun readShort(): Short =
        ((readUnsignedByte() shl 8) or readUnsignedByte()).toShort()

    private fun readFloat(): Float = Float.fromBits(readInt())

    private fun readBoolean(): Boolean = readUnsignedByte() != 0

    private fun readUnsignedByte(): Int {
        if (position >= bytes.size) {
            throw GenomeDraftWireFormatException(
                CellularityErrorCode.TRUNCATED_INPUT,
                "Genome draft ended unexpectedly"
            )
        }
        return bytes[position++].toInt() and 0xff
    }

    private inline fun <T> readEnum(name: String, block: () -> T): T = try {
        block()
    } catch (error: IllegalArgumentException) {
        throw GenomeDraftWireFormatException(
            CellularityErrorCode.INVALID_VALUE,
            "Invalid genome draft $name: ${error.message}"
        )
    }
}
