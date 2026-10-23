package com.cytarch.cellularity.shared.model

data class SimulationStats(
    val cellCount: Int = 0,
    val foodMass: Double = 0.0,
    val time: Double = 0.0,
    val updateCount: Long = 0
)
