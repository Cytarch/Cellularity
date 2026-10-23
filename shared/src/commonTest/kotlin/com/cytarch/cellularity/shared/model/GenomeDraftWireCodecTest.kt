package com.cytarch.cellularity.shared.model

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class GenomeDraftWireCodecTest {
    @Test
    fun maximumWireSizeMatchesNativeRetainedPayloadContract() {
        assertEquals(78_616, GenomeContract.MAX_WIRE_BYTES)
    }

    @Test
    fun currentDraftRoundTripsEveryFieldFamily() {
        val source = richGenome()

        val bytes = GenomeDraftWireCodec.encode(source)
        val decoded = GenomeDraftWireCodec.decode(bytes)
        val gene = decoded.genes[0]

        assertEquals(
            GenomeContract.WIRE_HEADER_BYTES +
                source.genes.size * GenomeContract.WIRE_BYTES_PER_GENE,
            bytes.size
        )
        assertEquals(GenomeContract.WIRE_MAGIC, readInt(bytes, 0))
        assertEquals(GenomeContract.WIRE_SCHEMA_VERSION, readInt(bytes, Int.SIZE_BYTES))
        assertNativeGeneLayout(bytes)
        assertContentEquals(listOf(0.2f, 0.4f, 0.6f), gene.color)
        assertEquals(0.12f, gene.splitMass)
        assertEquals(7.0f, gene.linkStrength)
        assertTrue(gene.makeLink)
        assertFalse(gene.child2KeepLinks)
        assertEquals(CellType.CILIOCYTE, gene.cellType)
        assertEquals(1.25f, gene.parametricValues[0].variableA)
        assertEquals(3.toShort(), gene.parametricValues[0].signalSubstanceChannelIndex)
        assertEquals(2.toShort(), gene.parametricValues[0].formulaType)
        assertEquals(4, gene.integerValues[0])
        assertEquals(0.005f, gene.floatValues.adhesinLength)
        assertEquals(1.25f, gene.floatValues.cytoskeleton)
        assertEquals(1, decoded.genes.count { it.initial })
        assertContentEquals(bytes, GenomeDraftWireCodec.encode(decoded))
    }

    @Test
    fun decodingRejectsHeaderVersionCountsAndPayloadShape() {
        val valid = GenomeDraftWireCodec.encode(richGenome())

        assertInvalid(valid.copyOf().also { writeInt(it, 0, 0) })
        assertInvalid(valid.copyOf().also {
            writeInt(it, Int.SIZE_BYTES, GenomeContract.WIRE_SCHEMA_VERSION + 1)
        })
        assertInvalid(valid.copyOf().also {
            writeInt(it, 2 * Int.SIZE_BYTES, 0)
        })
        assertInvalid(valid.copyOf(valid.size - 1))
        assertInvalid(valid + 0)
        assertInvalid(ByteArray(GenomeContract.MAX_WIRE_BYTES + 1))
    }

    @Test
    fun decodeProducesAnImmutableSnapshot() {
        val bytes = GenomeDraftWireCodec.encode(richGenome())
        val decoded = GenomeDraftWireCodec.decode(bytes)
        bytes.fill(0)

        assertEquals(0.2f, decoded.genes[0].color[0])
        assertFalse(decoded.genes is MutableList<*>)
        assertFalse(decoded.genes[0].parametricValues is MutableList<*>)
    }

    private fun richGenome(): GenomeDraft {
        var defaults = GenomeDraft.editorDefaults { listOf(0.2f, 0.4f, 0.6f) }
        repeat(4) {
            defaults = defaults.duplicateGene(defaults.genes.lastIndex)
        }
        val parameterValues = defaults.genes[0].parametricValues.toMutableList().also {
            it[0] = ParametricValueDraft(1.25f, -2.0f, 3.5f, 3, 2)
        }
        val integerValues = defaults.genes[0].integerValues.toMutableList().also { it[0] = 4 }
        return defaults.withGene(
            0,
            defaults.genes[0].copy(
                splitMass = 0.12f,
                linkStrength = 7.0f,
                makeLink = true,
                child2KeepLinks = false,
                child2Mirror = true,
                cellType = CellType.CILIOCYTE,
                parametricValues = parameterValues,
                integerValues = integerValues,
                floatValues = GeneFloatValues(
                    smellAmplitude = 1.5f,
                    smellRed = 0.25f,
                    smellGreen = 0.5f,
                    smellBlue = 0.75f,
                    smellThreshold = 0.5f,
                    adhesinLength = 0.005f,
                    cytoskeleton = 1.25f
                )
            )
        )
    }

    private fun assertInvalid(bytes: ByteArray) {
        assertFailsWith<GenomeDraftWireFormatException> {
            GenomeDraftWireCodec.decode(bytes)
        }
    }

    /** Fixed offsets are mirrored by native/formats/GenomeDraftCodec.cpp. */
    private fun assertNativeGeneLayout(bytes: ByteArray) {
        val gene = GenomeContract.WIRE_HEADER_BYTES
        assertEquals(0.2f, readFloat(bytes, gene))
        assertEquals(0.4f, readFloat(bytes, gene + 4))
        assertEquals(0.6f, readFloat(bytes, gene + 8))
        assertEquals(0.12f, readFloat(bytes, gene + 12))
        assertEquals(7.0f, readFloat(bytes, gene + 36))
        assertEquals(0, readInt(bytes, gene + 40))
        assertEquals(1, bytes[gene + 48].toInt())
        assertEquals(0, bytes[gene + 50].toInt())
        assertEquals(1, bytes[gene + 52].toInt())
        assertEquals(1, bytes[gene + 54].toInt())
        assertEquals(CellType.CILIOCYTE.nativeValue, readInt(bytes, gene + 55))
        assertEquals(1.25f, readFloat(bytes, gene + 59))
        assertEquals(-2.0f, readFloat(bytes, gene + 63))
        assertEquals(3.5f, readFloat(bytes, gene + 67))
        assertEquals(3, readShort(bytes, gene + 71))
        assertEquals(2, readShort(bytes, gene + 73))
        assertEquals(4, readInt(bytes, gene + 235))
        assertEquals(1.5f, readFloat(bytes, gene + 279))
        assertEquals(0.005f, readFloat(bytes, gene + 299))
        assertEquals(1.25f, readFloat(bytes, gene + 303))
    }

    private fun readInt(bytes: ByteArray, offset: Int): Int =
        ((bytes[offset].toInt() and 0xff) shl 24) or
            ((bytes[offset + 1].toInt() and 0xff) shl 16) or
            ((bytes[offset + 2].toInt() and 0xff) shl 8) or
            (bytes[offset + 3].toInt() and 0xff)

    private fun readShort(bytes: ByteArray, offset: Int): Int =
        ((bytes[offset].toInt() and 0xff) shl 8) or
            (bytes[offset + 1].toInt() and 0xff)

    private fun readFloat(bytes: ByteArray, offset: Int): Float =
        Float.fromBits(readInt(bytes, offset))

    private fun writeInt(bytes: ByteArray, offset: Int, value: Int) {
        bytes[offset] = (value ushr 24).toByte()
        bytes[offset + 1] = (value ushr 16).toByte()
        bytes[offset + 2] = (value ushr 8).toByte()
        bytes[offset + 3] = value.toByte()
    }
}
