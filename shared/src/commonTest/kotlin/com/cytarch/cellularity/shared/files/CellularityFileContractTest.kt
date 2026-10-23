package com.cytarch.cellularity.shared.files

import com.cytarch.cellularity.shared.model.GenomeContract
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class CellularityFileContractTest {
    @Test
    fun storedSubstrateIdAcceptsDisplayNamesButRejectsPaths() {
        assertEquals("Living colony", StoredSubstrateId("Living colony").value)
        for (invalid in listOf("", " ", ".", "..", "folder/name", "folder\\name", "bad\u0000name")) {
            assertFailsWith<IllegalArgumentException> { StoredSubstrateId(invalid) }
        }
    }

    @Test
    fun storedGenomeIdKeepsFilenameSeparateFromDisplayName() {
        val id = StoredGenomeId("Living genome.genome")

        assertEquals("Living genome.genome", id.fileName)
        assertEquals("Living genome", id.defaultDisplayName)
        for (invalid in listOf("", "genome", ".genome", "folder/name.genome")) {
            assertFailsWith<IllegalArgumentException> { StoredGenomeId(invalid) }
        }
    }

    @Test
    fun genomeFilenameRequiresAnExplicitValidName() {
        assertEquals("sample.genome", CellularityFileNames.genomeFileName("sample"))
        assertEquals("sample.genome", CellularityFileNames.genomeFileName("sample.genome"))
        for (invalid in listOf(null, "", " ", ".", "..", "folder/name")) {
            assertFailsWith<IllegalArgumentException> {
                CellularityFileNames.genomeFileName(invalid)
            }
        }
    }

    @Test
    fun releaseGenomeLimitLeavesBoundedSpaceForContainerAndPreview() {
        assertEquals(
            GenomeContract.WIRE_HEADER_BYTES +
                GenomeContract.MAX_GENE_COUNT * GenomeContract.WIRE_BYTES_PER_GENE,
            GenomeContract.MAX_WIRE_BYTES
        )
        assertEquals(16L * 1024L * 1024L, CellularityFileLimits.MAX_GENOME_SIZE_BYTES)
        assertTrue(
            CellularityFileLimits.MAX_GENOME_SIZE_BYTES > GenomeContract.MAX_WIRE_BYTES
        )
    }

    @Test
    fun storageFailuresCoverEveryExpectedOperationalOutcome() {
        assertEquals(
            setOf(
                StorageErrorCode.NOT_FOUND,
                StorageErrorCode.NAME_EXISTS,
                StorageErrorCode.BAD_NAME,
                StorageErrorCode.UNSUPPORTED_FORMAT,
                StorageErrorCode.NEWER_VERSION,
                StorageErrorCode.CORRUPTED,
                StorageErrorCode.TOO_LARGE,
                StorageErrorCode.PERMISSION_REVOKED,
                StorageErrorCode.IO_FAILURE
            ),
            StorageErrorCode.entries.toSet()
        )
    }
}
