package com.cytarch.cellularity.shared.model

/**
 * What a saved plate says about itself, as the native codec reports it.
 *
 * A collection shows a plate by what is inside it -- how many cells, how long it has been running,
 * how much food is left -- and none of that is in the file's header: it is in the metadata block and
 * the body, which only the release codec can read. The phone asks the native side for this document
 * and lists the answer; the desktop has the same codec linked and asks the same question, so the
 * shape of the answer is described once, here, rather than twice in two platforms' own words.
 *
 * Every field is what the file says, including the ones it may not have: a plate written before an
 * optional field existed reports it as absent rather than as zero, because "no food recorded" and
 * "no food left" are different things to show a person.
 */
data class SubstrateInspection(
    /** The version inside the format's own family; never comparable with a schema version. */
    val familyLocalVersion: Int,
    /** Seconds of simulated time. */
    val time: Double,
    val cellCount: Int,
    val configuration: SimulationConfiguration,
    val preview: SubstratePreviewDescriptor? = null,
    val formatFamily: SubstrateFormatFamily = SubstrateFormatFamily.UNKNOWN,
    val contentSchemaMajor: Int = 0,
    val contentSchemaMinor: Int = 0,
    val loadSupported: Boolean = true,
    val foodCount: Int? = null,
    val foodMass: Double? = null,
    val createdAtUnixMillis: Long? = null,
    val knownOrganismCount: Int? = null,
    val organismIdentityComplete: Boolean = false
)

/** Which of the two families a substrate file belongs to. */
enum class SubstrateFormatFamily {
    UNKNOWN,
    ORIGINAL_JAVA,
    RELEASE_CLTY
}

/** The picture a plate carries, described by the container rather than by its bytes. */
data class SubstratePreviewDescriptor(
    val format: EmbeddedPreviewFormat,
    val pixelWidth: Int,
    val pixelHeight: Int,
    val byteCount: Long,
    val encodedCrc32: Long,
    val usable: Boolean
)

/** The picture format a container's preview section declares. */
enum class EmbeddedPreviewFormat {
    NONE,
    PNG,
    JPEG,
    WEBP_LOSSY,
    WEBP_LOSSLESS,
    UNKNOWN
}

/**
 * Reads the native side's account of a substrate file.
 *
 * The document is the same one Android has always read: a magic number, a version, then the fields
 * the version declares, big-endian and with nothing after them. It is decoded strictly -- a
 * document that claims more than the buffer holds, or leaves bytes unread, is refused rather than
 * half-read -- because the alternative is a collection that shows a number nobody can justify.
 *
 * A field a version does not have is reported as absent, never as zero. Versions are read forward:
 * a document from an older build answers everything that build knew and nothing more, which is what
 * lets the collection show an old plate honestly instead of showing it empty.
 */
object SubstrateInspectionWireCodec {
    private const val MAGIC = 0x434c494e // CLIN
    private const val CURRENT_VERSION = 7

    /** The largest a document of this shape can be; a longer one is not one. */
    private const val MAXIMUM_BYTES = 160

    private const val MAXIMUM_PREVIEW_DIMENSION = 4096
    private const val MAXIMUM_PREVIEW_PIXELS = 16L * 1024L * 1024L
    private const val MAXIMUM_PREVIEW_BYTES = 8L * 1024L * 1024L

    /**
     * The inspection [bytes] describe, or null when they are not one.
     *
     * Null rather than an exception, because every caller of this is describing a file it has
     * already accepted: a document it cannot read means the file cannot be described, which the
     * collection shows as "unknown" -- the same answer it gave before this existed, so a failure
     * here can never make a plate that opened look broken.
     */
    fun decodeOrNull(bytes: ByteArray?): SubstrateInspection? {
        if (bytes == null || bytes.isEmpty() || bytes.size > MAXIMUM_BYTES) return null
        return try {
            decode(bytes)
        } catch (failure: InspectionFormatException) {
            null
        }
    }

    private fun decode(bytes: ByteArray): SubstrateInspection {
        val input = InspectionReader(bytes)
        if (input.int() != MAGIC) throw InspectionFormatException("Not a substrate inspection")
        val version = input.int()
        if (version <= 0) throw InspectionFormatException("Invalid inspection version")
        if (version > CURRENT_VERSION) {
            throw InspectionFormatException("Newer inspection version")
        }
        val familyLocalVersion = input.int()
        if (familyLocalVersion <= 0) throw InspectionFormatException("Invalid product version")
        val time = input.finiteDouble()
        val cellCount = input.int()
        if (cellCount < 0) throw InspectionFormatException("Negative cell count")
        val configuration = SimulationConfiguration(
            maxCellCount = input.int(),
            maxFoodCount = input.int(),
            radius = input.finiteDouble()
        )
        if (!SimulationContract.isConfigurationValid(configuration)) {
            throw InspectionFormatException("Unsupported allocation")
        }
        if (cellCount > configuration.maxCellCount) {
            throw InspectionFormatException("Cell count exceeds its limit")
        }
        val preview = if (version >= 3) readPreview(input) else null
        val compatibility = if (version >= 4) readCompatibility(input, familyLocalVersion) else null

        var foodCount: Int? = null
        var foodMass: Double? = null
        var createdAtUnixMillis: Long? = null
        if (version >= 5) {
            val encodedFoodCount = input.int()
            val encodedFoodMass = input.finiteDouble()
            val encodedCreatedAt = input.long()
            if (encodedFoodCount < -1 ||
                encodedFoodCount > configuration.maxFoodCount ||
                (encodedFoodMass < 0.0 && encodedFoodMass != -1.0) ||
                (encodedFoodCount == -1) != (encodedFoodMass == -1.0) ||
                encodedCreatedAt < 0
            ) {
                throw InspectionFormatException("Invalid extended metadata")
            }
            foodCount = encodedFoodCount.takeIf { it >= 0 }
            foodMass = encodedFoodMass.takeIf { it >= 0.0 }
            createdAtUnixMillis = encodedCreatedAt.takeIf { it > 0 }
        }

        var knownOrganismCount: Int? = null
        var organismIdentityComplete = false
        if (version >= 6) {
            val encodedKnownOrganismCount = input.int()
            val encodedIdentityComplete = input.int()
            if (encodedKnownOrganismCount < -1 ||
                encodedKnownOrganismCount > cellCount ||
                encodedIdentityComplete !in 0..1 ||
                (encodedIdentityComplete == 1 && encodedKnownOrganismCount < 0)
            ) {
                throw InspectionFormatException("Invalid organism metadata")
            }
            knownOrganismCount = encodedKnownOrganismCount.takeIf { it >= 0 }
            organismIdentityComplete = encodedIdentityComplete == 1
        }

        var effectiveConfiguration = configuration
        if (version >= 7) {
            effectiveConfiguration = configuration.copy(innerRadius = input.finiteDouble())
            if (!SimulationContract.isConfigurationValid(effectiveConfiguration)) {
                throw InspectionFormatException("Invalid geometry")
            }
        }
        if (input.hasRemaining()) throw InspectionFormatException("Trailing data")

        return SubstrateInspection(
            familyLocalVersion = familyLocalVersion,
            time = time,
            cellCount = cellCount,
            configuration = effectiveConfiguration,
            preview = preview,
            formatFamily = compatibility?.family ?: SubstrateFormatFamily.UNKNOWN,
            contentSchemaMajor = compatibility?.contentSchemaMajor ?: 0,
            contentSchemaMinor = compatibility?.contentSchemaMinor ?: 0,
            loadSupported = compatibility?.loadSupported ?: true,
            foodCount = foodCount,
            foodMass = foodMass,
            createdAtUnixMillis = createdAtUnixMillis,
            knownOrganismCount = knownOrganismCount,
            organismIdentityComplete = organismIdentityComplete
        )
    }

    private fun readPreview(input: InspectionReader): SubstratePreviewDescriptor? {
        val codec = input.int()
        val width = input.int()
        val height = input.int()
        val byteCount = input.long()
        val encodedCrc32 = input.int().toLong() and 0xffff_ffffL
        val usableValue = input.int()
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
        if (usableValue !in 0..1 ||
            !dimensionsValid ||
            !byteCountValid ||
            (usable && (format == EmbeddedPreviewFormat.NONE ||
                format == EmbeddedPreviewFormat.UNKNOWN ||
                width == 0 || height == 0 || byteCount == 0L)) ||
            (format == EmbeddedPreviewFormat.NONE &&
                (width != 0 || height != 0 || byteCount != 0L))
        ) {
            throw InspectionFormatException("Invalid preview descriptor")
        }
        return if (format == EmbeddedPreviewFormat.NONE) {
            null
        } else {
            SubstratePreviewDescriptor(
                format = format,
                pixelWidth = width,
                pixelHeight = height,
                byteCount = byteCount,
                encodedCrc32 = encodedCrc32,
                usable = usable
            )
        }
    }

    private class Compatibility(
        val family: SubstrateFormatFamily,
        val contentSchemaMajor: Int,
        val contentSchemaMinor: Int,
        val loadSupported: Boolean
    )

    private fun readCompatibility(
        input: InspectionReader,
        familyLocalVersion: Int
    ): Compatibility {
        val family = when (input.int()) {
            1 -> SubstrateFormatFamily.ORIGINAL_JAVA
            2 -> SubstrateFormatFamily.RELEASE_CLTY
            else -> throw InspectionFormatException("Unknown format family")
        }
        val contentSchemaMajor = input.int()
        val contentSchemaMinor = input.int()
        val loadSupportedValue = input.int()
        if (contentSchemaMajor < 0 ||
            contentSchemaMinor < 0 ||
            loadSupportedValue !in 0..1 ||
            (family == SubstrateFormatFamily.RELEASE_CLTY &&
                (contentSchemaMajor == 0 || familyLocalVersion != contentSchemaMajor)) ||
            (family != SubstrateFormatFamily.RELEASE_CLTY &&
                (contentSchemaMajor != 0 || contentSchemaMinor != 0))
        ) {
            throw InspectionFormatException("Invalid format descriptor")
        }
        return Compatibility(
            family = family,
            contentSchemaMajor = contentSchemaMajor,
            contentSchemaMinor = contentSchemaMinor,
            loadSupported = loadSupportedValue == 1
        )
    }

    /**
     * A reader that refuses to read past its end.
     *
     * Written here rather than reached for from the platform, because this codec is common code: a
     * document that ends early must be a refusal on both platforms and in the same words, not a
     * platform-specific exception about buffers.
     */
    private class InspectionReader(private val bytes: ByteArray) {
        private var position = 0

        fun hasRemaining(): Boolean = position < bytes.size

        fun int(): Int {
            require(4)
            val value = ((bytes[position].toInt() and 0xff) shl 24) or
                ((bytes[position + 1].toInt() and 0xff) shl 16) or
                ((bytes[position + 2].toInt() and 0xff) shl 8) or
                (bytes[position + 3].toInt() and 0xff)
            position += 4
            return value
        }

        fun long(): Long = int().toLong().shl(32) or (int().toLong() and 0xffff_ffffL)

        fun finiteDouble(): Double {
            val value = Double.fromBits(long())
            if (!value.isFinite()) throw InspectionFormatException("Expected a finite number")
            return value
        }

        private fun require(count: Int) {
            if (position + count > bytes.size) {
                throw InspectionFormatException("The document ended early")
            }
        }
    }

    private class InspectionFormatException(message: String) : Exception(message)
}
