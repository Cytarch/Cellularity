package com.cytarch.cellularity.shared.ui.play

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_cancel_button_label
import com.cytarch.cellularity.shared.generated.resources.common_discard_action
import com.cytarch.cellularity.shared.generated.resources.common_ok_action
import com.cytarch.cellularity.shared.generated.resources.common_cancel_action
import com.cytarch.cellularity.shared.generated.resources.play_discard_substrate_dialog_message
import com.cytarch.cellularity.shared.generated.resources.play_discard_substrate_dialog_title
import com.cytarch.cellularity.shared.generated.resources.play_reload_substrate_dialog_title
import com.cytarch.cellularity.shared.generated.resources.play_sterilize_substrate_dialog_message
import com.cytarch.cellularity.shared.generated.resources.play_sterilize_substrate_dialog_title
import com.cytarch.cellularity.shared.generated.resources.dna_icon
import com.cytarch.cellularity.shared.generated.resources.freezer_new_plate_action
import com.cytarch.cellularity.shared.generated.resources.microscope_icon
import com.cytarch.cellularity.shared.generated.resources.plate_clear
import com.cytarch.cellularity.shared.generated.resources.plate_load
import com.cytarch.cellularity.shared.generated.resources.plate_save
import com.cytarch.cellularity.shared.generated.resources.play_back_content_description
import com.cytarch.cellularity.shared.generated.resources.play_save_plate_menu_action
import com.cytarch.cellularity.shared.generated.resources.play_tab_genome_editor
import com.cytarch.cellularity.shared.generated.resources.play_tab_microscope
import com.cytarch.cellularity.shared.generated.resources.play_tab_substrate
import com.cytarch.cellularity.shared.generated.resources.play_sample_label_dialog_title
import com.cytarch.cellularity.shared.generated.resources.play_select_action_prompt
import com.cytarch.cellularity.shared.generated.resources.play_reload_plate_action
import com.cytarch.cellularity.shared.generated.resources.play_sterilize_plate_action
import com.cytarch.cellularity.shared.generated.resources.substrate_icon
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import com.cytarch.cellularity.shared.ui.theme.cellularityOutlinedTextFieldColors
import org.jetbrains.compose.resources.painterResource
import org.jetbrains.compose.resources.stringResource

private data class PlateActionItem(
    val type: PlayPlateAction,
    val title: String
)

internal data class PlayContentVisibility(
    val microscope: Boolean,
    val substrate: Boolean,
    val genome: Boolean
)

internal fun playContentVisibility(
    selectedTab: PlayTab,
    genomeContentReady: Boolean
): PlayContentVisibility {
    val genomeRequested = selectedTab == PlayTab.GENOME
    return PlayContentVisibility(
        microscope = selectedTab == PlayTab.MICROSCOPE ||
            (genomeRequested && !genomeContentReady),
        substrate = selectedTab == PlayTab.SUBSTRATE,
        genome = genomeRequested && genomeContentReady
    )
}

@Composable
fun PlayScreen(
    state: PlayState,
    askSterilize: Boolean,
    avoidCameraCutout: Boolean = false,
    onAction: (PlayAction) -> Unit,
    modifier: Modifier = Modifier,
    genomeContentReady: Boolean = true,
    microscopeContent: @Composable (isVisible: Boolean, onPlateActionClick: () -> Unit) -> Unit,
    substrateContent: @Composable (isVisible: Boolean, onPlateActionClick: () -> Unit) -> Unit,
    genomeContent: @Composable () -> Unit
) {
    var retainGenomeContent by remember {
        mutableStateOf(state.selectedTab == PlayTab.GENOME)
    }
    LaunchedEffect(state.selectedTab) {
        if (state.selectedTab == PlayTab.GENOME) retainGenomeContent = true
    }

    when (state.dialog) {
        PlayDialog.PLATE_ACTIONS -> {
            PlateSelectionDialog(
                substrateName = state.substrateName,
                onDismiss = { onAction(PlayAction.DismissDialog) },
                onActionSelected = { actionType ->
                    onAction(
                        PlayAction.SelectPlateAction(
                            action = actionType,
                            confirmationRequired = askSterilize
                        )
                    )
                }
            )
        }
        PlayDialog.SAVE_PLATE -> {
            SavePlateDialog(
                name = state.saveName,
                saving = state.operation == PlayOperation.SAVING,
                onNameChanged = { onAction(PlayAction.SaveNameChanged(it)) },
                onDismiss = { onAction(PlayAction.DismissDialog) },
                onConfirm = { onAction(PlayAction.ConfirmSave) }
            )
        }
        PlayDialog.CONFIRM_RELOAD -> {
            ConfirmActionDialog(
                title = stringResource(Res.string.play_reload_substrate_dialog_title),
                message = stringResource(Res.string.play_sterilize_substrate_dialog_message),
                confirmText = stringResource(Res.string.common_discard_action),
                onDismiss = { onAction(PlayAction.DismissDialog) },
                onConfirm = { onAction(PlayAction.ConfirmReload) }
            )
        }
        PlayDialog.CONFIRM_RESET -> {
            ConfirmActionDialog(
                title = stringResource(Res.string.play_sterilize_substrate_dialog_title),
                message = stringResource(Res.string.play_sterilize_substrate_dialog_message),
                confirmText = stringResource(Res.string.common_discard_action),
                onDismiss = { onAction(PlayAction.DismissDialog) },
                onConfirm = { onAction(PlayAction.ConfirmReset) }
            )
        }
        PlayDialog.CONFIRM_LEAVE -> ConfirmLeaveDialog(
            onConfirm = { onAction(PlayAction.ConfirmLeave) },
            onDismiss = { onAction(PlayAction.DismissDialog) }
        )
        PlayDialog.NONE -> Unit
    }

    Scaffold(
        modifier = modifier.cameraCutoutPadding(avoidCameraCutout),
        bottomBar = {
            PlayBottomBar(
                substrateName = state.substrateName,
                selectedTab = state.selectedTab,
                onBackClick = { onAction(PlayAction.BackRequested) },
                onTabSelected = { onAction(PlayAction.SelectTab(it)) }
            )
        }
    ) { paddingValues ->
        Box(modifier = Modifier.fillMaxSize()) {
            val contentVisibility = playContentVisibility(
                state.selectedTab,
                genomeContentReady
            )
            Box(
                modifier = Modifier.fillMaxSize()
            ) {
                microscopeContent(contentVisibility.microscope) {
                    onAction(PlayAction.RequestPlateActions)
                }
            }

            val isSubstrateVisible = contentVisibility.substrate
            Box(
                modifier = if (isSubstrateVisible) {
                    Modifier
                        .fillMaxSize()
                        .padding(bottom = paddingValues.calculateBottomPadding())
                        .background(MaterialTheme.colorScheme.background)
                } else {
                    Modifier
                        .width(CellularityDimensions.none)
                        .height(CellularityDimensions.none)
                }
            ) {
                substrateContent(isSubstrateVisible) {
                    onAction(PlayAction.RequestPlateActions)
                }
            }

            if (retainGenomeContent) {
                RetainedOffscreenContent(
                    visible = contentVisibility.genome,
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(bottom = paddingValues.calculateBottomPadding())
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .background(MaterialTheme.colorScheme.background)
                    ) {
                        genomeContent()
                    }
                }
            }
        }
    }
}

/**
 * Keeps Android-backed render surfaces attached while this content is not the
 * selected tab. Moving an already presented Surface back on-screen is atomic;
 * disposing and recreating it would expose an empty first frame.
 */
@Composable
private fun RetainedOffscreenContent(
    visible: Boolean,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit
) {
    Layout(
        content = content,
        modifier = modifier.clipToBounds()
    ) { measurables, constraints ->
        val placeable = measurables.single().measure(constraints)
        val width = constraints.maxWidth
        val height = constraints.maxHeight
        layout(width, height) {
            placeable.placeRelative(
                x = if (visible) 0 else width,
                y = 0
            )
        }
    }
}

private fun Modifier.cameraCutoutPadding(enabled: Boolean): Modifier =
    if (enabled) padding(top = CellularityDimensions.cutoutTopPadding) else this

@Composable
private fun PlayBottomBar(
    substrateName: String?,
    selectedTab: PlayTab,
    onBackClick: () -> Unit,
    onTabSelected: (PlayTab) -> Unit
) {
    val tabDescriptions = listOf(
        stringResource(Res.string.play_tab_substrate),
        stringResource(Res.string.play_tab_microscope),
        stringResource(Res.string.play_tab_genome_editor)
    )
    Surface(
        color = MaterialTheme.colorScheme.primaryContainer,
        contentColor = MaterialTheme.colorScheme.primary
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(CellularityDimensions.toolbarHeight),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onBackClick) {
                Icon(
                    imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                    contentDescription = stringResource(
                        Res.string.play_back_content_description
                    )
                )
            }

            Text(
                text = substrateName ?: stringResource(Res.string.freezer_new_plate_action),
                fontWeight = FontWeight.SemiBold,
                style = MaterialTheme.typography.bodyLarge,
                modifier = Modifier
                    .weight(1f)
                    .padding(horizontal = CellularityDimensions.medium),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )

            listOf(
                painterResource(Res.drawable.substrate_icon),
                painterResource(Res.drawable.microscope_icon),
                painterResource(Res.drawable.dna_icon)
            ).forEachIndexed { index, icon ->
                val tab = PlayTab.entries[index]
                val isSelected = selectedTab == tab
                Box(
                    modifier = Modifier
                        .fillMaxHeight()
                        .aspectRatio(1f)
                        .padding(CellularityDimensions.medium)
                        .clip(RoundedCornerShape(CellularityDimensions.tabCorner))
                        .background(
                            if (isSelected) MaterialTheme.colorScheme.background else Color.Transparent
                        )
                        .clickable { onTabSelected(tab) },
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        painter = icon,
                        contentDescription = tabDescriptions[index],
                        tint = if (isSelected) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.background
                        }
                    )
                }
            }
        }
    }
}

@Composable
private fun PlateSelectionDialog(
    substrateName: String?,
    onDismiss: () -> Unit,
    onActionSelected: (PlayPlateAction) -> Unit
) {
    val saveText = stringResource(Res.string.play_save_plate_menu_action)
    val reloadText = stringResource(Res.string.play_reload_plate_action)
    val sterilizeText = stringResource(Res.string.play_sterilize_plate_action)
    val actions = remember(substrateName, saveText, reloadText, sterilizeText) {
        buildList {
            add(PlateActionItem(PlayPlateAction.SAVE, saveText))
            if (substrateName != null) {
                add(PlateActionItem(PlayPlateAction.RELOAD, reloadText))
            }
            add(PlateActionItem(PlayPlateAction.STERILIZE, sterilizeText))
        }
    }
    val icons = mapOf(
        PlayPlateAction.SAVE to painterResource(Res.drawable.plate_save),
        PlayPlateAction.RELOAD to painterResource(Res.drawable.plate_load),
        PlayPlateAction.STERILIZE to painterResource(Res.drawable.plate_clear)
    )

    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(text = stringResource(Res.string.play_select_action_prompt))
        },
        text = {
            LazyColumn {
                items(actions) { action ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onActionSelected(action.type) }
                            .padding(vertical = CellularityDimensions.xLarge, horizontal = CellularityDimensions.medium),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            painter = icons.getValue(action.type),
                            contentDescription = action.title,
                            tint = Color.Unspecified
                        )
                        Spacer(modifier = Modifier.width(CellularityDimensions.xxxLarge))
                        Text(
                            text = action.title,
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
private fun SavePlateDialog(
    name: String,
    saving: Boolean,
    onNameChanged: (String) -> Unit,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit
) {
    CellularityAlertDialog(
        onDismissRequest = { if (!saving) onDismiss() },
        title = {
            Text(text = stringResource(Res.string.play_sample_label_dialog_title))
        },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = onNameChanged,
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
                colors = cellularityOutlinedTextFieldColors()
            )
        },
        confirmButton = {
            TextButton(
                enabled = !saving,
                onClick = onConfirm
            ) {
                Text(text = stringResource(Res.string.common_ok_action))
            }
        },
        dismissButton = {
            TextButton(enabled = !saving, onClick = onDismiss) {
                Text(text = stringResource(Res.string.common_cancel_action))
            }
        }
    )
}

@Composable
private fun ConfirmActionDialog(
    title: String,
    message: String,
    confirmText: String,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit
) {
    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(text = title)
        },
        text = {
            Text(
                text = message,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        },
        confirmButton = {
            TextButton(onClick = onConfirm) {
                Text(text = confirmText)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(Res.string.common_cancel_button_label))
            }
        }
    )
}

@Composable
private fun ConfirmLeaveDialog(
    onConfirm: () -> Unit,
    onDismiss: () -> Unit
) {
    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(text = stringResource(Res.string.play_discard_substrate_dialog_title))
        },
        text = {
            Text(text = stringResource(Res.string.play_discard_substrate_dialog_message))
        },
        confirmButton = {
            TextButton(onClick = onConfirm) {
                Text(text = stringResource(Res.string.common_discard_action))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(Res.string.common_cancel_button_label))
            }
        }
    )
}
