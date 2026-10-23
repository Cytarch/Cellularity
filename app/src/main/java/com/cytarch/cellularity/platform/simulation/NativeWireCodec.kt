package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.CellularityDataException
import com.cytarch.cellularity.shared.files.release.EmbeddedPreviewFormat
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.model.SimulationContract
import java.nio.BufferUnderflowException
import java.nio.ByteBuffer
import java.nio.ByteOrder

internal data class NativeSubstrateInspection(
    val familyLocalVersion: Int,
    val time: Double,
    val cellCount: Int,
    val configuration: SimulationConfiguration,
    val preview: NativeSubstratePreviewDescriptor? = null,
    val formatFamily: NativeSubstrateFormatFamily = NativeSubstrateFormatFamily.UNKNOWN,
    val contentSchemaMajor: Int = 0,
    val contentSchemaMinor: Int = 0,
    val loadSupported: Boolean = true,
    val foodCount: Int? = null,
    val foodMass: Double? = null,
    val createdAtUnixMillis: Long? = null,
    val knownOrganismCount: Int? = null,
    val organismIdentityComplete: Boolean = false
)

internal enum class NativeSubstrateFormatFamily {
    UNKNOWN,
    ORIGINAL_JAVA,
    RELEASE_CLTY
}

internal data class NativeSubstratePreviewDescriptor(
    val format: EmbeddedPreviewFormat,
    val pixelWidth: Int,
    val pixelHeight: Int,
    val byteCount: Long,
    val encodedCrc32: Long,
    val usable: Boolean
)

/** Decodes the small, transient native-to-Android payloads into shared immutable models. */
internal object NativeWireCodec {
    private const val CELL_SNAPSHOT_MAGIC = 0x434c4353 // CLCS
    private const val CELL_SNAPSHOT_VERSION = 8
    private const val SUBSTRATE_INSPECTION_MAGIC = 0x434c494e // CLIN
    private const val SUBSTRATE_INSPECTION_VERSION = 7
    private const val MAX_CELL_SNAPSHOT_BYTES = 4 * 1024
    private const val MAX_INSPECTION_BYTES = 160

    fun decodeCellSnapshot(bytes: ByteArray?): CellSnapshot? {
        if (bytes == null) return null
        return decode(bytes, "cell snapshot", MAX_CELL_SNAPSHOT_BYTES) { input ->
            val version = readVersionedHeader(
                input,
                expectedMagic = CELL_SNAPSHOT_MAGIC,
                currentVersion = CELL_SNAPSHOT_VERSION,
                payloadName = "cell snapshot"
            )
            val id = input.int.requireAtLeast(0, "Cell ID")
            val positionX = input.finiteDouble("Cell position X")
            val positionY = input.finiteDouble("Cell position Y")
            val positionZ = input.finiteDouble("Cell position Z")
            val radius = input.finiteDouble("Cell radius")
            val age = input.finiteDouble("Cell age")
            val mass = input.finiteDouble("Cell mass")
            val nitrates = input.finiteDouble("Cell nitrates")
            val velocityX = input.finiteDouble("Cell velocity X")
            val velocityY = input.finiteDouble("Cell velocity Y")
            val angle = input.finiteDouble("Cell angle")
            val angularVelocity = input.finiteDouble("Cell angular velocity")
            val toxin = input.finiteFloat("Cell toxin")
            val toxication = input.finiteFloat("Cell toxication")
            val lipids = input.finiteFloat("Cell lipids")
            val geneIndex = input.int
            if (version in 5..7) input.int // Retired transient field.
            val originTag = input.int
            val selectTag = input.int
            val mutationCount = input.int
            input.int // Pending mutation count is native-only state.
            val telomeresCount = input.int
            val mirrored = input.get().toInt() != 0
            val cellType = try {
                CellType.fromNativeValue(input.int)
            } catch (failure: IllegalArgumentException) {
                throw dataError(
                    CellularityErrorCode.INVALID_VALUE,
                    failure.message.orEmpty(),
                    failure
                )
            }
            val color = List(4) { input.finiteFloat("Cell color") }
            val signalSubstance =
                List(4) { input.finiteFloat("Cell signal") }
            val newSignalSubstance =
                List(4) { input.finiteFloat("Cell new signal") }
            val organismId = if (version >= 7) input.long else 0L
            val parentOrganismId = if (version >= 7) input.long else 0L
            if (
                organismId < 0L ||
                parentOrganismId < 0L ||
                (organismId == 0L && parentOrganismId != 0L) ||
                (organismId != 0L &&
                    parentOrganismId != 0L &&
                    parentOrganismId >= organismId)
            ) {
                throw dataError(
                    CellularityErrorCode.INVALID_VALUE,
                    "Invalid Cell organism identity"
                )
            }
            CellSnapshot(
                id = id,
                positionX = positionX,
                positionY = positionY,
                positionZ = positionZ,
                radius = radius,
                age = age,
                mass = mass,
                nitrates = nitrates,
                velocityX = velocityX,
                velocityY = velocityY,
                angle = angle,
                angularVelocity = angularVelocity,
                toxin = toxin,
                toxication = toxication,
                lipids = lipids,
                geneIndex = geneIndex,
                originTag = originTag,
                selectTag = selectTag,
                mutationCount = mutationCount,
                telomeresCount = telomeresCount,
                mirrored = mirrored,
                cellType = cellType,
                color = color,
                signalSubstance = signalSubstance,
                newSignalSubstance = newSignalSubstance,
                organismId = organismId,
                parentOrganismId = parentOrganismId
            )
        }
    }

    fun decodeSubstrateInspection(bytes: ByteArray): NativeSubstrateInspection =
        decode(bytes, "substrate inspection", MAX_INSPECTION_BYTES) { input ->
            val wireVersion = readVersionedHeader(
                input,
                expectedMagic = SUBSTRATE_INSPECTION_MAGIC,
                currentVersion = SUBSTRATE_INSPECTION_VERSION,
                payloadName = "substrate inspection"
            )
            val familyLocalVersion = input.int
            if (familyLocalVersion <= 0) {
                throw dataError(
                    CellularityErrorCode.INVALID_VERSION,
                    "Invalid substrate product version"
                )
            }
            val time = input.finiteDouble("Substrate time")
            val cellCount = input.int.requireAtLeast(0, "Substrate cell count")
            var configuration = SimulationConfiguration(
                maxCellCount = input.int,
                maxFoodCount = input.int,
                radius = input.finiteDouble("Substrate radius")
            )
            if (!SimulationContract.isConfigurationValid(configuration)) {
                throw dataError(
                    CellularityErrorCode.INVALID_VALUE,
                    "Unsupported substrate allocation parameters"
                )
            }
            if (cellCount > configuration.maxCellCount) {
                throw dataError(
                    CellularityErrorCode.LIMIT_EXCEEDED,
                    "Substrate cell count exceeds its configured limit"
                )
            }
            val preview = if (wireVersion >= 3) {
                decodeSubstratePreviewDescriptor(input)
            } else {
                null
            }
            val formatCompatibility = if (wireVersion >= 4) {
                decodeSubstrateFormatCompatibility(input, familyLocalVersion)
            } else {
                NativeSubstrateFormatCompatibility()
            }
            val foodCount: Int?
            val foodMass: Double?
            val createdAtUnixMillis: Long?
            if (wireVersion >= 5) {
                val encodedFoodCount = input.int
                val encodedFoodMass = input.finiteDouble("Substrate food mass")
                val encodedCreatedAtUnixMillis = input.long
                if (
                    encodedFoodCount < -1 ||
                    encodedFoodCount > configuration.maxFoodCount ||
                    (encodedFoodMass < 0.0 && encodedFoodMass != -1.0) ||
                    (encodedFoodCount == -1) != (encodedFoodMass == -1.0) ||
                    encodedCreatedAtUnixMillis < 0
                ) {
                    throw dataError(
                        CellularityErrorCode.INVALID_VALUE,
                        "Invalid extended substrate metadata"
                    )
                }
                foodCount = encodedFoodCount.takeIf { it >= 0 }
                foodMass = encodedFoodMass.takeIf { it >= 0.0 }
                createdAtUnixMillis = encodedCreatedAtUnixMillis.takeIf { it > 0 }
            } else {
                foodCount = null
                foodMass = null
                createdAtUnixMillis = null
            }
            val knownOrganismCount: Int?
            val organismIdentityComplete: Boolean
            if (wireVersion >= 6) {
                val encodedKnownOrganismCount = input.int
                val encodedIdentityComplete = input.int
                if (
                    encodedKnownOrganismCount < -1 ||
                    encodedKnownOrganismCount > cellCount ||
                    encodedIdentityComplete !in 0..1 ||
                    (encodedIdentityComplete == 1 &&
                        encodedKnownOrganismCount < 0)
                ) {
                    throw dataError(
                        CellularityErrorCode.INVALID_VALUE,
                        "Invalid substrate organism metadata"
                    )
                }
                knownOrganismCount =
                    encodedKnownOrganismCount.takeIf { it >= 0 }
                organismIdentityComplete = encodedIdentityComplete == 1
            } else {
                knownOrganismCount = null
                organismIdentityComplete = false
            }
            if (wireVersion >= 7) {
                configuration = configuration.copy(
                    innerRadius = input.finiteDouble("Substrate inner radius")
                )
                if (!SimulationContract.isConfigurationValid(configuration)) {
                    throw dataError(CellularityErrorCode.INVALID_VALUE, "Invalid substrate geometry")
                }
            }
            NativeSubstrateInspection(
                familyLocalVersion = familyLocalVersion,
                time = time,
                cellCount = cellCount,
                configuration = configuration,
                preview = preview,
                formatFamily = formatCompatibility.family,
                contentSchemaMajor = formatCompatibility.contentSchemaMajor,
                contentSchemaMinor = formatCompatibility.contentSchemaMinor,
                loadSupported = formatCompatibility.loadSupported,
                foodCount = foodCount,
                foodMass = foodMass,
                createdAtUnixMillis = createdAtUnixMillis,
                knownOrganismCount = knownOrganismCount,
                organismIdentityComplete = organismIdentityComplete
            )
        }

    private fun decodeSubstrateFormatCompatibility(
        input: ByteBuffer,
        familyLocalVersion: Int
    ): NativeSubstrateFormatCompatibility {
        val family = when (input.int) {
            1 -> NativeSubstrateFormatFamily.ORIGINAL_JAVA
            2 -> NativeSubstrateFormatFamily.RELEASE_CLTY
            else -> throw dataError(
                CellularityErrorCode.INVALID_VALUE,
                "Invalid substrate format family"
            )
        }
        val contentSchemaMajor = input.int
        val contentSchemaMinor = input.int
        val loadSupportedValue = input.int
        if (
            contentSchemaMajor < 0 ||
            contentSchemaMinor < 0 ||
            loadSupportedValue !in 0..1 ||
            family == NativeSubstrateFormatFamily.RELEASE_CLTY &&
            (contentSchemaMajor == 0 || familyLocalVersion != contentSchemaMajor) ||
            family != NativeSubstrateFormatFamily.RELEASE_CLTY &&
            (contentSchemaMajor != 0 || contentSchemaMinor != 0)
        ) {
            throw dataError(
                CellularityErrorCode.INVALID_VALUE,
                "Invalid substrate format compatibility descriptor"
            )
        }
        return NativeSubstrateFormatCompatibility(
            family = family,
            contentSchemaMajor = contentSchemaMajor,
            contentSchemaMinor = contentSchemaMinor,
            loadSupported = loadSupportedValue == 1
        )
    }

    private fun decodeSubstratePreviewDescriptor(
        input: ByteBuffer
    ): NativeSubstratePreviewDescriptor {
        val codec = input.int
        val width = input.int
        val height = input.int
        val byteCount = input.long
        val encodedCrc32 = input.int.toLong() and 0xffff_ffffL
        val usableValue = input.int
        val format = when (codec) {
            0 -> EmbeddedPreviewFormat.NONE
            1 -> EmbeddedPreviewFormat.PNG
            2 -> EmbeddedPreviewFormat.JPEG
            3 -> EmbeddedPreviewFormat.WEBP_LOSSY
            4 -> EmbeddedPreviewFormat.WEBP_LOSSLESS
            else -> EmbeddedPreviewFormat.UNKNOWN
        }
        val dimensionsValid = width in 0..MAXIMUM_PREVIEW_DIMENSION &&
            height in 0..MAXIMUM_PREVIEW_DIMENSION &&
            width.toLong() * height.toLong() <= MAXIMUM_PREVIEW_PIXELS
        val byteCountValid = byteCount in 0..MAXIMUM_PREVIEW_BYTES
        val usable = usableValue == 1
        if (
            usableValue !in 0..1 ||
            !dimensionsValid ||
            !byteCountValid ||
            usable && (
                format == EmbeddedPreviewFormat.NONE ||
                    format == EmbeddedPreviewFormat.UNKNOWN ||
                    width == 0 || height == 0 || byteCount == 0L
                ) ||
            format == EmbeddedPreviewFormat.NONE &&
            (width != 0 || height != 0 || byteCount != 0L || encodedCrc32 != 0L)
        ) {
            throw dataError(
                CellularityErrorCode.INVALID_VALUE,
                "Invalid substrate preview descriptor"
            )
        }
        return NativeSubstratePreviewDescriptor(
            format = format,
            pixelWidth = width,
            pixelHeight = height,
            byteCount = byteCount,
            encodedCrc32 = encodedCrc32,
            usable = usable
        )
    }

    private inline fun <T> decode(
        bytes: ByteArray,
        payloadName: String,
        maximumBytes: Int,
        block: (ByteBuffer) -> T
    ): T {
        if (bytes.size > maximumBytes) {
            throw dataError(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "$payloadName exceeds its wire size limit"
            )
        }
        val input = ByteBuffer.wrap(bytes).order(ByteOrder.BIG_ENDIAN)
        return try {
            val result = block(input)
            if (input.hasRemaining()) {
                throw dataError(
                    CellularityErrorCode.TRAILING_DATA,
                    "$payloadName contains trailing data"
                )
            }
            result
        } catch (failure: BufferUnderflowException) {
            throw dataError(
                CellularityErrorCode.TRUNCATED_INPUT,
                "$payloadName ended unexpectedly",
                failure
            )
        }
    }

    private fun readVersionedHeader(
        input: ByteBuffer,
        expectedMagic: Int,
        currentVersion: Int,
        payloadName: String
    ): Int {
        if (input.int != expectedMagic) {
            throw dataError(
                CellularityErrorCode.INVALID_HEADER,
                "Invalid $payloadName header"
            )
        }
        val version = input.int
        if (version <= 0) {
            throw dataError(
                CellularityErrorCode.INVALID_VERSION,
                "Invalid $payloadName version"
            )
        }
        if (version > currentVersion) {
            throw dataError(
                CellularityErrorCode.NEWER_SNAPSHOT_VERSION,
                "Unsupported newer $payloadName version"
            )
        }
        return version
    }

    private fun ByteBuffer.finiteDouble(name: String): Double = double.also { value ->
        if (!value.isFinite()) {
            throw dataError(CellularityErrorCode.INVALID_VALUE, "$name must be finite")
        }
    }

    private fun ByteBuffer.finiteFloat(name: String): Float = float.also { value ->
        if (!value.isFinite()) {
            throw dataError(CellularityErrorCode.INVALID_VALUE, "$name must be finite")
        }
    }

    private fun Int.requireAtLeast(minimum: Int, name: String): Int = also { value ->
        if (value < minimum) {
            throw dataError(CellularityErrorCode.INVALID_COUNT, "$name is negative")
        }
    }

    private fun dataError(
        code: CellularityErrorCode,
        message: String,
        cause: Throwable? = null
    ): CellularityDataException = CellularityDataException(code, message).also { error ->
        if (cause != null) error.initCause(cause)
    }

    private const val MAXIMUM_PREVIEW_DIMENSION = 4096
    private const val MAXIMUM_PREVIEW_PIXELS = 16L * 1024L * 1024L
    private const val MAXIMUM_PREVIEW_BYTES = 8L * 1024L * 1024L

    private data class NativeSubstrateFormatCompatibility(
        val family: NativeSubstrateFormatFamily = NativeSubstrateFormatFamily.UNKNOWN,
        val contentSchemaMajor: Int = 0,
        val contentSchemaMinor: Int = 0,
        val loadSupported: Boolean = true
    )
}
