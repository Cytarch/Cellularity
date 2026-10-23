package com.cytarch.cellularity.shared.ui.play.microscope

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_cancel_action
import com.cytarch.cellularity.shared.generated.resources.microscope_render_preparation_failed_message
import com.cytarch.cellularity.shared.generated.resources.plate
import com.cytarch.cellularity.shared.generated.resources.play_tool_cell_boost
import com.cytarch.cellularity.shared.generated.resources.play_tool_cell_diagnostics
import com.cytarch.cellularity.shared.generated.resources.play_tool_cell_removal
import com.cytarch.cellularity.shared.generated.resources.play_tool_cell_synthesizer
import com.cytarch.cellularity.shared.generated.resources.play_tool_optical_tweezers
import com.cytarch.cellularity.shared.generated.resources.play_tool_selection_title
import com.cytarch.cellularity.shared.generated.resources.temp_high
import com.cytarch.cellularity.shared.generated.resources.temp_low
import com.cytarch.cellularity.shared.generated.resources.temp_medium
import com.cytarch.cellularity.shared.generated.resources.play_temperature_menu_title
import com.cytarch.cellularity.shared.generated.resources.temp_slow
import com.cytarch.cellularity.shared.generated.resources.play_temperature_freeze_action
import com.cytarch.cellularity.shared.generated.resources.play_temperature_incubate_action
import com.cytarch.cellularity.shared.generated.resources.play_temperature_incubate_without_rendering_action
import com.cytarch.cellularity.shared.generated.resources.play_temperature_observe_action
import com.cytarch.cellularity.shared.generated.resources.play_temperature_slow_observe_action
import com.cytarch.cellularity.shared.generated.resources.tool_add
import com.cytarch.cellularity.shared.generated.resources.tool_cell
import com.cytarch.cellularity.shared.generated.resources.tool_info
import com.cytarch.cellularity.shared.generated.resources.tool_move
import com.cytarch.cellularity.shared.generated.resources.tool_remove
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import org.jetbrains.compose.resources.DrawableResource
import org.jetbrains.compose.resources.StringResource
import org.jetbrains.compose.resources.painterResource
import org.jetbrains.compose.resources.stringResource

data class MicroscopeToolItem(
    val tool: MicroscopeTool,
    val icon: DrawableResource,
    val title: StringResource
)

data class MicroscopeTemperatureItem(
    val value: Int,
    val icon: DrawableResource,
    val title: StringResource
)

@Composable
fun MicroscopeScreen(
    state: MicroscopeState,
    isVisible: Boolean,
    modifier: Modifier = Modifier,
    onPlateActionClick: (() -> Unit)? = null,
    onAction: (MicroscopeAction) -> Unit,
    viewportContent: @Composable (MicroscopeState) -> Unit
) {
    if (isVisible && state.dialog == MicroscopeDialog.TOOL_SELECTION) {
        ToolSelectionDialog(
            onDismiss = { onAction(MicroscopeAction.DismissDialog) },
            onSelected = { tool ->
                onAction(MicroscopeAction.SelectTool(tool))
            }
        )
    }

    if (isVisible && state.dialog == MicroscopeDialog.TEMPERATURE_SELECTION) {
        TemperatureSelectionDialog(
            onDismiss = { onAction(MicroscopeAction.DismissDialog) },
            onSelected = { temperature ->
                MicroscopeTemperature.fromNativeMode(temperature)?.let { value ->
                    onAction(MicroscopeAction.SelectTemperature(value))
                }
            }
        )
    }

    Box(modifier = modifier.fillMaxSize()) {
        viewportContent(state)

        if (isVisible) {
            Row(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .background(
                        color = MaterialTheme.colorScheme.surfaceVariant,
                        shape = RoundedCornerShape(
                            topStart = 0.dp,
                            topEnd = 0.dp,
                            bottomEnd = 0.dp,
                            bottomStart = 16.dp
                        )
                ),
                verticalAlignment = Alignment.CenterVertically
            ) {
                if (onPlateActionClick != null) {
                    IconButton(onClick = onPlateActionClick) {
                        Icon(
                            painter = painterResource(Res.drawable.plate),
                            contentDescription = microscopeString(
                                MicroscopeTextKey.MICROSCOPE_PLATE_OPTIONS_CONTENT_DESCRIPTION
                            ),
                            tint = Color.Unspecified
                        )
                    }
                }

                IconButton(onClick = { onAction(MicroscopeAction.ShowToolSelection) }) {
                    Icon(
                        painter = painterResource(toolIcon(state.selectedTool)),
                        contentDescription = stringResource(toolTitle(state.selectedTool)),
                        tint = Color.Unspecified
                    )
                }

                IconButton(onClick = { onAction(MicroscopeAction.ShowTemperatureSelection) }) {
                    Icon(
                        painter = painterResource(
                            temperatureIcon(state.selectedTemperature.nativeMode)
                        ),
                        contentDescription = stringResource(
                            temperatureTitle(state.selectedTemperature.nativeMode)
                        ),
                        tint = Color.Unspecified
                    )
                }
            }
        }

        val zoomText = state.zoomLabel?.let { resolveMicroscopeText(it) }
        if (isVisible && zoomText != null) {
            Text(
                text = zoomText,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.labelMedium,
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(top = 58.dp, end = 8.dp)
                    .background(
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.92f),
                        shape = RoundedCornerShape(8.dp)
                    )
                    .padding(horizontal = 8.dp, vertical = 4.dp)
                    .wrapContentSize()
            )
        }

        if (isVisible && state.renderPreparationFailed) {
            Text(
                text = stringResource(
                    Res.string.microscope_render_preparation_failed_message
                ),
                color = MaterialTheme.colorScheme.onErrorContainer,
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier
                    .align(Alignment.TopCenter)
                    .padding(top = 64.dp, start = 16.dp, end = 16.dp)
                    .background(
                        color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.96f),
                        shape = RoundedCornerShape(8.dp)
                    )
                    .padding(horizontal = 12.dp, vertical = 8.dp)
            )
        }

        if (isVisible && state.isDiagnosisVisible) {
            BoxWithConstraints(
                modifier = Modifier
                    .fillMaxSize()
            ) {
                val bottomBarHeight = 56.dp
                DiagnosisPanel(
                    diagnosis = state.diagnosis,
                    maxPanelHeight = (maxHeight - bottomBarHeight) * 0.46f,
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                        .padding(top = 8.dp, bottom = bottomBarHeight)
                )
            }
        }
    }
}

@Composable
private fun DiagnosisPanel(
    diagnosis: MicroscopeDiagnosis,
    maxPanelHeight: Dp,
    modifier: Modifier = Modifier
) {
    val panelShape = RoundedCornerShape(topStart = 8.dp, topEnd = 8.dp)
    val title = if (diagnosis.title.parts.isEmpty()) {
        MicroscopeText.resource(MicroscopeTextKey.MICROSCOPE_DIAGNOSIS_TITLE)
    } else {
        diagnosis.title
    }
    Column(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(max = maxPanelHeight)
            .clip(panelShape)
            .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.96f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(MaterialTheme.colorScheme.primaryContainer)
                .padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(9.dp)
                    .clip(RoundedCornerShape(50))
                    .background(MaterialTheme.colorScheme.primary)
            )
            Spacer(modifier = Modifier.width(10.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = resolveMicroscopeText(title),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                if (diagnosis.subtitle.parts.isNotEmpty()) {
                    Text(
                        text = resolveMicroscopeText(diagnosis.subtitle),
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.78f),
                        style = MaterialTheme.typography.bodySmall,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
        }

        if (diagnosis.sections.isEmpty()) {
            return@Column
        }

        LazyColumn(
            modifier = Modifier.fillMaxWidth()
        ) {
            items(diagnosis.sections) { section ->
                DiagnosisSection(section)
            }
        }
    }
}

@Composable
private fun DiagnosisSection(section: MicroscopeDiagnosisSection) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Text(
            text = resolveMicroscopeText(section.title),
            color = MaterialTheme.colorScheme.primary,
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.Bold,
            letterSpacing = 0.sp
        )
        section.items.forEach { item ->
            DiagnosisRow(item)
        }
    }
    HorizontalDivider(
        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.14f),
        thickness = 1.dp
    )
}

@Composable
private fun DiagnosisRow(item: MicroscopeDiagnosisItem) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text(
            text = resolveMicroscopeText(item.label),
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.72f),
            style = MaterialTheme.typography.bodySmall,
            lineHeight = 15.sp,
            modifier = Modifier.weight(0.42f),
            maxLines = 2,
            overflow = TextOverflow.Ellipsis
        )
        Text(
            text = resolveMicroscopeText(item.value),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodySmall,
            lineHeight = 15.sp,
            modifier = Modifier.weight(0.58f)
        )
    }
}

@Composable
private fun ToolSelectionDialog(
    onDismiss: () -> Unit,
    onSelected: (MicroscopeTool) -> Unit
) {
    val tools = remember {
        listOf(
            MicroscopeToolItem(MicroscopeTool.CELL, Res.drawable.tool_cell, Res.string.play_tool_cell_synthesizer),
            MicroscopeToolItem(MicroscopeTool.MOVE, Res.drawable.tool_move, Res.string.play_tool_optical_tweezers),
            MicroscopeToolItem(MicroscopeTool.ADD, Res.drawable.tool_add, Res.string.play_tool_cell_boost),
            MicroscopeToolItem(MicroscopeTool.REMOVE, Res.drawable.tool_remove, Res.string.play_tool_cell_removal),
            MicroscopeToolItem(MicroscopeTool.DIAGNOSIS, Res.drawable.tool_info, Res.string.play_tool_cell_diagnostics)
        )
    }

    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(text = stringResource(Res.string.play_tool_selection_title))
        },
        text = {
            LazyColumn {
                items(tools) { tool ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onSelected(tool.tool) }
                            .padding(vertical = 12.dp, horizontal = 8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            painter = painterResource(tool.icon),
                            contentDescription = stringResource(tool.title),
                            tint = Color.Unspecified
                        )
                        Spacer(modifier = Modifier.width(16.dp))
                        Text(
                            text = stringResource(tool.title),
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(Res.string.common_cancel_action))
            }
        }
    )
}

@Composable
fun TemperatureSelectionDialog(
    onDismiss: () -> Unit,
    onSelected: (Int) -> Unit
) {
    val temperatures = remember {
        listOf(
            MicroscopeTemperatureItem(MicroscopeTemperature.FREEZE.nativeMode, Res.drawable.temp_low, Res.string.play_temperature_freeze_action),
            MicroscopeTemperatureItem(MicroscopeTemperature.OBSERVE.nativeMode, Res.drawable.temp_medium, Res.string.play_temperature_observe_action),
            MicroscopeTemperatureItem(MicroscopeTemperature.INCUBATE.nativeMode, Res.drawable.temp_high, Res.string.play_temperature_incubate_action),
            MicroscopeTemperatureItem(MicroscopeTemperature.INCUBATE_NO_RENDER.nativeMode, Res.drawable.temp_high, Res.string.play_temperature_incubate_without_rendering_action),
            MicroscopeTemperatureItem(MicroscopeTemperature.SLOW.nativeMode, Res.drawable.temp_slow, Res.string.play_temperature_slow_observe_action)
        )
    }

    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(text = stringResource(Res.string.play_temperature_menu_title))
        },
        text = {
            LazyColumn {
                items(temperatures) { temperature ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onSelected(temperature.value) }
                            .padding(vertical = 12.dp, horizontal = 8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            painter = painterResource(temperature.icon),
                            contentDescription = stringResource(temperature.title),
                            tint = Color.Unspecified
                        )
                        Spacer(modifier = Modifier.width(16.dp))
                        Text(
                            text = stringResource(temperature.title),
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(Res.string.common_cancel_action))
            }
        }
    )
}

private fun toolIcon(tool: MicroscopeTool): DrawableResource = when (tool) {
    MicroscopeTool.CELL -> Res.drawable.tool_cell
    MicroscopeTool.MOVE -> Res.drawable.tool_move
    MicroscopeTool.ADD -> Res.drawable.tool_add
    MicroscopeTool.REMOVE -> Res.drawable.tool_remove
    MicroscopeTool.DIAGNOSIS -> Res.drawable.tool_info
}

private fun toolTitle(tool: MicroscopeTool): StringResource = when (tool) {
    MicroscopeTool.CELL -> Res.string.play_tool_cell_synthesizer
    MicroscopeTool.MOVE -> Res.string.play_tool_optical_tweezers
    MicroscopeTool.ADD -> Res.string.play_tool_cell_boost
    MicroscopeTool.REMOVE -> Res.string.play_tool_cell_removal
    MicroscopeTool.DIAGNOSIS -> Res.string.play_tool_cell_diagnostics
}

fun temperatureIcon(temperature: Int): DrawableResource = when (temperature) {
    MicroscopeTemperature.OBSERVE.nativeMode -> Res.drawable.temp_medium
    MicroscopeTemperature.INCUBATE.nativeMode -> Res.drawable.temp_high
    MicroscopeTemperature.INCUBATE_NO_RENDER.nativeMode -> Res.drawable.temp_high
    MicroscopeTemperature.SLOW.nativeMode -> Res.drawable.temp_slow
    else -> Res.drawable.temp_low
}

fun temperatureTitle(temperature: Int): StringResource = when (temperature) {
    MicroscopeTemperature.OBSERVE.nativeMode -> Res.string.play_temperature_observe_action
    MicroscopeTemperature.INCUBATE.nativeMode -> Res.string.play_temperature_incubate_action
    MicroscopeTemperature.INCUBATE_NO_RENDER.nativeMode -> Res.string.play_temperature_incubate_without_rendering_action
    MicroscopeTemperature.SLOW.nativeMode -> Res.string.play_temperature_slow_observe_action
    else -> Res.string.play_temperature_freeze_action
}
