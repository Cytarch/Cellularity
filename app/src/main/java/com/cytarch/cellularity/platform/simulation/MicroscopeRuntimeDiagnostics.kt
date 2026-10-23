package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.shared.model.SimulationStats
import java.util.Locale

/** Latest backend-neutral scheduler and frame-exchange observation for local profiling. */
internal object MicroscopeRuntimeDiagnostics {
    private var latest = "Microscope runtime profile\nschema=1\nstatus=not_available\n"
    private var latestTelemetry: MicroscopeRuntimeTelemetry? = null
    private var latestStats: SimulationStats? = null
    private var profileBaseline: RuntimeProfileBaseline? = null

    @Synchronized
    fun update(
        telemetry: MicroscopeRuntimeTelemetry,
        stats: SimulationStats? = null
    ) {
        latestTelemetry = telemetry
        latestStats = stats
        latest = buildString {
            appendLine("Microscope runtime profile")
            appendLine("schema=1")
            appendLine("status=ready")
            appendLine("rate_policy=${telemetry.ratePolicy.name.lowercase(Locale.ROOT)}")
            appendLine("actual_rate_valid=${telemetry.actualRateValid}")
            appendLine(
                "actual_completed_ups=" +
                    String.format(Locale.ROOT, "%.3f", telemetry.actualCompletedUps)
            )
            appendLine(
                "actual_simulation_hours_per_second=" +
                    String.format(
                        Locale.ROOT,
                        "%.6f",
                        telemetry.actualSimulationHoursPerSecond
                    )
            )
            appendLine("actual_rate_window_ns=${telemetry.actualRateWindowNanos}")
            appendLine("actual_rate_window_updates=${telemetry.actualRateWindowUpdates}")
            appendLine("cell_count=${stats?.cellCount ?: -1}")
            appendLine(
                "food_mass=" +
                    (stats?.let { String.format(Locale.ROOT, "%.6f", it.foodMass) } ?: "nan")
            )
            appendLine(
                "simulation_time=" +
                    (stats?.let { String.format(Locale.ROOT, "%.6f", it.time) } ?: "nan")
            )
            appendLine("simulation_update_count=${stats?.updateCount ?: -1L}")
            appendLine("observed_update_count=${telemetry.observedUpdateCount}")
            appendLine("total_observed_batch_ns=${telemetry.totalObservedBatchNanos}")
            appendLine("maximum_observed_batch_ns=${telemetry.maximumObservedBatchNanos}")
            appendLine("maximum_owner_service_interval_ns=${telemetry.maximumOwnerServiceIntervalNanos}")
            appendLine("published_source_frame_count=${telemetry.publishedSourceFrameCount}")
            appendLine("dropped_source_frame_count=${telemetry.droppedSourceFrameCount}")
            appendLine("published_prepared_frame_count=${telemetry.publishedPreparedFrameCount}")
            appendLine("dropped_prepared_frame_count=${telemetry.droppedPreparedFrameCount}")
            appendLine("status_published_count=${telemetry.statusPublishedCount}")
            appendLine("status_consumed_count=${telemetry.statusConsumedCount}")
            appendLine("status_producer_replaced_count=${telemetry.statusProducerReplacedCount}")
            appendLine("status_consumer_discarded_count=${telemetry.statusConsumerDiscardedCount}")
            appendLine("status_publish_retries_exhausted=${telemetry.statusPublishRetriesExhausted}")
        }
    }

    @Synchronized
    fun startProfile() {
        profileBaseline = RuntimeProfileBaseline(
            telemetry = latestTelemetry,
            stats = latestStats
        )
    }

    @Synchronized
    fun resetProfile() {
        profileBaseline = null
    }

    @Synchronized
    fun snapshot(): String {
        val baseline = profileBaseline ?: return latest
        val telemetry = latestTelemetry ?: return latest
        val stats = latestStats
        return latest + buildString {
            appendLine("Microscope runtime profile window")
            appendLine(
                "profile_observed_update_count=" +
                    delta(
                        telemetry.observedUpdateCount,
                        baseline.telemetry?.observedUpdateCount
                    )
            )
            appendLine(
                "profile_published_source_frame_count=" +
                    delta(
                        telemetry.publishedSourceFrameCount,
                        baseline.telemetry?.publishedSourceFrameCount
                    )
            )
            appendLine(
                "profile_dropped_source_frame_count=" +
                    delta(
                        telemetry.droppedSourceFrameCount,
                        baseline.telemetry?.droppedSourceFrameCount
                    )
            )
            appendLine(
                "profile_published_prepared_frame_count=" +
                    delta(
                        telemetry.publishedPreparedFrameCount,
                        baseline.telemetry?.publishedPreparedFrameCount
                    )
            )
            appendLine(
                "profile_dropped_prepared_frame_count=" +
                    delta(
                        telemetry.droppedPreparedFrameCount,
                        baseline.telemetry?.droppedPreparedFrameCount
                    )
            )
            appendLine(
                "profile_status_producer_replaced_count=" +
                    delta(
                        telemetry.statusProducerReplacedCount,
                        baseline.telemetry?.statusProducerReplacedCount
                    )
            )
            appendLine(
                "profile_status_consumer_discarded_count=" +
                    delta(
                        telemetry.statusConsumerDiscardedCount,
                        baseline.telemetry?.statusConsumerDiscardedCount
                    )
            )
            appendLine(
                "profile_simulation_update_count=" +
                    delta(stats?.updateCount, baseline.stats?.updateCount)
            )
            appendLine(
                "profile_cell_count_change=" +
                    signedDelta(stats?.cellCount, baseline.stats?.cellCount)
            )
        }
    }

    private fun delta(current: Long?, baseline: Long?): Long =
        if (current == null || baseline == null || current < baseline) {
            0L
        } else {
            current - baseline
        }

    private fun signedDelta(current: Int?, baseline: Int?): Int =
        if (current == null || baseline == null) 0 else current - baseline

    private data class RuntimeProfileBaseline(
        val telemetry: MicroscopeRuntimeTelemetry?,
        val stats: SimulationStats?
    )
}
