package com.cytarch.cellularity.shared.model

import kotlin.jvm.JvmOverloads

/** Allocation parameters required before a native simulation world can be created. */
data class SimulationConfiguration @JvmOverloads constructor(
    val maxCellCount: Int,
    val maxFoodCount: Int,
    val radius: Double,
    val innerRadius: Double = 0.0
)

/** Stable limits and tags shared by application code and the native runtime. */
object SimulationContract {
    const val UPDATE_STAGE_CAN_TOUCH = 0

    const val LEVEL_CELL_ORIGIN = 0
    const val USER_CELL_ORIGIN = 1
    const val RANDOM_CELL_ORIGIN = 2
    const val INFECTED_LEVEL_CELL_ORIGIN = 3
    const val INFECTED_USER_CELL_ORIGIN = 4
    const val INFECTED_RANDOM_CELL_ORIGIN = 5
    const val HYBRID_CELL_ORIGIN = 6

    val defaultConfiguration = SimulationConfiguration(
        maxCellCount = 1_000,
        maxFoodCount = 3_500,
        radius = 1.0
    )

    fun isConfigurationValid(configuration: SimulationConfiguration): Boolean =
        isConfigurationValid(
            configuration.maxCellCount,
            configuration.maxFoodCount,
            configuration.radius,
            configuration.innerRadius
        )

    fun isConfigurationValid(
        maxCellCount: Int,
        maxFoodCount: Int,
        radius: Double,
        innerRadius: Double = 0.0
    ): Boolean {
        if (maxCellCount <= 0 || maxFoodCount < 0 || !radius.isFinite() || radius <= 0.0) {
            return false
        }
        if (radius > Int.MAX_VALUE.toDouble() * CellDataConstants.MAX_CELL_RADIUS) {
            return false
        }
        if (!innerRadius.isFinite() || innerRadius < 0.0 ||
            (innerRadius > 0.0 &&
                (innerRadius <= CellDataConstants.MAX_CELL_RADIUS ||
                    radius - innerRadius <= 2.0 * CellDataConstants.MAX_CELL_RADIUS * (1.0 + 1e-6)))
        ) {
            return false
        }
        return maxCellCount <= Int.MAX_VALUE / CellDataConstants.MAX_NEIGHBORS
    }
}
