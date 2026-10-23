package com.cytarch.cellularity.shared.model

import kotlin.math.sqrt

/** Values shared by native simulation contracts and application UI. */
object CellDataConstants {
    const val BOUNDARY_CONNECTION_INDEX = -1

    const val MASS_PER_AREA = 400.0
    const val MAX_CELL_RADIUS = 0.03
    const val MIN_MASS = MAX_CELL_RADIUS * MAX_CELL_RADIUS * MASS_PER_AREA / 5.0
    const val MAX_MASS = MAX_CELL_RADIUS * MAX_CELL_RADIUS * MASS_PER_AREA
    const val MAX_LIPO_MASS = MAX_MASS * 6.0
    val MIN_CELL_RADIUS: Double = sqrt(MIN_MASS / MASS_PER_AREA)
    const val MAX_AGE = 60.0 * 4.0
    const val MAX_LIPIDS = (MAX_LIPO_MASS - MAX_MASS).toFloat()
    const val MAX_SWIM_FORCE = 1.0f
    const val MAX_SIGNAL_SUBSTANCE_OUTPUT = 20.0f
    const val MAX_SIGNAL_SUBSTANCE_OUTPUT_INVERSE = 1.0f / MAX_SIGNAL_SUBSTANCE_OUTPUT
    const val SIGNAL_SUBSTANCE_FLOW = 5.0f
    const val SIGNAL_SUBSTANCE_DECAY = 5.0f
    const val SIGNAL_SUBSTANCE_INITIAL = 0.01f
    const val SIGNAL_SUBSTANCE_COUNT = 4
    const val AUX_SIGNAL_SUBSTANCE_COUNT = 4
    const val NEUROCYTE_OUTPUT_COUNT = 4
    const val SMELL_TYPE_CELL = 0
    const val SMELL_TYPE_FOOD = 1
    const val SMELL_TYPE_FAT = 2
    const val SMELL_TYPE_LIGHT = 3
    const val SMELL_TYPE_VELOCITY = 4
    const val SMELL_TYPE_WALL = 5
    const val SMELL_TYPE_COUNT = 6
    const val MAX_NEIGHBORS = 20
    /** Historical effective size produced by the original game's import format. */
    const val LEGACY_GENOME_GENE_COUNT = 40

    /** Defensive runtime/format bound. This is not a selectable genome size. */
    const val MAX_GENOME_GENE_COUNT = 256
    const val VALUE_VARIABLE_MAX = 8.0f
    const val MAX_MASS_FLOW_PRIORITY = 10.0f
    const val MIN_MASS_FLOW_PRIORITY = 0.1f
    const val MIN_RATIO = 0.1f
    /** Largest split threshold that a cell capped at [MAX_MASS] can strictly exceed. */
    val MAX_SPLITTABLE_MASS: Float = Float.fromBits(MAX_MASS.toFloat().toBits() - 1)
    /** Canonical sentinel used by saves and native simulation for a gene that never divides. */
    const val MAX_SPLIT_MASS = (MAX_MASS + 0.1 * MIN_MASS).toFloat()
    const val MIN_SPLIT_MASS = (0.9 * MIN_MASS).toFloat()
    const val MUTATE_PERIOD = 15
}

object LinkConstants {
    const val MAX_STIFFNESS = 35.0f
}

/** UI ranges that are part of the native environment contract. */
object EnvironmentConstants {
    const val MAX_LIGHT = 2.0
    const val MAX_LIGHT_RANGE = 1.0
    const val MAX_LIGHT_SPEED = 1.0
    const val MAX_RADIATION = 0.06 * 3.0 / 2.0
    const val VISCOSITY_MAX = 1.0 * 150.0 * CellDataConstants.MAX_CELL_RADIUS / 0.03
    const val MAX_FOOD_SIZE = 4.0 * (CellDataConstants.MAX_MASS - CellDataConstants.MIN_MASS)
    const val MIN_FOOD_SIZE = CellDataConstants.MIN_MASS * 0.3
    const val MAX_FOOD_LUMPINESS = 100.0
    const val MIN_FOOD_LUMP_SIZE = 0.01
    const val MAX_LUMP_SIZE = 0.2
    const val MAX_FOOD_SPEED = 0.15 * 100.0 * 0.03 * 0.03 /
        (CellDataConstants.MAX_CELL_RADIUS * CellDataConstants.MAX_CELL_RADIUS)
    const val MAX_FRICTION = 8.0
    const val MAX_MUTATION_WEIGHT = 25.0

    const val MIN_DENSITY = -3.3
    const val MAX_DENSITY = 3.3
    const val MIN_GRAVITY = 0.0
    const val MAX_GRAVITY = 1.0
    const val MIN_GRADIENT = 0.0
    const val MAX_GRADIENT = -2.0
}

/** Stable limits shared by every platform implementation of the runtime environment API. */
object EnvironmentContract {
    const val SCHEMA_VERSION = 1
    const val MAX_PATCH_CHANGES = 256
    const val GENE_POOL_SIZE = 18
    const val MUTATION_WEIGHT_COUNT = 100
}
