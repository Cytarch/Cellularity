package com.cytarch.cellularity.shared.ui.components

import kotlin.test.Test
import kotlin.test.assertEquals

class SliderGestureStateTest {
    @Test
    fun sliderFractionAlwaysRunsFromMinimumToMaximum() {
        assertEquals(0f, sliderFraction(-10f, -10f..30f))
        assertEquals(0.25f, sliderFraction(0f, -10f..30f))
        assertEquals(1f, sliderFraction(30f, -10f..30f))
        assertEquals(0f, sliderFraction(-11f, -10f..30f))
        assertEquals(1f, sliderFraction(31f, -10f..30f))
    }

    @Test
    fun emitsOneStartAndFinishPerGesture() {
        val state = SliderGestureState()
        var starts = 0
        var finishes = 0

        state.startIfNeeded { starts++ }
        state.startIfNeeded { starts++ }
        state.finishIfNeeded { finishes++ }
        state.finishIfNeeded { finishes++ }

        assertEquals(1, starts)
        assertEquals(1, finishes)
    }

    @Test
    fun nextGestureCanStartAfterFinishing() {
        val state = SliderGestureState()
        var starts = 0

        state.startIfNeeded { starts++ }
        state.finishIfNeeded(null)
        state.startIfNeeded { starts++ }

        assertEquals(2, starts)
    }
}
