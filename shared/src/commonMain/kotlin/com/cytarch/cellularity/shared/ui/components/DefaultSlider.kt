package com.cytarch.cellularity.shared.ui.components

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.DpSize
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DefaultSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    commitOnValueChangeFinished: Boolean = false,
    onValueChangeFinished: (() -> Unit)? = null,
    steps: Int = 0,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
) {
    val dimensions = CellularityDimensions.current
    var pendingGestureValue by remember(value) { mutableStateOf<Float?>(null) }
    val activeTrackColor = MaterialTheme.colorScheme.primary
    val inactiveTrackColor = MaterialTheme.colorScheme.background
    Slider(
        value = pendingGestureValue ?: value,
        onValueChange = { newValue ->
            if (commitOnValueChangeFinished) {
                pendingGestureValue = newValue
            } else {
                onValueChange(newValue)
            }
        },
        modifier = modifier,
        enabled = enabled,
        onValueChangeFinished = {
            if (commitOnValueChangeFinished) {
                val committedValue = pendingGestureValue
                pendingGestureValue = null
                committedValue?.let(onValueChange)
            }
            onValueChangeFinished?.invoke()
        },
        steps = steps,
        valueRange = valueRange,
        thumb = {
            SliderDefaults.Thumb(
                interactionSource = remember { MutableInteractionSource() },
                thumbSize = DpSize(dimensions.sliderThumbSize, dimensions.sliderThumbSize)
            )
        },
        track = { sliderState ->
            val fraction = sliderFraction(sliderState.value, sliderState.valueRange)
            Canvas(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(dimensions.sliderTrackHeight)
            ) {
                val strokeWidth = size.height.coerceAtLeast(1f)
                val left = Offset(0f, size.height / 2f)
                val right = Offset(size.width, size.height / 2f)
                val start = if (layoutDirection == LayoutDirection.Ltr) left else right
                val end = if (layoutDirection == LayoutDirection.Ltr) right else left
                val activeEnd = Offset(
                    x = start.x + (end.x - start.x) * fraction,
                    y = start.y
                )
                drawLine(
                    color = inactiveTrackColor,
                    start = start,
                    end = end,
                    strokeWidth = strokeWidth,
                    cap = StrokeCap.Round
                )
                drawLine(
                    color = activeTrackColor,
                    start = start,
                    end = activeEnd,
                    strokeWidth = strokeWidth,
                    cap = StrokeCap.Round
                )
            }
        }
    )
}

internal fun sliderFraction(
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>
): Float {
    val length = valueRange.endInclusive - valueRange.start
    if (!length.isFinite() || length <= 0f || !value.isFinite()) return 0f
    return ((value - valueRange.start) / length).coerceIn(0f, 1f)
}
