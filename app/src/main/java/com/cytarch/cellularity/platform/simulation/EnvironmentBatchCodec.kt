package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.shared.model.EnvironmentContract
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldSnapshot
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.simulation.ApplyResult
import com.cytarch.cellularity.shared.simulation.EnvironmentChange
import com.cytarch.cellularity.shared.simulation.EnvironmentPatch
import com.cytarch.cellularity.shared.simulation.EnvironmentPatchRejection
import com.cytarch.cellularity.shared.simulation.EnvironmentPatchRejectionReason
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.IOException

/** The only Android wire mapping for the versioned native environment contract. */
internal object EnvironmentBatchCodec {
    internal const val STATE_MAGIC = 0x43455653 // CEVS
    internal const val PATCH_MAGIC = 0x43455650 // CEVP
    internal const val RESULT_MAGIC = 0x43455652 // CEVR
    internal const val MAX_WIRE_BYTES = 64 * 1024

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
        val outputBytes = ByteArrayOutputStream()
        DataOutputStream(outputBytes).use { output ->
            output.writeInt(PATCH_MAGIC)
            output.writeInt(patch.schemaVersion)
            output.writeLong(patch.baseRevision)
            output.writeInt(patch.changes.size)
            patch.changes.forEach { change -> output.writeChange(change) }
        }
        return outputBytes.toByteArray().also { bytes ->
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

    private fun DataOutputStream.writeChange(change: EnvironmentChange) {
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

    private fun DataInputStream.readScalarField(name: String) = ScalarFieldSnapshot(
        type = scalarFieldType(readInt()),
        base = readFiniteDouble("$name base"),
        strength = readFiniteDouble("$name strength"),
        angle = readFiniteDouble("$name angle"),
        offset = readFiniteDouble("$name offset"),
        softness = readFiniteDouble("$name softness")
    )

    private fun DataInputStream.readExactBooleanList(
        expectedCount: Int,
        name: String
    ): List<Boolean> {
        val count = readInt()
        contractCheck(count == expectedCount) { "Environment $name count is $count, expected $expectedCount" }
        return List(count) { readBoolean() }
    }

    private fun DataInputStream.readExactDoubleList(
        expectedCount: Int,
        name: String
    ): List<Double> {
        val count = readInt()
        contractCheck(count == expectedCount) { "Environment $name count is $count, expected $expectedCount" }
        return List(count) { index -> readFiniteDouble("$name[$index]") }
    }

    private fun DataInputStream.readFiniteDouble(name: String): Double = readDouble().also { value ->
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

    private inline fun <T> readPayload(
        bytes: ByteArray,
        payloadName: String,
        read: (DataInputStream) -> T
    ): T {
        contractCheck(bytes.size <= MAX_WIRE_BYTES) { "$payloadName exceeds the wire size limit" }
        try {
            val byteInput = ByteArrayInputStream(bytes)
            val input = DataInputStream(byteInput)
            val value = read(input)
            contractCheck(byteInput.available() == 0) { "$payloadName contains trailing data" }
            return value
        } catch (failure: EnvironmentContractException) {
            throw failure
        } catch (failure: IOException) {
            throw EnvironmentContractException("Could not decode $payloadName", failure)
        }
    }

    private fun requireHeader(input: DataInputStream, expected: Int, name: String) {
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

internal class EnvironmentContractException(
    message: String,
    cause: Throwable? = null
) : RuntimeException(message, cause)
