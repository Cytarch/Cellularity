package com.cytarch.cellularity.shared.ui.components

import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions

@Composable
fun DefaultCard(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    val dimensions = CellularityDimensions.current
    Card(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = dimensions.xSmall),
        shape = RoundedCornerShape(dimensions.cardCorner),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
            contentColor = MaterialTheme.colorScheme.onSurfaceVariant
        ),
        elevation = CardDefaults.cardElevation(defaultElevation = dimensions.cardElevation),
        content = content
    )
}
