package com.cytarch.cellularity.shared.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import com.cytarch.cellularity.shared.ui.theme.cellularityOutlinedTextFieldColors

internal class SliderGestureState {
    private var changing = false

    fun startIfNeeded(onStarted: (() -> Unit)?) {
        if (changing) return
        changing = true
        onStarted?.invoke()
    }

    fun finishIfNeeded(onFinished: (() -> Unit)?) {
        if (!changing) return
        changing = false
        onFinished?.invoke()
    }
}

data class SliderCardStrings(
    val invalidNumber: String,
    val confirm: String,
    val dismiss: String
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SliderCard(
    label: String,
    currentValue: Double,
    min: Double,
    max: Double,
    maxSteps: Int,
    valueFormatter: (Double) -> String,
    inputValueFormatter: (Double) -> String,
    editIcon: @Composable () -> Unit,
    strings: SliderCardStrings,
    enabled: Boolean = true,
    commitOnValueChangeFinished: Boolean = false,
    onValueChangeStarted: (() -> Unit)? = null,
    onValueChangeFinished: (() -> Unit)? = null,
    onValueChange: (Double) -> Unit,
) {
    val dimensions = CellularityDimensions.current
    var showDialog by remember { mutableStateOf(false) }
    var pendingGestureValue by remember(currentValue) { mutableStateOf<Double?>(null) }
    val gestureState = remember { SliderGestureState() }
    val displayedValue = pendingGestureValue ?: currentValue
    val progress = if (max == min) {
        0f
    } else {
        (((displayedValue - min) / (max - min)) * maxSteps).toFloat()
    }.coerceIn(0f, maxSteps.toFloat())

    DefaultCard {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(dimensions.xLarge)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = label,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = valueFormatter(displayedValue),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.Bold
                )
            }

            Spacer(modifier = Modifier.height(dimensions.small))

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                DefaultSlider(
                    value = progress,
                    onValueChange = { newProgress ->
                        gestureState.startIfNeeded(onValueChangeStarted)
                        val newValue = min + ((newProgress.toDouble() / maxSteps) * (max - min))
                        if (commitOnValueChangeFinished) {
                            pendingGestureValue = newValue
                        } else {
                            onValueChange(newValue)
                        }
                    },
                    onValueChangeFinished = {
                        if (commitOnValueChangeFinished) {
                            val committedValue = pendingGestureValue
                            pendingGestureValue = null
                            committedValue?.let(onValueChange)
                        }
                        gestureState.finishIfNeeded(onValueChangeFinished)
                    },
                    steps = (maxSteps - 1).coerceAtLeast(0),
                    valueRange = 0f..maxSteps.toFloat(),
                    enabled = enabled,
                    modifier = Modifier.weight(1f),
                )
                Spacer(modifier = Modifier.width(dimensions.medium))
                IconButton(
                    onClick = { showDialog = true },
                    modifier = Modifier.size(dimensions.iconButtonSize)
                ) {
                    editIcon()
                }
            }

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = dimensions.xSmall),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = valueFormatter(min),
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.Gray
                )
                Text(
                    text = valueFormatter(max),
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.Gray
                )
            }
        }
    }

    if (showDialog) {
        var inputText by remember {
            mutableStateOf(inputValueFormatter(currentValue))
        }
        val parsedValue = inputText.replace(',', '.').toDoubleOrNull()
        val isValid = parsedValue != null && parsedValue.isFinite() && parsedValue in min..max

        CellularityAlertDialog(
            onDismissRequest = { showDialog = false },
            title = {
                Text(text = label)
            },
            text = {
                Column {
                    OutlinedTextField(
                        value = inputText,
                        onValueChange = { inputText = it },
                        singleLine = true,
                        isError = !isValid,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                        placeholder = { Text(text = "0.0") },
                        modifier = Modifier.fillMaxWidth(),
                        colors = cellularityOutlinedTextFieldColors()
                    )
                    if (!isValid) {
                        Text(
                            text = strings.invalidNumber,
                            color = MaterialTheme.colorScheme.error,
                            style = MaterialTheme.typography.bodySmall,
                            modifier = Modifier.padding(top = dimensions.xSmall)
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        if (!isValid) return@TextButton
                        onValueChange(parsedValue ?: return@TextButton)
                        showDialog = false
                    },
                    enabled = isValid
                ) {
                    Text(strings.confirm)
                }
            },
            dismissButton = {
                TextButton(onClick = { showDialog = false }) {
                    Text(strings.dismiss)
                }
            }
        )
    }
}
