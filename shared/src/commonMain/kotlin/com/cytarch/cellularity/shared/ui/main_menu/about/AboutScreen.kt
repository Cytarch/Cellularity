package com.cytarch.cellularity.shared.ui.main_menu.about

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.about_version_format
import com.cytarch.cellularity.shared.generated.resources.about_version_label
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import org.jetbrains.compose.resources.stringResource

@Composable
fun AboutScreen(
    versionName: String,
    modifier: Modifier = Modifier
) {
    val dimensions = CellularityDimensions.current
    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(dimensions.large)
    ) {
        DefaultCard {
            Text(
                text = stringResource(
                    Res.string.about_version_format,
                    stringResource(Res.string.about_version_label),
                    versionName
                ),
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.padding(dimensions.xxLarge)
            )
        }
    }
}
