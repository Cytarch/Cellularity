package com.cytarch.cellularity.platform.simulation

enum class SimulationRatePolicy {
    PAUSED,
    TARGET_RATE,
    MAX_THROUGHPUT
}

enum class SimulationBatchStopReason {
    NO_WORK,
    QUOTA_COMPLETED,
    SERVICE_REQUESTED,
    SERVICE_DEADLINE_REACHED
}

/**
 * Immutable owner-side rate and frame-exchange observation.
 *
 * Values are cumulative unless their name starts with `last` or `maximum`.
 * [requestedTargetUps] is present only for [SimulationRatePolicy.TARGET_RATE];
 * actual speed is an observation and never feeds the scheduler.
 */
data class MicroscopeRuntimeTelemetry(
    val ratePolicy: SimulationRatePolicy = SimulationRatePolicy.PAUSED,
    val requestedTargetUps: Double? = null,
    val actualRateSampleSequence: Long = 0,
    val actualRateValid: Boolean = false,
    val actualCompletedUps: Double = 0.0,
    val actualSimulationHoursPerSecond: Double = 0.0,
    val actualRateWindowNanos: Long = 0,
    val actualRateWindowUpdates: Long = 0,
    val policyGeneration: Long = 0,
    val learningResetCount: Long = 0,
    val plannedCycleCount: Long = 0,
    val observedBatchCount: Long = 0,
    val observedUpdateCount: Long = 0,
    val totalObservedBatchNanos: Long = 0,
    val maximumObservedBatchNanos: Long = 0,
    val lastBatchUpdateCount: Long = 0,
    val maximumBatchUpdateCount: Long = 0,
    val lastBatchNanos: Long = 0,
    val lastPlannedUpdateQuota: Long = 0,
    val lastClockCheckStride: Long = 0,
    val updateCostInitialized: Boolean = false,
    val fastUpdateCostNanos: Long = 0,
    val slowUpdateCostNanos: Long = 0,
    val estimatedUpdateCostNanos: Long = 0,
    val quotaExceededCount: Long = 0,
    val deadlineOverrunCount: Long = 0,
    val maximumDeadlineOverrunNanos: Long = 0,
    val lastStopReason: SimulationBatchStopReason = SimulationBatchStopReason.NO_WORK,
    val ownerServiceCount: Long = 0,
    val lastOwnerServiceIntervalNanos: Long = 0,
    val maximumOwnerServiceIntervalNanos: Long = 0,
    val publishedSourceFrameCount: Long = 0,
    val droppedSourceFrameCount: Long = 0,
    val publishedPreparedFrameCount: Long = 0,
    val droppedPreparedFrameCount: Long = 0,
    val statusPublishedCount: Long = 0,
    val statusConsumedCount: Long = 0,
    val statusProducerReplacedCount: Long = 0,
    val statusConsumerDiscardedCount: Long = 0,
    val statusPublishRetriesExhausted: Long = 0
)

internal object NativeMicroscopeRuntimeTelemetryCodec {
    private const val SCHEMA_VERSION = 1L
    private const val FIELD_COUNT = 42

    fun decode(values: LongArray): MicroscopeRuntimeTelemetry? {
        if (values.size < FIELD_COUNT || values[SCHEMA] != SCHEMA_VERSION) return null

        val policy = when (values[POLICY]) {
            0L -> SimulationRatePolicy.PAUSED
            1L -> SimulationRatePolicy.TARGET_RATE
            2L -> SimulationRatePolicy.MAX_THROUGHPUT
            else -> return null
        }
        val requestedTarget = when (values[REQUESTED_TARGET_PRESENT]) {
            0L -> null
            1L -> Double.fromBits(values[REQUESTED_TARGET_BITS])
                .takeIf { it.isFinite() && it > 0.0 }
                ?: return null
            else -> return null
        }
        if ((policy == SimulationRatePolicy.TARGET_RATE) != (requestedTarget != null)) {
            return null
        }

        val actualUps = Double.fromBits(values[ACTUAL_COMPLETED_UPS_BITS])
        val actualHoursPerSecond =
            Double.fromBits(values[ACTUAL_SIMULATION_HOURS_PER_SECOND_BITS])
        if (!actualUps.isFinite() || actualUps < 0.0 ||
            !actualHoursPerSecond.isFinite() || actualHoursPerSecond < 0.0
        ) {
            return null
        }
        val actualRateValid = booleanValue(values[ACTUAL_RATE_VALID]) ?: return null
        val updateCostInitialized =
            booleanValue(values[UPDATE_COST_INITIALIZED]) ?: return null
        val stopReason = when (values[LAST_STOP_REASON]) {
            0L -> SimulationBatchStopReason.NO_WORK
            1L -> SimulationBatchStopReason.QUOTA_COMPLETED
            2L -> SimulationBatchStopReason.SERVICE_REQUESTED
            3L -> SimulationBatchStopReason.SERVICE_DEADLINE_REACHED
            else -> return null
        }
        if (NON_NEGATIVE_FIELDS.any { values[it] < 0L }) return null

        return MicroscopeRuntimeTelemetry(
            ratePolicy = policy,
            requestedTargetUps = requestedTarget,
            actualRateSampleSequence = values[ACTUAL_RATE_SAMPLE_SEQUENCE],
            actualRateValid = actualRateValid,
            actualCompletedUps = actualUps,
            actualSimulationHoursPerSecond = actualHoursPerSecond,
            actualRateWindowNanos = values[ACTUAL_RATE_WINDOW_NANOS],
            actualRateWindowUpdates = values[ACTUAL_RATE_WINDOW_UPDATES],
            policyGeneration = values[POLICY_GENERATION],
            learningResetCount = values[LEARNING_RESET_COUNT],
            plannedCycleCount = values[PLANNED_CYCLE_COUNT],
            observedBatchCount = values[OBSERVED_BATCH_COUNT],
            observedUpdateCount = values[OBSERVED_UPDATE_COUNT],
            totalObservedBatchNanos = values[TOTAL_OBSERVED_BATCH_NANOS],
            maximumObservedBatchNanos = values[MAXIMUM_OBSERVED_BATCH_NANOS],
            lastBatchUpdateCount = values[LAST_BATCH_UPDATE_COUNT],
            maximumBatchUpdateCount = values[MAXIMUM_BATCH_UPDATE_COUNT],
            lastBatchNanos = values[LAST_BATCH_NANOS],
            lastPlannedUpdateQuota = values[LAST_PLANNED_UPDATE_QUOTA],
            lastClockCheckStride = values[LAST_CLOCK_CHECK_STRIDE],
            updateCostInitialized = updateCostInitialized,
            fastUpdateCostNanos = values[FAST_UPDATE_COST_NANOS],
            slowUpdateCostNanos = values[SLOW_UPDATE_COST_NANOS],
            estimatedUpdateCostNanos = values[ESTIMATED_UPDATE_COST_NANOS],
            quotaExceededCount = values[QUOTA_EXCEEDED_COUNT],
            deadlineOverrunCount = values[DEADLINE_OVERRUN_COUNT],
            maximumDeadlineOverrunNanos = values[MAXIMUM_DEADLINE_OVERRUN_NANOS],
            lastStopReason = stopReason,
            ownerServiceCount = values[OWNER_SERVICE_COUNT],
            lastOwnerServiceIntervalNanos = values[LAST_OWNER_SERVICE_INTERVAL_NANOS],
            maximumOwnerServiceIntervalNanos = values[MAXIMUM_OWNER_SERVICE_INTERVAL_NANOS],
            publishedSourceFrameCount = values[PUBLISHED_SOURCE_FRAME_COUNT],
            droppedSourceFrameCount = values[DROPPED_SOURCE_FRAME_COUNT],
            publishedPreparedFrameCount = values[PUBLISHED_PREPARED_FRAME_COUNT],
            droppedPreparedFrameCount = values[DROPPED_PREPARED_FRAME_COUNT],
            statusPublishedCount = values[STATUS_PUBLISHED_COUNT],
            statusConsumedCount = values[STATUS_CONSUMED_COUNT],
            statusProducerReplacedCount = values[STATUS_PRODUCER_REPLACED_COUNT],
            statusConsumerDiscardedCount = values[STATUS_CONSUMER_DISCARDED_COUNT],
            statusPublishRetriesExhausted = values[STATUS_PUBLISH_RETRIES_EXHAUSTED]
        )
    }

    private fun booleanValue(value: Long): Boolean? = when (value) {
        0L -> false
        1L -> true
        else -> null
    }

    private val NON_NEGATIVE_FIELDS = intArrayOf(
        ACTUAL_RATE_SAMPLE_SEQUENCE,
        ACTUAL_RATE_WINDOW_NANOS,
        ACTUAL_RATE_WINDOW_UPDATES,
        POLICY_GENERATION,
        LEARNING_RESET_COUNT,
        PLANNED_CYCLE_COUNT,
        OBSERVED_BATCH_COUNT,
        OBSERVED_UPDATE_COUNT,
        TOTAL_OBSERVED_BATCH_NANOS,
        MAXIMUM_OBSERVED_BATCH_NANOS,
        LAST_BATCH_UPDATE_COUNT,
        MAXIMUM_BATCH_UPDATE_COUNT,
        LAST_BATCH_NANOS,
        LAST_PLANNED_UPDATE_QUOTA,
        LAST_CLOCK_CHECK_STRIDE,
        FAST_UPDATE_COST_NANOS,
        SLOW_UPDATE_COST_NANOS,
        ESTIMATED_UPDATE_COST_NANOS,
        QUOTA_EXCEEDED_COUNT,
        DEADLINE_OVERRUN_COUNT,
        MAXIMUM_DEADLINE_OVERRUN_NANOS,
        OWNER_SERVICE_COUNT,
        LAST_OWNER_SERVICE_INTERVAL_NANOS,
        MAXIMUM_OWNER_SERVICE_INTERVAL_NANOS,
        PUBLISHED_SOURCE_FRAME_COUNT,
        DROPPED_SOURCE_FRAME_COUNT,
        PUBLISHED_PREPARED_FRAME_COUNT,
        DROPPED_PREPARED_FRAME_COUNT,
        STATUS_PUBLISHED_COUNT,
        STATUS_CONSUMED_COUNT,
        STATUS_PRODUCER_REPLACED_COUNT,
        STATUS_CONSUMER_DISCARDED_COUNT,
        STATUS_PUBLISH_RETRIES_EXHAUSTED
    )

    private const val SCHEMA = 0
    private const val POLICY = 1
    private const val REQUESTED_TARGET_PRESENT = 2
    private const val REQUESTED_TARGET_BITS = 3
    private const val ACTUAL_RATE_SAMPLE_SEQUENCE = 4
    private const val ACTUAL_RATE_VALID = 5
    private const val ACTUAL_COMPLETED_UPS_BITS = 6
    private const val ACTUAL_SIMULATION_HOURS_PER_SECOND_BITS = 7
    private const val ACTUAL_RATE_WINDOW_NANOS = 8
    private const val ACTUAL_RATE_WINDOW_UPDATES = 9
    private const val POLICY_GENERATION = 10
    private const val LEARNING_RESET_COUNT = 11
    private const val PLANNED_CYCLE_COUNT = 12
    private const val OBSERVED_BATCH_COUNT = 13
    private const val OBSERVED_UPDATE_COUNT = 14
    private const val TOTAL_OBSERVED_BATCH_NANOS = 15
    private const val MAXIMUM_OBSERVED_BATCH_NANOS = 16
    private const val LAST_BATCH_UPDATE_COUNT = 17
    private const val MAXIMUM_BATCH_UPDATE_COUNT = 18
    private const val LAST_BATCH_NANOS = 19
    private const val LAST_PLANNED_UPDATE_QUOTA = 20
    private const val LAST_CLOCK_CHECK_STRIDE = 21
    private const val UPDATE_COST_INITIALIZED = 22
    private const val FAST_UPDATE_COST_NANOS = 23
    private const val SLOW_UPDATE_COST_NANOS = 24
    private const val ESTIMATED_UPDATE_COST_NANOS = 25
    private const val QUOTA_EXCEEDED_COUNT = 26
    private const val DEADLINE_OVERRUN_COUNT = 27
    private const val MAXIMUM_DEADLINE_OVERRUN_NANOS = 28
    private const val LAST_STOP_REASON = 29
    private const val OWNER_SERVICE_COUNT = 30
    private const val LAST_OWNER_SERVICE_INTERVAL_NANOS = 31
    private const val MAXIMUM_OWNER_SERVICE_INTERVAL_NANOS = 32
    private const val PUBLISHED_SOURCE_FRAME_COUNT = 33
    private const val DROPPED_SOURCE_FRAME_COUNT = 34
    private const val PUBLISHED_PREPARED_FRAME_COUNT = 35
    private const val DROPPED_PREPARED_FRAME_COUNT = 36
    private const val STATUS_PUBLISHED_COUNT = 37
    private const val STATUS_CONSUMED_COUNT = 38
    private const val STATUS_PRODUCER_REPLACED_COUNT = 39
    private const val STATUS_CONSUMER_DISCARDED_COUNT = 40
    private const val STATUS_PUBLISH_RETRIES_EXHAUSTED = 41
}
