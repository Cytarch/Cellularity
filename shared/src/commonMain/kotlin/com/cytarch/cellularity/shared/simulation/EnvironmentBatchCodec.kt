package com.cytarch.cellularity.shared.simulation

import com.cytarch.cellularity.shared.model.EnvironmentContract
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldSnapshot
import com.cytarch.cellularity.shared.model.VectorFieldType

/**
 * The only wire mapping for the versioned native environment contract.
 *
 * It lives here rather than beside either caller because there are two of them now: the
 * Android backend reads the same document from the same native code, and a second
 * implementation would be a second chance to disagree with the native writer about a
 * field order that fails silently when it is wrong.
 *
 * The reader and writer are the private classes at the bottom of this file rather than
 * the JVM data streams, because this is shared common code and those streams belong to
 * one platform. Every integer is written most significant byte first, which is what the
 * native writer in native/io/NativeByteStream.h emits and what the sibling
 * [com.cytarch.cellularity.shared.model.GenomeDraftWireCodec] emits; the two must not
 * drift, because a byte-order disagreement decodes to plausible field values rather than
 * to an error. Every read is bounds checked so that a truncated payload fails as an
 * [EnvironmentContractException] instead of reading past the end or short-reading.
 */
object EnvironmentBatchCodec {
    const val STATE_MAGIC = 0x43455653 // CEVS
    const val PATCH_MAGIC = 0x43455650 // CEVP
    const val RESULT_MAGIC = 0x43455652 // CEVR
    const val MAX_WIRE_BYTES = 64 * 1024

    private const val CHANGE_BOOLEAN = 1
    private const val CHANGE_SCALAR = 2
    private const val CHANGE_FIELD_TYPE = 3
    private const val CHANGE_FIELD_VALUE = 4
    private const val CHANGE_MUTATION_WEIGHT = 5
    private const val CHANGE_RESET_MUTATION_WEIGHTS = 6
    private const val CHANGE_GENE_POOL = 7
    private const val FIELD_TYPE_SCALAR = 1
    private const val FIELD_TYPE_VECTOR = 2

    fun decodeSnapshot(bytes: ByteArray): EnvironmentSnapshot = readPayload(
        bytes = bytes,
        payloadName = "environment state"
    ) { input ->
        requireHeader(input, STATE_MAGIC, "environment state")
        val schemaVersion = input.readInt()
        requireSupportedSchema(schemaVersion)
        val revision = input.readLong().also {
            contractCheck(it >= 0) { "Environment state has a negative revision" }
        }

        val addRandom = input.readBoolean()
        val dieEdge = input.readBoolean()
        val onlyTweakMutations = input.readBoolean()
        val mutationOnlyAfterSplit = input.readBoolean()
        val aging = input.readBoolean()
        val mobileFood = input.readBoolean()

        val gravityField = VectorFieldSnapshot(
            type = vectorFieldType(input.readInt()),
            strength = input.readFiniteDouble("gravity strength"),
            angle = input.readFiniteDouble("gravity angle")
        )
        val densityField = input.readScalarField("density")
        val salinityField = input.readScalarField("salinity")
        val foodLipidLayerField = input.readScalarField("nutrient coating")

        val scalars = DoubleArray(23) { index ->
            input.readFiniteDouble("environment scalar ${index + 1}")
        }
        val genePool = input.readExactBooleanList(
            expectedCount = EnvironmentContract.GENE_POOL_SIZE,
            name = "gene pool"
        )
        val mutationWeights = input.readExactDoubleList(
            expectedCount = EnvironmentContract.MUTATION_WEIGHT_COUNT,
            name = "mutation weights"
        )

        EnvironmentSnapshot(
            schemaVersion = schemaVersion,
            revision = revision,
            addRandom = addRandom,
            dieEdge = dieEdge,
            onlyTweakMutations = onlyTweakMutations,
            mutationOnlyAfterSplit = mutationOnlyAfterSplit,
            aging = aging,
            mobileFood = mobileFood,
            gravityField = gravityField,
            densityField = densityField,
            salinityField = salinityField,
            foodLipidLayerField = foodLipidLayerField,
            radiation = scalars[0],
            testMulticellularCoef = scalars[1],
            testMutationCoef = scalars[2],
            testFoodDecay = scalars[3],
            testIdleMetabolismScale = scalars[4],
            testFlagellocyteCostScale = scalars[5],
            testPhagocyteEatSpeedScale = scalars[6],
            viscosity = scalars[7],
            frictionDynamic = scalars[8],
            frictionStatic = scalars[9],
            foodSpeed = scalars[10],
            foodSize = scalars[11],
            foodLumpiness = scalars[12],
            foodLumpSize = scalars[13],
            foodLipidLayer = scalars[14],
            salinity = scalars[15],
            lightIntensity = scalars[16],
            lightRange = scalars[17],
            lightSpeed = scalars[18],
            gravity = scalars[19],
            density = scalars[20],
            densityGradient = scalars[21],
            nitrates = scalars[22],
            genePool = genePool,
            mutationWeights = mutationWeights
        )
    }

    fun encodePatch(patch: EnvironmentPatch): ByteArray {
        val output = EnvironmentWireWriter()
        output.writeInt(PATCH_MAGIC)
        output.writeInt(patch.schemaVersion)
        output.writeLong(patch.baseRevision)
        output.writeInt(patch.changes.size)
        patch.changes.forEach { change -> output.writeChange(change) }
        return output.toByteArray().also { bytes ->
            contractCheck(bytes.size <= MAX_WIRE_BYTES) {
                "Environment patch exceeds the wire size limit"
            }
        }
    }

    fun decodeApplyResult(bytes: ByteArray, patch: EnvironmentPatch): ApplyResult = readPayload(
        bytes = bytes,
        payloadName = "environment apply result"
    ) { input ->
        requireHeader(input, RESULT_MAGIC, "environment apply result")
        requireSupportedSchema(input.readInt())
        val applied = input.readBoolean()
        val revision = input.readLong().also {
            contractCheck(it >= 0) { "Environment apply result has a negative revision" }
        }
        val rejectionCount = input.readInt()
        contractCheck(rejectionCount in 0..(patch.changes.size + 1)) {
            "Environment apply result has an invalid rejection count"
        }
        val rejections = List(rejectionCount) {
            val changeIndex = input.readInt()
            contractCheck(changeIndex == -1 || changeIndex in patch.changes.indices) {
                "Environment apply result refers to an unknown change"
            }
            val reason = try {
                EnvironmentPatchRejectionReason.fromWireId(input.readInt())
            } catch (failure: IllegalArgumentException) {
                throw EnvironmentContractException(failure.message.orEmpty(), failure)
            }
            EnvironmentPatchRejection(changeIndex, reason)
        }
        contractCheck(applied == rejections.isEmpty()) {
            "Environment apply result has inconsistent status and rejections"
        }
        if (applied) {
            ApplyResult.Applied(revision)
        } else {
            contractCheck(rejections.isNotEmpty()) {
                "Rejected environment patch has no rejection reason"
            }
            ApplyResult.Rejected(revision, rejections)
        }
    }

    private fun EnvironmentWireWriter.writeChange(change: EnvironmentChange) {
        when (change) {
            is EnvironmentChange.BooleanValue -> {
                writeInt(CHANGE_BOOLEAN)
                writeInt(change.property.wireId)
                writeBoolean(change.value)
            }
            is EnvironmentChange.ScalarValue -> {
                writeInt(CHANGE_SCALAR)
                writeInt(change.property.wireId)
                writeDouble(change.value)
            }
            is EnvironmentChange.FieldType -> {
                writeInt(CHANGE_FIELD_TYPE)
                writeInt(change.field.wireId)
                when (val type = change.value) {
                    is ScalarFieldType -> {
                        writeInt(FIELD_TYPE_SCALAR)
                        writeInt(type.nativeValue)
                    }
                    is VectorFieldType -> {
                        writeInt(FIELD_TYPE_VECTOR)
                        writeInt(type.nativeValue)
                    }
                }
            }
            is EnvironmentChange.FieldValue -> {
                writeInt(CHANGE_FIELD_VALUE)
                writeInt(change.field.wireId)
                writeInt(change.component.wireId)
                writeDouble(change.value)
            }
            is EnvironmentChange.MutationWeight -> {
                writeInt(CHANGE_MUTATION_WEIGHT)
                writeInt(change.index)
                writeDouble(change.value)
            }
            EnvironmentChange.ResetMutationWeights -> {
                writeInt(CHANGE_RESET_MUTATION_WEIGHTS)
            }
            is EnvironmentChange.GenePoolEnabled -> {
                writeInt(CHANGE_GENE_POOL)
                writeInt(change.index)
                writeBoolean(change.enabled)
            }
        }
    }

    private fun EnvironmentWireReader.readScalarField(name: String) = ScalarFieldSnapshot(
        type = scalarFieldType(readInt()),
        base = readFiniteDouble("$name base"),
        strength = readFiniteDouble("$name strength"),
        angle = readFiniteDouble("$name angle"),
        offset = readFiniteDouble("$name offset"),
        softness = readFiniteDouble("$name softness")
    )

    private fun EnvironmentWireReader.readExactBooleanList(
        expectedCount: Int,
        name: String
    ): List<Boolean> {
        val count = readInt()
        contractCheck(count == expectedCount) { "Environment $name count is $count, expected $expectedCount" }
        return List(count) { readBoolean() }
    }

    private fun EnvironmentWireReader.readExactDoubleList(
        expectedCount: Int,
        name: String
    ): List<Double> {
        val count = readInt()
        contractCheck(count == expectedCount) { "Environment $name count is $count, expected $expectedCount" }
        return List(count) { index -> readFiniteDouble("$name[$index]") }
    }

    private fun EnvironmentWireReader.readFiniteDouble(name: String): Double = readDouble().also { value ->
        contractCheck(value.isFinite()) { "Environment $name is not finite" }
    }

    private fun scalarFieldType(value: Int): ScalarFieldType = try {
        ScalarFieldType.fromNativeValue(value)
    } catch (failure: IllegalArgumentException) {
        throw EnvironmentContractException(failure.message.orEmpty(), failure)
    }

    private fun vectorFieldType(value: Int): VectorFieldType = try {
        VectorFieldType.fromNativeValue(value)
    } catch (failure: IllegalArgumentException) {
        throw EnvironmentContractException(failure.message.orEmpty(), failure)
    }

    private fun <T> readPayload(
        bytes: ByteArray,
        payloadName: String,
        read: (EnvironmentWireReader) -> T
    ): T {
        contractCheck(bytes.size <= MAX_WIRE_BYTES) { "$payloadName exceeds the wire size limit" }
        val input = EnvironmentWireReader(bytes = bytes, payloadName = payloadName)
        val value = read(input)
        contractCheck(input.remaining == 0) { "$payloadName contains trailing data" }
        return value
    }

    private fun requireHeader(input: EnvironmentWireReader, expected: Int, name: String) {
        contractCheck(input.readInt() == expected) { "Invalid $name header" }
    }

    private fun requireSupportedSchema(schemaVersion: Int) {
        contractCheck(schemaVersion == EnvironmentContract.SCHEMA_VERSION) {
            "Unsupported environment schema version: $schemaVersion"
        }
    }

    private inline fun contractCheck(value: Boolean, lazyMessage: () -> String) {
        if (!value) throw EnvironmentContractException(lazyMessage())
    }
}

/**
 * Big-endian, bounds-checked reader for one wire document.
 *
 * The most significant byte comes first for every multi-byte value, matching
 * native/io/NativeByteStream.h and
 * [com.cytarch.cellularity.shared.model.GenomeDraftWireCodec]. A read past the end of
 * the payload is a contract failure rather than a short read, so no caller has to tell a
 * truncated document from a valid one by inspecting the value it received.
 */
private class EnvironmentWireReader(
    private val bytes: ByteArray,
    private val payloadName: String
) {
    private var position = 0

    val remaining: Int
        get() = bytes.size - position

    fun readBoolean(): Boolean = readUnsignedByte() != 0

    fun readInt(): Int =
        (readUnsignedByte() shl 24) or
            (readUnsignedByte() shl 16) or
            (readUnsignedByte() shl 8) or
            readUnsignedByte()

    fun readLong(): Long =
        (readUnsignedByte().toLong() shl 56) or
            (readUnsignedByte().toLong() shl 48) or
            (readUnsignedByte().toLong() shl 40) or
            (readUnsignedByte().toLong() shl 32) or
            (readUnsignedByte().toLong() shl 24) or
            (readUnsignedByte().toLong() shl 16) or
            (readUnsignedByte().toLong() shl 8) or
            readUnsignedByte().toLong()

    fun readDouble(): Double = Double.fromBits(readLong())

    private fun readUnsignedByte(): Int {
        if (position >= bytes.size) {
            throw EnvironmentContractException(
                "Could not decode $payloadName: the payload ended unexpectedly"
            )
        }
        return bytes[position++].toInt() and 0xff
    }
}

/** Big-endian growable writer for one wire document. */
private class EnvironmentWireWriter(initialCapacity: Int = INITIAL_CAPACITY) {
    private var bytes = ByteArray(initialCapacity.coerceAtLeast(1))
    private var size = 0

    fun writeBoolean(value: Boolean) {
        writeByte(if (value) 1 else 0)
    }

    fun writeInt(value: Int) {
        writeByte(value ushr 24)
        writeByte(value ushr 16)
        writeByte(value ushr 8)
        writeByte(value)
    }

    fun writeLong(value: Long) {
        writeByte((value ushr 56).toInt())
        writeByte((value ushr 48).toInt())
        writeByte((value ushr 40).toInt())
        writeByte((value ushr 32).toInt())
        writeByte((value ushr 24).toInt())
        writeByte((value ushr 16).toInt())
        writeByte((value ushr 8).toInt())
        writeByte(value.toInt())
    }

    fun writeDouble(value: Double) = writeLong(value.toRawBits())

    fun toByteArray(): ByteArray = bytes.copyOf(size)

    private fun writeByte(value: Int) {
        if (size == bytes.size) {
            bytes = bytes.copyOf(size * 2)
        }
        bytes[size++] = value.toByte()
    }

    private companion object {
        const val INITIAL_CAPACITY = 32
    }
}

class EnvironmentContractException(
    message: String,
    cause: Throwable? = null
) : RuntimeException(message, cause)
