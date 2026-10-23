package com.cytarch.cellularity.platform.files

import com.cytarch.cellularity.shared.files.CellularityFileLimits
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GenomeSizeContractTest {
    @Test
    fun currentGenomeEncodingFitsTheCalculatedBound() {
        assertEquals(1, GenomeContract.MIN_GENE_COUNT)
        assertEquals(256, GenomeContract.MAX_GENE_COUNT)
        assertEquals(11, GenomeContract.PARAMETRIC_VALUE_COUNT)
        assertEquals(11, GenomeContract.INTEGER_VALUE_COUNT)
        assertEquals(7, GenomeContract.FLOAT_VALUE_COUNT)
        assertEquals(40, CellDataConstants.LEGACY_GENOME_GENE_COUNT)
        assertEquals(256, CellDataConstants.MAX_GENOME_GENE_COUNT)

        val bridge = TestReleaseGenomeCodec()
        val bytes = ByteArrayOutputStream().also { output ->
            GenomeFileCodec.write(
                output,
                GenomeDraft.editorDefaults { listOf(0.3f, 0.75f, 0.3f) },
                releaseCodec = bridge
            )
        }

        assertArrayEquals("CLTY".encodeToByteArray(), bytes.toByteArray().copyOfRange(0, 4))
        assertEquals(
            GenomeContract.WIRE_HEADER_BYTES + GenomeContract.WIRE_BYTES_PER_GENE,
            bridge.lastEncodedWire?.size
        )
        assertTrue(bytes.size().toLong() <= CellularityFileLimits.MAX_GENOME_SIZE_BYTES)
    }
}
