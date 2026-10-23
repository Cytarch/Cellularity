package com.cytarch.cellularity.platform.files

import com.cytarch.cellularity.CellularityDataException
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.ObjectOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GenomeFileCodecTest {
    @Test
    fun everySupportedLegacyFixtureImportsToTheCanonicalDraft() {
        val fixtures = listOf(
            "legacy-genome-v002.bin" to 2,
            "legacy-genome-v003.bin" to 3,
            "legacy-genome-v004.bin" to 4,
            "legacy-genome-v017.bin" to 17,
            "legacy-genome-v018.bin" to 18,
            "legacy-genome-v103.bin" to 103
        )

        fixtures.forEach { (name, expectedVersion) ->
            val decoded = resource(name).use(GenomeFileCodec::read)

            assertEquals(expectedVersion, decoded.familyLocalVersion)
            assertEquals(CellDataConstants.LEGACY_GENOME_GENE_COUNT, decoded.genome.genes.size)
            assertEquals(1, decoded.genome.genes.count { it.initial })
            assertEquals(0.2f, decoded.genome.genes[0].color[0], 1e-6f)
            assertEquals(0.6f, decoded.genome.genes[0].color[2], 1e-6f)
        }
    }

    @Test
    fun releaseWriterKeepsTheDynamicWireInternal() {
        val genome = richGenome()
        val expected = GenomeDraftWireCodec.encode(genome)
        val bridge = TestReleaseGenomeCodec()
        val actual = ByteArrayOutputStream().also { output ->
            GenomeFileCodec.write(output, genome, releaseCodec = bridge)
        }.toByteArray()

        assertArrayEquals(expected, bridge.lastEncodedWire)
        assertArrayEquals(TestReleaseGenomeCodec.wrap(expected), actual)
        assertEquals("CLTY", actual.copyOfRange(0, 4).decodeToString())
        assertNotEquals(GenomeContract.WIRE_MAGIC, readBigEndianInt(actual))
    }

    @Test
    fun currentWriterRoundTripsCanonicalDraftValues() {
        val genome = richGenome()
        val bridge = TestReleaseGenomeCodec()
        val bytes = ByteArrayOutputStream().also {
            GenomeFileCodec.write(it, genome, releaseCodec = bridge)
        }.toByteArray()

        val decoded = GenomeFileCodec.read(ByteArrayInputStream(bytes), bridge)
        val first = decoded.genome.genes.first()
        assertEquals(1, decoded.familyLocalVersion)
        assertEquals(genome.genes.size, decoded.genome.genes.size)
        assertEquals(CellType.CILIOCYTE, first.cellType)
        assertEquals(7.0f, first.linkStrength)
        assertEquals(1, decoded.genome.genes.count { it.initial })
        assertFalse(first.color is MutableList<*>)
        assertFalse(first.integerValues is MutableList<*>)
    }

    @Test
    fun malformedOuterStreamReturnsStableTypedCodes() {
        assertVersionFailure(0, CellularityErrorCode.INVALID_VERSION)
        assertVersionFailure(
            LegacyGenomeFormat.MAX_PRODUCT_VERSION + 1,
            CellularityErrorCode.NEWER_PRODUCT_VERSION
        )

        val current = GenomeDraftWireCodec.encode(richGenome())
        assertFailure(
            TestReleaseGenomeCodec.wrap(current.copyOf(current.size - 1)),
            CellularityErrorCode.TRUNCATED_INPUT,
            TestReleaseGenomeCodec()
        )
        assertFailure(
            TestReleaseGenomeCodec.wrap(current + 0),
            CellularityErrorCode.TRAILING_DATA,
            TestReleaseGenomeCodec()
        )
        assertFailure(current, CellularityErrorCode.UNSUPPORTED_FEATURE)
        assertFailure(byteArrayOf('C'.code.toByte(), 'L'.code.toByte(), 'G'.code.toByte(), 'N'.code.toByte()), CellularityErrorCode.UNSUPPORTED_FEATURE)
        assertFailure(byteArrayOf('N'.code.toByte(), 'O'.code.toByte(), 'P'.code.toByte(), 'E'.code.toByte()), CellularityErrorCode.INVALID_HEADER)
        assertFailure(byteArrayOf('C'.code.toByte(), 'L'.code.toByte()), CellularityErrorCode.TRUNCATED_INPUT)
    }

    @Test
    fun fileInspectorPublishesReleaseCardMetadataWithoutJni() {
        withTemporaryGenome(hexResource("genome-v1-golden.hex")) { file ->
            val inspection = GenomeFileCodec.inspect(file)

            assertEquals(1, inspection.familyLocalVersion)
            assertTrue(inspection.loadSupported)
            val summary = checkNotNull(inspection.releaseSummary)
            assertEquals(2, summary.geneCount)
            assertEquals(1, summary.initialGeneIndex)
            assertEquals(2, summary.uniqueGeneCount)
            assertFalse(summary.preview.usable)
        }
    }

    @Test
    fun fileInspectorExplicitlyRejectsInternalDraftWireFiles() {
        withTemporaryGenome(GenomeDraftWireCodec.encode(richGenome())) { file ->
            val failure = runCatching { GenomeFileCodec.inspect(file) }.exceptionOrNull()

            assertTrue(failure is CellularityDataException)
            assertEquals(
                CellularityErrorCode.UNSUPPORTED_FEATURE,
                (failure as CellularityDataException).code
            )
        }
    }

    private fun assertVersionFailure(version: Int, expectedCode: CellularityErrorCode) {
        val bytes = ByteArrayOutputStream().also { output ->
            ObjectOutputStream(output).use { it.writeInt(version) }
        }.toByteArray()
        assertFailure(bytes, expectedCode)
    }

    private fun assertFailure(
        bytes: ByteArray,
        expectedCode: CellularityErrorCode,
        releaseCodec: ReleaseGenomeCodecBridge = TestReleaseGenomeCodec()
    ) {
        val failure = runCatching {
            GenomeFileCodec.read(ByteArrayInputStream(bytes), releaseCodec)
        }.exceptionOrNull()
        assertTrue(failure is CellularityDataException)
        assertEquals(expectedCode, (failure as CellularityDataException).code)
    }

    private fun richGenome(): GenomeDraft {
        val defaults = GenomeDraft.editorDefaults { listOf(0.2f, 0.4f, 0.6f) }
            .duplicateGene(0)
        return defaults.withGene(
            0,
            defaults.genes[0].copy(
                splitMass = 0.12f,
                linkStrength = 7.0f,
                cellType = CellType.CILIOCYTE
            )
        )
    }

    private fun resource(name: String) = checkNotNull(javaClass.classLoader?.getResourceAsStream(name)) {
        "Missing test fixture $name"
    }

    private fun <T> withTemporaryGenome(bytes: ByteArray, block: (File) -> T): T {
        val file = File.createTempFile("cellularity-genome-codec-", ".genome")
        return try {
            file.writeBytes(bytes)
            block(file)
        } finally {
            file.delete()
        }
    }

    private fun hexResource(name: String): ByteArray {
        val text = resource(name).bufferedReader().use { it.readText() }
        val hex = text.filterNot(Char::isWhitespace)
        require(hex.length % 2 == 0)
        return ByteArray(hex.length / 2) { index ->
            hex.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }

    private fun readBigEndianInt(bytes: ByteArray): Int =
        bytes.take(4).fold(0) { value, byte -> (value shl 8) or (byte.toInt() and 0xff) }
}
