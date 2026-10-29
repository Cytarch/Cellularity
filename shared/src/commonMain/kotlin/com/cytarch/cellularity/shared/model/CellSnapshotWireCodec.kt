package com.cytarch.cellularity.shared.model

/**
 * A cell snapshot document that does not describe a cell.
 *
 * The error code travels with the exception because callers act on it rather than on the
 * text: a snapshot from a newer runtime is a version problem, and a snapshot that ends early
 * is a truncation, and the two are told apart when a failure is reported. The wording is for
 * a person reading a log; the code is for the code.
 */
class CellSnapshotWireFormatException(
    val code: CellularityErrorCode,
    message: String,
    cause: Throwable? = null
) : RuntimeException(message, cause)

/**
 * The cell snapshot the native runtime writes for one cell, as the shared model.
 *
 * Cell inspection is the one place where the two platforms read the same native document for
 * the same purpose: a person selects a cell and asks what it is. The document was decoded on
 * the phone and nowhere else, which meant the desktop could read it out of the runtime and
 * had no way to say what it contained. Putting the decoder here is what makes the answer the
 * same on both -- and the same is the point, because a cell's diameter cannot be one number
 * on a phone and another in a window.
 *
 * The format is big-endian and versioned, and older versions are read rather than refused:
 * versions 1 to 8 exist, and the fields a given version does not carry are the ones the
 * native side did not write yet. Retired fields are stepped over rather than read, which is
 * the reason the version has to be honoured at all.
 */
object CellSnapshotWireCodec {

    private const val MAGIC = 0x434c4353 // CLCS
    private const val CURRENT_VERSION = 8
    private const val MAXIMUM_BYTES = 4 * 1024

    /** Reads a cell snapshot, or returns null for a null document. */
    fun decode(bytes: ByteArray?): CellSnapshot? {
        if (bytes == null) return null
        if (bytes.size > MAXIMUM_BYTES) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.LIMIT_EXCEEDED,
                "cell snapshot exceeds its wire size limit"
            )
        }
        val input = CellSnapshotWireReader(bytes)
        val version = input.readVersionedHeader(MAGIC, CURRENT_VERSION, "cell snapshot")

        val id = input.readInt().requireAtLeast(0, "Cell ID")
        val positionX = input.readFiniteDouble("Cell position X")
        val positionY = input.readFiniteDouble("Cell position Y")
        val positionZ = input.readFiniteDouble("Cell position Z")
        val radius = input.readFiniteDouble("Cell radius")
        val age = input.readFiniteDouble("Cell age")
        val mass = input.readFiniteDouble("Cell mass")
        val nitrates = input.readFiniteDouble("Cell nitrates")
        val velocityX = input.readFiniteDouble("Cell velocity X")
        val velocityY = input.readFiniteDouble("Cell velocity Y")
        val angle = input.readFiniteDouble("Cell angle")
        val angularVelocity = input.readFiniteDouble("Cell angular velocity")
        val toxin = input.readFiniteFloat("Cell toxin")
        val toxication = input.readFiniteFloat("Cell toxication")
        val lipids = input.readFiniteFloat("Cell lipids")
        val geneIndex = input.readInt()
        if (version in 5..7) input.readInt() // Retired transient field.
        val originTag = input.readInt()
        val selectTag = input.readInt()
        val mutationCount = input.readInt()
        input.readInt() // Pending mutation count is native-only state.
        val telomeresCount = input.readInt()
        val mirrored = input.readByte().toInt() != 0
        val cellType = try {
            CellType.fromNativeValue(input.readInt())
        } catch (failure: IllegalArgumentException) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.INVALID_VALUE,
                failure.message.orEmpty(),
                failure
            )
        }
        val color = List(4) { input.readFiniteFloat("Cell color") }
        val signalSubstance = List(4) { input.readFiniteFloat("Cell signal") }
        val newSignalSubstance = List(4) { input.readFiniteFloat("Cell new signal") }
        val organismId = if (version >= 7) input.readLong() else 0L
        val parentOrganismId = if (version >= 7) input.readLong() else 0L

        // The identity rules are the runtime's, restated where the values enter the model:
        // an organism has an identity or has none, and a parent cannot be newer than its
        // child. A document that says otherwise was not written by this runtime.
        if (
            organismId < 0L ||
            parentOrganismId < 0L ||
            (organismId == 0L && parentOrganismId != 0L) ||
            (organismId != 0L && parentOrganismId != 0L && parentOrganismId >= organismId)
        ) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.INVALID_VALUE,
                "Invalid Cell organism identity"
            )
        }

        input.requireExhausted("cell snapshot")

        return CellSnapshot(
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

    private fun Int.requireAtLeast(minimum: Int, name: String): Int = also { value ->
        if (value < minimum) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.INVALID_COUNT,
                "$name is negative"
            )
        }
    }
}

/**
 * A big-endian reader over one document, which refuses to read past the end.
 *
 * The bounds checks are the whole reason this exists rather than `ByteBuffer`: an underflow
 * there is an exception a caller has to translate, and here it is the failure the format
 * already has a name for, raised at the point where the short read happened.
 */
private class CellSnapshotWireReader(private val bytes: ByteArray) {
    private var position = 0

    fun readByte(): Byte {
        require(1)
        return bytes[position++]
    }

    fun readInt(): Int {
        require(4)
        val value = ((bytes[position].toInt() and 0xFF) shl 24) or
            ((bytes[position + 1].toInt() and 0xFF) shl 16) or
            ((bytes[position + 2].toInt() and 0xFF) shl 8) or
            (bytes[position + 3].toInt() and 0xFF)
        position += 4
        return value
    }

    fun readLong(): Long {
        require(8)
        var value = 0L
        repeat(8) { index ->
            value = (value shl 8) or (bytes[position + index].toLong() and 0xFF)
        }
        position += 8
        return value
    }

    fun readFiniteDouble(name: String): Double {
        val value = Double.fromBits(readLong())
        if (!value.isFinite()) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.INVALID_VALUE,
                "$name must be finite"
            )
        }
        return value
    }

    fun readFiniteFloat(name: String): Float {
        val value = Float.fromBits(readInt())
        if (!value.isFinite()) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.INVALID_VALUE,
                "$name must be finite"
            )
        }
        return value
    }

    fun readVersionedHeader(expectedMagic: Int, currentVersion: Int, payloadName: String): Int {
        if (readInt() != expectedMagic) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.INVALID_HEADER,
                "Invalid $payloadName header"
            )
        }
        val version = readInt()
        if (version <= 0) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.INVALID_VERSION,
                "Invalid $payloadName version"
            )
        }
        if (version > currentVersion) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.NEWER_SNAPSHOT_VERSION,
                "Unsupported newer $payloadName version"
            )
        }
        return version
    }

    /** A document that carries more than its fields say is not one this reader understood. */
    fun requireExhausted(payloadName: String) {
        if (position != bytes.size) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.TRAILING_DATA,
                "$payloadName contains trailing data"
            )
        }
    }

    private fun require(count: Int) {
        if (position + count > bytes.size) {
            throw CellSnapshotWireFormatException(
                CellularityErrorCode.TRUNCATED_INPUT,
                "cell snapshot ended unexpectedly"
            )
        }
    }
}
