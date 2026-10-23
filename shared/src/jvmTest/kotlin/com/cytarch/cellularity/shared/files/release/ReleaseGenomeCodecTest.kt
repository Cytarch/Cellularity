package com.cytarch.cellularity.shared.files.release

import com.cytarch.cellularity.shared.model.CanonicalGeneDefaults
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.GeneFloatValues
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.model.ParametricValueDraft
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ReleaseGenomeCodecTest {
    @Test
    fun cppAndKotlinShareExactRawStandaloneGenomeGolden() {
        val source = goldenGenome()
        val expected = fixtureText("genome-v1-golden.hex").hexToBytes()
        val actual = ReleaseGenomeRawCodec.encode(source)

        assertContentEquals(expected, actual)
        assertContentEquals(
            GenomeDraftWireCodec.encode(source),
            GenomeDraftWireCodec.encode(ReleaseGenomeRawCodec.decode(expected))
        )
    }

    @Test
    fun exactDynamicGenomeCountsRoundTripThroughColumnarReferenceCodec() {
        listOf(1, 2, 40, 150, 256).forEach { count ->
            val source = genome(count, initialIndex = count / 2)
            val encoded = ReleaseGenomeRawCodec.encode(source)
            val decoded = ReleaseGenomeRawCodec.decode(encoded)

            assertContentEquals(
                GenomeDraftWireCodec.encode(source),
                GenomeDraftWireCodec.encode(decoded),
                "Changed $count-gene genome"
            )
            val summary = ReleaseGenomeFileInspector.inspect(encoded)
            assertEquals(count, summary.geneCount)
            assertEquals(count / 2, summary.initialGeneIndex)
        }
    }

    @Test
    fun previewDimensionsAndCodecAreSelfDescribingAndAncillary() {
        val encoded = ReleaseGenomeRawCodec.encode(
            genome(2, 1),
            EncodedPreview(
                codec = PreviewCodec.PNG,
                pixelWidth = 320,
                pixelHeight = 180,
                bytes = PNG_SIGNATURE
            )
        )
        val summary = ReleaseGenomeFileInspector.inspect(encoded)

        assertEquals(EmbeddedPreviewFormat.PNG, summary.preview.format)
        assertEquals(320, summary.preview.pixelWidth)
        assertEquals(180, summary.preview.pixelHeight)
        assertTrue(summary.preview.usable)

        val damaged = encoded.copyOf().also { bytes ->
            bytes[summary.preview.byteOffset.toInt()] =
                (bytes[summary.preview.byteOffset.toInt()].toInt() xor 1).toByte()
        }
        val damagedSummary = ReleaseGenomeFileInspector.inspect(damaged)
        assertFalse(damagedSummary.preview.usable)
        assertContentEquals(
            GenomeDraftWireCodec.encode(genome(2, 1)),
            GenomeDraftWireCodec.encode(ReleaseGenomeRawCodec.decode(damaged))
        )
    }

    @Test
    fun metadataAndPreviewUseIndependentBoundedRangeReads() {
        val encoded = ReleaseGenomeRawCodec.encode(
            genome(2, 1),
            EncodedPreview(
                codec = PreviewCodec.PNG,
                pixelWidth = 320,
                pixelHeight = 180,
                bytes = PNG_SIGNATURE
            )
        )
        val header = SaveContainerCodec.decodeBootstrap(
            encoded.copyOfRange(0, SaveContainerContract.BOOTSTRAP_BYTES),
            encoded.size.toLong()
        )
        val requests = mutableListOf<Pair<Long, Int>>()
        val reader = ReleaseGenomeRangeReader { offset, byteCount ->
            requests += offset to byteCount
            encoded.copyOfRange(offset.toInt(), offset.toInt() + byteCount)
        }

        val summary = ReleaseGenomeFileInspector.inspectMetadata(
            encoded.size.toLong(),
            reader
        )

        assertEquals(
            listOf(
                0L to SaveContainerContract.BOOTSTRAP_BYTES,
                header.metadataOffset to header.metadataBytes.toInt()
            ),
            requests
        )
        assertTrue(summary.preview.usable)
        assertContentEquals(PNG_SIGNATURE, ReleaseGenomeFileInspector.readPreview(summary, reader))
        assertEquals(3, requests.size)
        assertEquals(summary.preview.byteOffset to PNG_SIGNATURE.size, requests.last())
    }

    @Test
    fun equalCanonicalSelfGenesUseOneFileRowAndMaterializePerOwner() {
        val genes = List(40) { index ->
            CanonicalGeneDefaults.materialize(index).copy(initial = index == 20)
        }
        val source = GenomeDraft(genes)
        val encoded = ReleaseGenomeRawCodec.encode(source)
        val summary = ReleaseGenomeFileInspector.inspect(encoded)

        assertEquals(1, summary.uniqueGeneCount)
        assertContentEquals(
            GenomeDraftWireCodec.encode(source),
            GenomeDraftWireCodec.encode(ReleaseGenomeRawCodec.decode(encoded))
        )
    }

    @Test
    fun additiveContentAndSectionMinorRemainReadable() {
        val source = goldenGenome()
        val bytes = ReleaseGenomeRawCodec.encode(source)
        val header = SaveContainerCodec.decodeBootstrap(
            bytes.copyOfRange(0, SaveContainerContract.BOOTSTRAP_BYTES),
            bytes.size.toLong()
        )
        val metadataBytes = bytes.copyOfRange(
            header.metadataOffset.toInt(),
            (header.metadataOffset + header.metadataBytes).toInt()
        )
        val metadata = SaveContainerCodec.decodeMetadata(
            DocumentKind.GENOME,
            metadataBytes,
            header.metadataCrc32
        ).map { entry ->
            if (entry.fieldId == SaveContainerContract.MetadataField.CONTENT_SCHEMA_MINOR) {
                entry.copy(rawBits = 7)
            } else {
                entry
            }
        }
        val patchedMetadata = SaveContainerCodec.encodeMetadata(DocumentKind.GENOME, metadata)
        assertEquals(metadataBytes.size, patchedMetadata.size)

        val directoryBytes = bytes.copyOfRange(
            header.directoryOffset.toInt(),
            (header.directoryOffset + header.directoryBytes).toInt()
        )
        val directory = SaveContainerCodec.decodeDirectory(
            directoryBytes,
            header.directoryCrc32,
            header
        ).map { it.copy(schemaMinor = 7) }
        val patchedDirectory = SaveContainerCodec.encodeDirectory(directory)
        assertEquals(directoryBytes.size, patchedDirectory.size)

        val patched = bytes.copyOf()
        patchedMetadata.copyInto(patched, header.metadataOffset.toInt())
        patchedDirectory.copyInto(patched, header.directoryOffset.toInt())
        SaveContainerCodec.encodeBootstrap(
            header.copy(
                metadataCrc32 = SaveContainerCodec.crc32(patchedMetadata),
                directoryCrc32 = SaveContainerCodec.crc32(patchedDirectory)
            )
        ).copyInto(patched)

        val summary = ReleaseGenomeFileInspector.inspect(patched)
        assertEquals(7, summary.contentSchemaMinor)
        assertContentEquals(
            GenomeDraftWireCodec.encode(source),
            GenomeDraftWireCodec.encode(ReleaseGenomeRawCodec.decode(patched))
        )

        val futureMetadata = SaveContainerCodec.encodeMetadata(
            DocumentKind.GENOME,
            metadata.map { entry ->
                if (entry.fieldId == SaveContainerContract.MetadataField.CONTENT_SCHEMA_MAJOR) {
                    entry.copy(rawBits = 2)
                } else {
                    entry
                }
            }
        )
        val future = patched.copyOf()
        futureMetadata.copyInto(future, header.metadataOffset.toInt())
        SaveContainerCodec.encodeBootstrap(
            header.copy(
                metadataCrc32 = SaveContainerCodec.crc32(futureMetadata),
                directoryCrc32 = SaveContainerCodec.crc32(patchedDirectory)
            )
        ).copyInto(future)
        val futureSummary = ReleaseGenomeFileInspector.inspect(future)
        assertEquals(2, futureSummary.contentSchemaMajor)
        assertFalse(futureSummary.bodySupported)
        val failure = runCatching { ReleaseGenomeRawCodec.decode(future) }.exceptionOrNull()
        assertTrue(failure is SaveContainerFormatException)
        assertEquals(
            CellularityErrorCode.NEWER_SNAPSHOT_VERSION,
            (failure as SaveContainerFormatException).code
        )
    }

    private fun genome(count: Int, initialIndex: Int): GenomeDraft = GenomeDraft(
        List(count) { index ->
            CanonicalGeneDefaults.materialize(index).copy(
                color = listOf(
                    ((index % 11) + 1) / 12.0f,
                    ((index % 7) + 1) / 8.0f,
                    ((index % 5) + 1) / 6.0f
                ),
                initial = index == initialIndex,
                cellType = if (index % 2 == 0) CellType.PHAGOCYTE else CellType.CILIOCYTE
            )
        }
    )

    private fun goldenGenome(): GenomeDraft {
        val source = genome(2, 1)
        val first = source.genes[0].copy(
            splitMass = 0.1f,
            splitRatio = 0.4f,
            splitAngle = 0.25f,
            child1Angle = 0.5f,
            child2Angle = 0.75f,
            massFlowPriority = 2.0f,
            linkStrength = 3.0f,
            child1GeneIndex = 0,
            child2GeneIndex = 1,
            makeLink = true,
            child1KeepLinks = false,
            child2KeepLinks = true,
            stayAlive = true,
            child1Mirror = true,
            child2Mirror = false,
            parametricValues = List(GenomeContract.PARAMETRIC_VALUE_COUNT) { index ->
                ParametricValueDraft(
                    variableA = 0.01f * index,
                    variableB = if (index == 0) 0.0f else -0.01f * index,
                    variableC = 0.02f * index,
                    signalSubstanceChannelIndex = (index % 8).toShort(),
                    formulaType = (index % 4).toShort()
                )
            },
            integerValues = listOf(0, 1, 1, 2, 3, 1, 2, 3, 3, 5, 2),
            floatValues = GeneFloatValues(
                smellAmplitude = 0.5f,
                smellRed = 0.2f,
                smellGreen = 0.3f,
                smellBlue = 0.4f,
                smellThreshold = 0.6f,
                adhesinLength = 0.005f,
                cytoskeleton = 1.5f
            )
        )
        return source.withGene(0, first)
    }

    private fun fixtureText(name: String): String = checkNotNull(
        javaClass.classLoader?.getResource(name)
    ) { "Missing release format fixture $name" }.readText()

    private fun String.hexToBytes(): ByteArray {
        val digits = filterNot(Char::isWhitespace)
        require(digits.length % 2 == 0)
        return ByteArray(digits.length / 2) { index ->
            digits.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }

    private companion object {
        val PNG_SIGNATURE = byteArrayOf(
            0x89.toByte(),
            0x50,
            0x4e,
            0x47,
            0x0d,
            0x0a,
            0x1a,
            0x0a
        )
    }
}
