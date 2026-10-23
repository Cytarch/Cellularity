package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GenomeContract
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

data class GenomeOscillatorPresetConfiguration(
    val adhesinConnections: Int,
    val firstChannel: Int,
    val secondChannel: Int,
    val periodHours: Float
) {
    init {
        require(adhesinConnections in GenomeOscillatorPreset.CONNECTION_RANGE)
        require(firstChannel in 0 until CellDataConstants.SIGNAL_SUBSTANCE_COUNT)
        require(secondChannel in 0 until CellDataConstants.SIGNAL_SUBSTANCE_COUNT)
        require(firstChannel != secondChannel)
        require(periodHours.isFinite() && periodHours in GenomeOscillatorPreset.PERIOD_RANGE)
    }
}

data class GenomeOscillatorPresetSource(
    val firstInputChannel: Int,
    val secondInputChannel: Int,
    val inhibitionFactor: Float,
    val amplificationFactor: Float
)

/** Pure form of the Neurocyte oscillator setup previously embedded in the Android dialog. */
object GenomeOscillatorPreset {
    const val MIN_PERIOD_HOURS = 0.25f
    const val MAX_PERIOD_HOURS = 9.25f
    const val DEFAULT_PERIOD_HOURS = 2.0f
    val CONNECTION_RANGE = 0..5
    val PERIOD_RANGE = MIN_PERIOD_HOURS..MAX_PERIOD_HOURS

    private const val WORLD_DELTA_TIME = 0.02f
    private const val SIGNAL_PASSES = 4
    private const val MAX_SIMULATION_STEPS = 20_000 * SIGNAL_PASSES

    fun sourceFrom(gene: GeneDraft): GenomeOscillatorPresetSource {
        val outputStart = GenomeContract.GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX
        return GenomeOscillatorPresetSource(
            firstInputChannel = gene.parametricValues[outputStart + 1]
                .signalSubstanceChannelIndex.toInt(),
            secondInputChannel = gene.parametricValues[outputStart]
                .signalSubstanceChannelIndex.toInt(),
            inhibitionFactor = gene.parametricValues[outputStart].variableA,
            amplificationFactor = gene.parametricValues[outputStart + 2].variableA
        )
    }

    fun configurationFor(gene: GeneDraft): GenomeOscillatorPresetConfiguration =
        configurationFor(sourceFrom(gene))

    fun configurationFor(source: GenomeOscillatorPresetSource): GenomeOscillatorPresetConfiguration {
        val outputStart = GenomeContract.GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX
        var firstChannel = source.firstInputChannel
        var secondChannel = source.secondInputChannel
        val outputMaximum = GenomeContract.parametricMaximums[outputStart]
        val inhibition = outputMaximum * source.inhibitionFactor
        val amplification = outputMaximum * source.amplificationFactor
        var connections = (
            (amplification - CellDataConstants.SIGNAL_SUBSTANCE_DECAY * 1.5f) /
                (CellDataConstants.SIGNAL_SUBSTANCE_FLOW * 0.8f)
            ).roundToInt()
        var period = if (connections in CONNECTION_RANGE) {
            estimatePeriod(connections, amplification, inhibition)
        } else {
            -1.0f
        }

        if (period > MIN_PERIOD_HOURS * 0.5f && period < MIN_PERIOD_HOURS) {
            period = MIN_PERIOD_HOURS
        }
        if (period > MAX_PERIOD_HOURS && period < MAX_PERIOD_HOURS + MIN_PERIOD_HOURS * 0.5f) {
            period = MAX_PERIOD_HOURS
        }

        val channelsAreValid = firstChannel in 0 until CellDataConstants.SIGNAL_SUBSTANCE_COUNT &&
            secondChannel in 0 until CellDataConstants.SIGNAL_SUBSTANCE_COUNT &&
            firstChannel != secondChannel
        if (connections !in CONNECTION_RANGE || !channelsAreValid || period !in PERIOD_RANGE) {
            connections = 1
            firstChannel = 0
            secondChannel = 1
            period = DEFAULT_PERIOD_HOURS
        }

        return GenomeOscillatorPresetConfiguration(
            adhesinConnections = connections,
            firstChannel = firstChannel,
            secondChannel = secondChannel,
            periodHours = period
        )
    }

    fun applyTo(
        gene: GeneDraft,
        configuration: GenomeOscillatorPresetConfiguration
    ): GeneDraft {
        val outputStart = GenomeContract.GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX
        val integerStart = GenomeContract.GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX
        val outputMaximum = GenomeContract.parametricMaximums[outputStart]
        val amplification = CellDataConstants.SIGNAL_SUBSTANCE_DECAY * 1.5f +
            configuration.adhesinConnections * CellDataConstants.SIGNAL_SUBSTANCE_FLOW * 0.8f
        var inhibitionLower = 0.0f
        var inhibitionUpper = outputMaximum * CellDataConstants.VALUE_VARIABLE_MAX
        var inhibition = (inhibitionLower + inhibitionUpper) * 0.5f
        repeat(25) {
            val estimated = estimatePeriod(
                configuration.adhesinConnections,
                amplification,
                inhibition
            )
            if (estimated < configuration.periodHours) {
                inhibitionUpper = inhibition
            } else {
                inhibitionLower = inhibition
            }
            inhibition = (inhibitionLower + inhibitionUpper) * 0.5f
        }

        val channels = listOf(
            configuration.firstChannel,
            configuration.secondChannel,
            configuration.firstChannel,
            configuration.secondChannel
        )
        val inputChannels = listOf(
            configuration.secondChannel,
            configuration.firstChannel,
            configuration.firstChannel,
            configuration.secondChannel
        )
        val outputFactors = listOf(
            inhibition / outputMaximum,
            -inhibition / outputMaximum,
            amplification / outputMaximum,
            amplification / outputMaximum
        )
        var result = gene
        repeat(CellDataConstants.NEUROCYTE_OUTPUT_COUNT) { offset ->
            result = result
                .withIntegerValue(integerStart + offset, channels[offset])
                .withParametricValue(
                    outputStart + offset,
                    result.parametricValues[outputStart + offset].copy(
                        variableA = outputFactors[offset],
                        variableB = 0.0f,
                        signalSubstanceChannelIndex = inputChannels[offset].toShort(),
                        formulaType = 1
                    )
                )
        }
        return result
    }

    internal fun estimatePeriod(
        connectionCount: Int,
        amplification: Float,
        inhibition: Float
    ): Float {
        require(connectionCount in CONNECTION_RANGE)
        var first = 1.0f
        var second = 0.0f
        var flips = 0
        var measurementStart = 0.0f
        val deltaTime = WORLD_DELTA_TIME / SIGNAL_PASSES
        val otherFirst = FloatArray(connectionCount)
        val otherSecond = FloatArray(connectionCount)
        var step = 1
        while (step < MAX_SIMULATION_STEPS) {
            var nextFirst = first + (
                -CellDataConstants.SIGNAL_SUBSTANCE_DECAY * first +
                    min(
                        CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
                        max(-CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT, amplification * first)
                    ) +
                    min(
                        CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
                        max(-CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT, inhibition * second)
                    )
                ) * deltaTime
            var nextSecond = second + (
                -CellDataConstants.SIGNAL_SUBSTANCE_DECAY * second +
                    min(
                        CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
                        max(-CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT, amplification * second)
                    ) +
                    min(
                        CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT,
                        max(-CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT, -inhibition * first)
                    )
                ) * deltaTime
            repeat(connectionCount) { connection ->
                nextFirst -= CellDataConstants.SIGNAL_SUBSTANCE_FLOW *
                    (first - otherFirst[connection]) * deltaTime
                nextSecond -= CellDataConstants.SIGNAL_SUBSTANCE_FLOW *
                    (second - otherSecond[connection]) * deltaTime
                otherFirst[connection] += (
                    CellDataConstants.SIGNAL_SUBSTANCE_FLOW * (first - otherFirst[connection]) -
                        CellDataConstants.SIGNAL_SUBSTANCE_DECAY * otherFirst[connection]
                    ) * deltaTime
                otherSecond[connection] += (
                    CellDataConstants.SIGNAL_SUBSTANCE_FLOW * (second - otherSecond[connection]) -
                        CellDataConstants.SIGNAL_SUBSTANCE_DECAY * otherSecond[connection]
                    ) * deltaTime
            }
            if (nextSecond * second < 0.0f) {
                flips++
                if (flips == 6) measurementStart = step * deltaTime
                if (flips == 14) break
            }
            first = nextFirst.coerceIn(-1.0f, 1.0f)
            second = nextSecond.coerceIn(-1.0f, 1.0f)
            step++
        }
        return (step * deltaTime - measurementStart) / 4.0f
    }
}
