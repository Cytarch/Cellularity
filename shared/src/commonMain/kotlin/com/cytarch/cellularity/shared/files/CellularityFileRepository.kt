package com.cytarch.cellularity.shared.files

import com.cytarch.cellularity.shared.model.GenomeDraft

/**
 * Platform storage boundary used by common presentation code.
 *
 * Every operation is asynchronous. Expected storage failures are returned as
 * [StorageResult.Failure]; coroutine cancellation is propagated to the caller.
 */
interface CellularityFileRepository {
    suspend fun listSubstrateIds(): StorageResult<List<StoredSubstrateId>>
    suspend fun readSubstrateInfo(id: StoredSubstrateId): StorageResult<SubstrateFileInfo>
    suspend fun substrateExists(name: String?): StorageResult<Boolean>
    suspend fun deleteSubstrate(id: StoredSubstrateId): StorageResult<Unit>
    suspend fun renameSubstrate(
        id: StoredSubstrateId,
        newDisplayName: String
    ): StorageResult<Unit>

    suspend fun listGenomeIds(): StorageResult<List<StoredGenomeId>>
    suspend fun listGenomeNames(): StorageResult<List<String>>
    suspend fun readGenomeInfo(id: StoredGenomeId): StorageResult<GenomeFileInfo>
    suspend fun readGenomeDraft(nameOrFilename: String?): StorageResult<GenomeDraft>
    suspend fun writeGenomeDraft(
        nameOrFilename: String?,
        genome: GenomeDraft
    ): StorageResult<Unit>
    suspend fun deleteGenome(id: StoredGenomeId): StorageResult<Unit>
    suspend fun renameGenome(
        id: StoredGenomeId,
        newDisplayName: String
    ): StorageResult<Unit>
}

/** Opaque identifier of an application-owned substrate. */
data class StoredSubstrateId(val value: String) {
    init {
        require(CellularityFileNames.isValidDisplayName(value)) {
            "Substrate name must be a non-empty file name"
        }
    }
}

/** Opaque identifier of an application-owned genome file. */
data class StoredGenomeId(val fileName: String) {
    init {
        require(CellularityFileNames.isGenomeFileName(fileName)) {
            "Genome ID must contain the .genome extension"
        }
        require(
            CellularityFileNames.isValidDisplayName(
                CellularityFileNames.genomeNameFromFileName(fileName)
            )
        ) {
            "Genome ID must be a safe, non-empty file name"
        }
    }

    val defaultDisplayName: String
        get() = CellularityFileNames.genomeNameFromFileName(fileName)
}

enum class StorageErrorCode {
    NOT_FOUND,
    NAME_EXISTS,
    BAD_NAME,
    UNSUPPORTED_FORMAT,
    NEWER_VERSION,
    CORRUPTED,
    TOO_LARGE,
    PERMISSION_REVOKED,
    IO_FAILURE
}

data class StorageError(
    val code: StorageErrorCode,
    val detail: String = ""
)

sealed interface StorageResult<out T> {
    data class Success<T>(val value: T) : StorageResult<T>
    data class Failure(val error: StorageError) : StorageResult<Nothing>
}

enum class FileLoadCompatibility {
    LOADABLE,
    NEWER_VERSION
}

data class SubstrateFileInfo(
    val time: Double = 0.0,
    val cellCount: Int = 0,
    val maximumCellCount: Int? = null,
    val foodCount: Int? = null,
    val maximumFoodCount: Int? = null,
    val foodMass: Double? = null,
    val dishDiameterMillimeters: Double? = null,
    val createdAtUnixMillis: Long? = null,
    val knownOrganismCount: Int? = null,
    val organismIdentityComplete: Boolean = false,
    val previewAvailable: Boolean = false,
    val compatibility: FileLoadCompatibility = FileLoadCompatibility.LOADABLE
)

data class GenomeFileInfo(
    val displayName: String,
    val geneCount: Int? = null,
    val initialGeneIndex: Int? = null,
    val uniqueGeneCount: Int? = null,
    val cellTypeMask: Long? = null,
    val previewAvailable: Boolean = false,
    val compatibility: FileLoadCompatibility = FileLoadCompatibility.LOADABLE
)

/** File limits at the common storage boundary. */
object CellularityFileLimits {
    /** Bounded CLTY genome, including an optional 8 MiB embedded preview. */
    const val MAX_GENOME_SIZE_BYTES = 16L * 1024L * 1024L
}

object CellularityFileNames {
    private const val SUBSTRATE_EXTENSION = ".substrate"
    private const val GENOME_EXTENSION = ".genome"

    fun substrateFileName(name: String?): String {
        val validName = requireValidDisplayName(name)
        return "$validName$SUBSTRATE_EXTENSION"
    }

    fun substrateNameFromFileName(filename: String): String {
        return filename.dropLast(SUBSTRATE_EXTENSION.length)
    }

    fun substrateNameFromAnyName(nameOrFilename: String): String {
        return if (nameOrFilename.endsWith(SUBSTRATE_EXTENSION)) {
            nameOrFilename.dropLast(SUBSTRATE_EXTENSION.length)
        } else {
            nameOrFilename
        }
    }

    fun isValidDisplayName(name: String?): Boolean {
        if (name == null || name.isBlank()) return false
        if (name == "." || name == "..") return false
        return '/' !in name && '\\' !in name && '\u0000' !in name
    }

    fun displayNameFromFileName(filename: String): String {
        return when {
            filename.endsWith(SUBSTRATE_EXTENSION) -> substrateNameFromFileName(filename)
            filename.endsWith(GENOME_EXTENSION) -> genomeNameFromFileName(filename)
            else -> filename
        }
    }

    fun isSubstrateFileName(filename: String): Boolean {
        return filename.endsWith(SUBSTRATE_EXTENSION)
    }

    fun genomeFileName(nameOrFilename: String?): String {
        val candidate = requireValidDisplayName(nameOrFilename)
        val baseName = if (candidate.endsWith(GENOME_EXTENSION)) {
            candidate.dropLast(GENOME_EXTENSION.length)
        } else {
            candidate
        }
        return "${requireValidDisplayName(baseName)}$GENOME_EXTENSION"
    }

    fun genomeNameFromFileName(filename: String): String {
        return if (filename.endsWith(GENOME_EXTENSION)) {
            filename.dropLast(GENOME_EXTENSION.length)
        } else {
            filename
        }
    }

    fun isGenomeFileName(filename: String): Boolean {
        return filename.endsWith(GENOME_EXTENSION)
    }

    private fun requireValidDisplayName(name: String?): String {
        require(isValidDisplayName(name)) { "Invalid Cellularity file name" }
        return requireNotNull(name)
    }
}
