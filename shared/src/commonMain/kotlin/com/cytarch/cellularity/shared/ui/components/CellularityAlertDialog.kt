package com.cytarch.cellularity.shared.ui.components

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ProvideTextStyle
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.window.DialogProperties
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions

/** Applies the application's panel styling to every modal dialog. */
@Composable
fun CellularityAlertDialog(
    onDismissRequest: () -> Unit,
    confirmButton: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    dismissButton: @Composable (() -> Unit)? = null,
    icon: @Composable (() -> Unit)? = null,
    title: @Composable (() -> Unit)? = null,
    text: @Composable (() -> Unit)? = null,
    properties: DialogProperties = DialogProperties()
) {
    val dimensions = CellularityDimensions.current
    AlertDialog(
        onDismissRequest = onDismissRequest,
        confirmButton = confirmButton,
        modifier = modifier,
        dismissButton = dismissButton,
        icon = icon,
        title = title?.let { titleContent ->
            {
                ProvideTextStyle(
                    value = MaterialTheme.typography.titleMedium.copy(
                        fontWeight = FontWeight.Bold
                    ),
                    content = titleContent
                )
            }
        },
        text = text?.let { textContent ->
            {
                ProvideTextStyle(
                    value = MaterialTheme.typography.bodyMedium,
                    content = textContent
                )
            }
        },
        shape = RoundedCornerShape(dimensions.cardCorner),
        containerColor = MaterialTheme.colorScheme.surfaceVariant,
        iconContentColor = MaterialTheme.colorScheme.primary,
        titleContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
        textContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
        tonalElevation = dimensions.cardElevation,
        properties = properties
    )
}
