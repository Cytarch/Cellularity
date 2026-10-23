package com.cytarch.cellularity.shared.ui.main_menu.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
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
import androidx.compose.ui.text.input.KeyboardType
import com.cytarch.cellularity.shared.ui.text.formatLocalizedNumber
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_export_action
import com.cytarch.cellularity.shared.generated.resources.common_ok_action
import com.cytarch.cellularity.shared.generated.resources.common_cancel_action
import com.cytarch.cellularity.shared.generated.resources.settings_language_names
import com.cytarch.cellularity.shared.generated.resources.settings_confirm_before_sterilize_or_reload
import com.cytarch.cellularity.shared.generated.resources.settings_keep_screen_awake_description
import com.cytarch.cellularity.shared.generated.resources.settings_language
import com.cytarch.cellularity.shared.generated.resources.settings_general_section_title
import com.cytarch.cellularity.shared.generated.resources.settings_keep_screen_awake
import com.cytarch.cellularity.shared.generated.resources.settings_crash_report_details_format
import com.cytarch.cellularity.shared.generated.resources.settings_crash_reports
import com.cytarch.cellularity.shared.generated.resources.settings_crash_reports_privacy_description
import com.cytarch.cellularity.shared.generated.resources.settings_dimension_unit_dp
import com.cytarch.cellularity.shared.generated.resources.settings_allowed_range_prompt
import com.cytarch.cellularity.shared.generated.resources.settings_export_diagnostics_action
import com.cytarch.cellularity.shared.generated.resources.settings_export_diagnostics_disclosure
import com.cytarch.cellularity.shared.generated.resources.settings_export_diagnostics_title
import com.cytarch.cellularity.shared.generated.resources.settings_experimental_section_title
import com.cytarch.cellularity.shared.generated.resources.settings_genome_preview_maximum_cell_count
import com.cytarch.cellularity.shared.generated.resources.settings_incubation_target_updates_per_second
import com.cytarch.cellularity.shared.generated.resources.settings_no_crash_reports_message
import com.cytarch.cellularity.shared.generated.resources.settings_performance_hud
import com.cytarch.cellularity.shared.generated.resources.settings_performance_hud_description
import com.cytarch.cellularity.shared.generated.resources.settings_stored_crash_report_count
import com.cytarch.cellularity.shared.generated.resources.settings_theme
import com.cytarch.cellularity.shared.generated.resources.settings_theme_background
import com.cytarch.cellularity.shared.generated.resources.settings_theme_card_corner_radius
import com.cytarch.cellularity.shared.generated.resources.settings_theme_card_elevation
import com.cytarch.cellularity.shared.generated.resources.settings_theme_components
import com.cytarch.cellularity.shared.generated.resources.settings_theme_content_padding
import com.cytarch.cellularity.shared.generated.resources.settings_theme_default
import com.cytarch.cellularity.shared.generated.resources.settings_theme_editor_title
import com.cytarch.cellularity.shared.generated.resources.settings_theme_icon_button_size
import com.cytarch.cellularity.shared.generated.resources.settings_theme_import_action
import com.cytarch.cellularity.shared.generated.resources.settings_theme_item_spacing
import com.cytarch.cellularity.shared.generated.resources.settings_theme_list_padding
import com.cytarch.cellularity.shared.generated.resources.settings_theme_name
import com.cytarch.cellularity.shared.generated.resources.settings_theme_primary
import com.cytarch.cellularity.shared.generated.resources.settings_theme_primary_container
import com.cytarch.cellularity.shared.generated.resources.settings_theme_secondary
import com.cytarch.cellularity.shared.generated.resources.settings_theme_share_action
import com.cytarch.cellularity.shared.generated.resources.settings_theme_slider_thumb_size
import com.cytarch.cellularity.shared.generated.resources.settings_theme_slider_track_height
import com.cytarch.cellularity.shared.generated.resources.settings_theme_surface_variant
import com.cytarch.cellularity.shared.generated.resources.settings_theme_swatch_size
import com.cytarch.cellularity.shared.generated.resources.settings_theme_text
import com.cytarch.cellularity.shared.generated.resources.settings_theme_text_field_text
import com.cytarch.cellularity.shared.settings.CellularityLanguage
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.CellularityThemeComponents
import com.cytarch.cellularity.shared.settings.CellularityThemeColors
import com.cytarch.cellularity.shared.settings.normalizeHexColor
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.components.DefaultLabeledRadioButton
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import com.cytarch.cellularity.shared.ui.theme.cellularityOutlinedTextFieldColors
import org.jetbrains.compose.resources.stringArrayResource
import org.jetbrains.compose.resources.pluralStringResource
import org.jetbrains.compose.resources.stringResource

data class SettingsOption(val value: String, val label: String)

data class CrashReportItem(
    val fileName: String,
    val dateTime: String,
    val sizeText: String
)

@Composable
fun SettingsScreen(
    state: CellularitySettings,
    onAction: (SettingsAction) -> Unit,
    crashReports: List<CrashReportItem> = emptyList(),
    onExportDiagnostics: () -> Unit = {},
    onExportTheme: () -> Unit = {},
    onImportTheme: () -> Unit = {},
    additionalGeneralContent: @Composable () -> Unit = {},
    additionalExperimentalContent: (@Composable () -> Unit)? = null,
    modifier: Modifier = Modifier
) {
    val dimensions = CellularityDimensions.current
    val languageNames = stringArrayResource(Res.array.settings_language_names)
    val languageOptions = CellularityLanguage.entries.mapIndexed { index, language ->
        SettingsOption(
            value = language.languageTag,
            label = languageNames.getOrElse(index) { language.languageTag }
        )
    }
    val ok = stringResource(Res.string.common_ok_action)
    val cancel = stringResource(Res.string.common_cancel_action)

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = dimensions.large, vertical = dimensions.small)
    ) {
        PreferenceCategory(title = stringResource(Res.string.settings_general_section_title))

        EditTextPreference(
            title = stringResource(Res.string.settings_genome_preview_maximum_cell_count),
            value = state.genomePreviewMaxCells.toString(),
            ok = ok,
            cancel = cancel,
            invalidValueMessage = stringResource(
                Res.string.settings_allowed_range_prompt,
                CellularitySettings.MinGenomePreviewMaxCells.toString(),
                CellularitySettings.MaxGenomePreviewMaxCells.toString()
            ),
            isValueValid = { value ->
                value.toIntOrNull()?.let {
                    it in CellularitySettings.MinGenomePreviewMaxCells..CellularitySettings.MaxGenomePreviewMaxCells
                } == true
            },
            onValueChange = { value ->
                onAction(SettingsAction.SetGenomePreviewMaxCells(value.toInt()))
            }
        )

        EditTextPreference(
            title = stringResource(Res.string.settings_incubation_target_updates_per_second),
            value = formatSettingsDouble(state.incubateTargetUps),
            ok = ok,
            cancel = cancel,
            keyboardType = KeyboardType.Decimal,
            invalidValueMessage = stringResource(
                Res.string.settings_allowed_range_prompt,
                formatSettingsDouble(CellularitySettings.MinIncubateTargetUps),
                formatSettingsDouble(CellularitySettings.MaxIncubateTargetUps)
            ),
            isValueValid = { value ->
                parseSettingsDouble(value)?.let {
                    it in CellularitySettings.MinIncubateTargetUps..CellularitySettings.MaxIncubateTargetUps
                } == true
            },
            onValueChange = { value ->
                onAction(
                    SettingsAction.SetIncubateTargetUps(
                        requireNotNull(parseSettingsDouble(value))
                    )
                )
            }
        )

        SettingsSwitchPreference(
            title = stringResource(Res.string.settings_performance_hud),
            summary = stringResource(Res.string.settings_performance_hud_description),
            checked = state.performanceHudEnabled,
            onCheckedChange = { enabled ->
                onAction(SettingsAction.SetPerformanceHudEnabled(enabled))
            }
        )

        SettingsListPreference(
            title = stringResource(Res.string.settings_language),
            currentValue = state.language.languageTag,
            options = languageOptions,
            cancel = cancel,
            onValueChange = { value ->
                val language = CellularityLanguage.fromLanguageTag(value) ?: state.language
                onAction(SettingsAction.SetLanguage(language))
            }
        )

        SettingsSwitchPreference(
            title = stringResource(Res.string.settings_keep_screen_awake),
            summary = stringResource(Res.string.settings_keep_screen_awake_description),
            checked = state.keepOn,
            onCheckedChange = { keepOn ->
                onAction(SettingsAction.SetKeepOn(keepOn))
            }
        )

        SettingsSwitchPreference(
            title = stringResource(Res.string.settings_confirm_before_sterilize_or_reload),
            checked = state.askSterilize,
            onCheckedChange = { askSterilize ->
                onAction(SettingsAction.SetAskSterilize(askSterilize))
            }
        )

        additionalGeneralContent()

        additionalExperimentalContent?.let { experimentalContent ->
            PreferenceCategory(
                title = stringResource(Res.string.settings_experimental_section_title)
            )
            experimentalContent()
        }

        PreferenceCategory(title = stringResource(Res.string.settings_theme))

        ThemeEditorPreference(
            theme = state.theme,
            components = state.themeComponents,
            ok = ok,
            cancel = cancel,
            onThemePresetChange = { colors, components ->
                onAction(SettingsAction.SetTheme(colors, components))
            },
            onExportTheme = onExportTheme,
            onImportTheme = onImportTheme
        )

        PreferenceCategory(title = stringResource(Res.string.settings_crash_reports))

        if (crashReports.isEmpty()) {
            PreferenceCard {
                ListItem(
                    headlineContent = { Text(stringResource(Res.string.settings_no_crash_reports_message)) },
                    supportingContent = {
                        Text(stringResource(Res.string.settings_crash_reports_privacy_description))
                    },
                    colors = preferenceListItemColors()
                )
            }
        } else {
            crashReports.forEach { report ->
                CrashReportPreference(report = report)
            }
            DiagnosticsExportPreference(
                reportCount = crashReports.size,
                onExportDiagnostics = onExportDiagnostics
            )
        }
    }
}

@Composable
private fun ThemeEditorPreference(
    theme: CellularityThemeColors,
    components: CellularityThemeComponents,
    ok: String,
    cancel: String,
    onThemePresetChange: (CellularityThemeColors, CellularityThemeComponents) -> Unit,
    onExportTheme: () -> Unit,
    onImportTheme: () -> Unit
) {
    val dimensions = CellularityDimensions.current
    var showDialog by remember { mutableStateOf(false) }

    PreferenceCard(onClick = { showDialog = true }) {
        ListItem(
            headlineContent = { Text(stringResource(Res.string.settings_theme_editor_title)) },
            supportingContent = { Text(theme.name) },
            trailingContent = {
                Row(horizontalArrangement = Arrangement.spacedBy(dimensions.small)) {
                    ThemeSwatch(theme.primary)
                    ThemeSwatch(theme.background)
                    ThemeSwatch(theme.surfaceVariant)
                }
            },
            colors = preferenceListItemColors()
        )
    }

    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(dimensions.medium)
    ) {
        TextButton(onClick = onImportTheme) {
            Text(stringResource(Res.string.settings_theme_import_action))
        }
        TextButton(onClick = onExportTheme) {
            Text(stringResource(Res.string.settings_theme_share_action))
        }
    }

    if (showDialog) {
        var name by remember(theme, components) { mutableStateOf(theme.name) }
        var primary by remember(theme, components) { mutableStateOf(theme.primary) }
        var secondary by remember(theme, components) { mutableStateOf(theme.secondary) }
        var primaryContainer by remember(theme, components) { mutableStateOf(theme.primaryContainer) }
        var background by remember(theme, components) { mutableStateOf(theme.background) }
        var surfaceVariant by remember(theme, components) { mutableStateOf(theme.surfaceVariant) }
        var onSurfaceVariant by remember(theme, components) { mutableStateOf(theme.onSurfaceVariant) }
        var textFieldText by remember(theme, components) {
            mutableStateOf(theme.textFieldText)
        }
        var cardCorner by remember(theme, components) { mutableStateOf(formatComponentValue(components.cardCorner)) }
        var cardElevation by remember(theme, components) { mutableStateOf(formatComponentValue(components.cardElevation)) }
        var itemSpacing by remember(theme, components) { mutableStateOf(formatComponentValue(components.itemSpacing)) }
        var contentPadding by remember(theme, components) { mutableStateOf(formatComponentValue(components.contentPadding)) }
        var listPadding by remember(theme, components) { mutableStateOf(formatComponentValue(components.listPadding)) }
        var iconButtonSize by remember(theme, components) { mutableStateOf(formatComponentValue(components.iconButtonSize)) }
        var sliderThumbSize by remember(theme, components) { mutableStateOf(formatComponentValue(components.sliderThumbSize)) }
        var sliderTrackHeight by remember(theme, components) { mutableStateOf(formatComponentValue(components.sliderTrackHeight)) }
        var swatchSize by remember(theme, components) { mutableStateOf(formatComponentValue(components.swatchSize)) }

        CellularityAlertDialog(
            onDismissRequest = { showDialog = false },
            title = { Text(stringResource(Res.string.settings_theme_editor_title)) },
            text = {
                Column(
                    modifier = Modifier.verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(dimensions.medium)
                ) {
                    OutlinedTextField(
                        value = name,
                        onValueChange = { name = it },
                        label = { Text(stringResource(Res.string.settings_theme_name)) },
                        singleLine = true,
                        colors = cellularityOutlinedTextFieldColors()
                    )
                    ThemeColorField(stringResource(Res.string.settings_theme_primary), primary, CellularityThemeColors.Default.primary) { primary = it }
                    ThemeColorField(stringResource(Res.string.settings_theme_secondary), secondary, CellularityThemeColors.Default.secondary) { secondary = it }
                    ThemeColorField(stringResource(Res.string.settings_theme_primary_container), primaryContainer, CellularityThemeColors.Default.primaryContainer) { primaryContainer = it }
                    ThemeColorField(stringResource(Res.string.settings_theme_background), background, CellularityThemeColors.Default.background) { background = it }
                    ThemeColorField(stringResource(Res.string.settings_theme_surface_variant), surfaceVariant, CellularityThemeColors.Default.surfaceVariant) { surfaceVariant = it }
                    ThemeColorField(stringResource(Res.string.settings_theme_text), onSurfaceVariant, CellularityThemeColors.Default.onSurfaceVariant) { onSurfaceVariant = it }
                    ThemeColorField(
                        stringResource(Res.string.settings_theme_text_field_text),
                        textFieldText,
                        CellularityThemeColors.Default.textFieldText
                    ) { textFieldText = it }
                    Text(
                        text = stringResource(Res.string.settings_theme_components),
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.primary
                    )
                    ComponentNumberField(stringResource(Res.string.settings_theme_card_corner_radius), cardCorner) { cardCorner = it }
                    ComponentNumberField(stringResource(Res.string.settings_theme_card_elevation), cardElevation) { cardElevation = it }
                    ComponentNumberField(stringResource(Res.string.settings_theme_item_spacing), itemSpacing) { itemSpacing = it }
                    ComponentNumberField(stringResource(Res.string.settings_theme_content_padding), contentPadding) { contentPadding = it }
                    ComponentNumberField(stringResource(Res.string.settings_theme_list_padding), listPadding) { listPadding = it }
                    ComponentNumberField(stringResource(Res.string.settings_theme_icon_button_size), iconButtonSize) { iconButtonSize = it }
                    ComponentNumberField(stringResource(Res.string.settings_theme_slider_thumb_size), sliderThumbSize) { sliderThumbSize = it }
                    ComponentNumberField(stringResource(Res.string.settings_theme_slider_track_height), sliderTrackHeight) { sliderTrackHeight = it }
                    ComponentNumberField(stringResource(Res.string.settings_theme_swatch_size), swatchSize) { swatchSize = it }
                }
            },
            confirmButton = {
                TextButton(onClick = {
                    onThemePresetChange(
                        CellularityThemeColors(
                            name = name.ifBlank { CellularityThemeColors.Default.name },
                            primary = normalizeHexColor(primary, CellularityThemeColors.Default.primary),
                            secondary = normalizeHexColor(secondary, CellularityThemeColors.Default.secondary),
                            primaryContainer = normalizeHexColor(primaryContainer, CellularityThemeColors.Default.primaryContainer),
                            background = normalizeHexColor(background, CellularityThemeColors.Default.background),
                            surfaceVariant = normalizeHexColor(surfaceVariant, CellularityThemeColors.Default.surfaceVariant),
                            onSurfaceVariant = normalizeHexColor(onSurfaceVariant, CellularityThemeColors.Default.onSurfaceVariant),
                            textFieldText = normalizeHexColor(
                                textFieldText,
                                CellularityThemeColors.Default.textFieldText
                            )
                        ),
                        CellularityThemeComponents(
                            cardCorner = parseComponentValue(cardCorner, CellularityThemeComponents.Default.cardCorner),
                            cardElevation = parseComponentValue(cardElevation, CellularityThemeComponents.Default.cardElevation),
                            itemSpacing = parseComponentValue(itemSpacing, CellularityThemeComponents.Default.itemSpacing),
                            contentPadding = parseComponentValue(contentPadding, CellularityThemeComponents.Default.contentPadding),
                            listPadding = parseComponentValue(listPadding, CellularityThemeComponents.Default.listPadding),
                            iconButtonSize = parseComponentValue(iconButtonSize, CellularityThemeComponents.Default.iconButtonSize),
                            sliderThumbSize = parseComponentValue(sliderThumbSize, CellularityThemeComponents.Default.sliderThumbSize),
                            sliderTrackHeight = parseComponentValue(sliderTrackHeight, CellularityThemeComponents.Default.sliderTrackHeight),
                            swatchSize = parseComponentValue(swatchSize, CellularityThemeComponents.Default.swatchSize)
                        ).normalized()
                    )
                    showDialog = false
                }) {
                    Text(ok)
                }
            },
            dismissButton = {
                TextButton(onClick = {
                    onThemePresetChange(CellularityThemeColors.Default, CellularityThemeComponents.Default)
                    showDialog = false
                }) {
                    Text(stringResource(Res.string.settings_theme_default))
                }
                TextButton(onClick = { showDialog = false }) {
                    Text(cancel)
                }
            }
        )
    }
}

@Composable
private fun ThemeColorField(
    title: String,
    value: String,
    fallback: String,
    onValueChange: (String) -> Unit
) {
    val dimensions = CellularityDimensions.current
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(dimensions.large)
    ) {
        ThemeSwatch(normalizeHexColor(value, fallback))
        OutlinedTextField(
            value = value,
            onValueChange = onValueChange,
            label = { Text(title) },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Text),
            singleLine = true,
            modifier = Modifier.weight(1f),
            colors = cellularityOutlinedTextFieldColors()
        )
    }
}

@Composable
private fun ComponentNumberField(
    title: String,
    value: String,
    onValueChange: (String) -> Unit
) {
    OutlinedTextField(
        value = value,
        onValueChange = onValueChange,
        label = { Text(title) },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        singleLine = true,
        suffix = { Text(stringResource(Res.string.settings_dimension_unit_dp)) },
        modifier = Modifier.fillMaxWidth(),
        colors = cellularityOutlinedTextFieldColors()
    )
}

@Composable
private fun ThemeSwatch(value: String) {
    val dimensions = CellularityDimensions.current
    Box(
        modifier = Modifier
            .size(dimensions.swatchSize)
            .background(parseColor(value))
            .border(
                dimensions.hairline,
                MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.35f),
                RoundedCornerShape(dimensions.smallCorner)
            )
    )
}

private fun formatComponentValue(value: Float): String {
    return if (value % 1f == 0f) {
        value.toInt().toString()
    } else {
        value.toString()
    }
}

private fun parseComponentValue(value: String, fallback: Float): Float {
    return value.replace(',', '.').toFloatOrNull() ?: fallback
}

private fun parseColor(value: String): Color {
    val rgb = value.trim().removePrefix("#").toLongOrNull(16) ?: return Color.Transparent
    return if (rgb <= 0xFFFFFF) Color(0xFF000000 or rgb) else Color(rgb)
}

@Composable
private fun PreferenceCategory(title: String) {
    val dimensions = CellularityDimensions.current
    Text(
        text = title,
        style = MaterialTheme.typography.labelLarge,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(
            start = dimensions.small,
            top = dimensions.sectionTop,
            bottom = dimensions.medium
        )
    )
}

@Composable
private fun PreferenceCard(
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
    content: @Composable () -> Unit
) {
    val dimensions = CellularityDimensions.current
    Card(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = dimensions.xSmall)
            .then(if (onClick != null) Modifier.clickable { onClick() } else Modifier),
        shape = RoundedCornerShape(dimensions.cardCorner),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
            contentColor = MaterialTheme.colorScheme.onSurfaceVariant
        ),
        elevation = CardDefaults.cardElevation(defaultElevation = dimensions.cardElevation)
    ) {
        content()
    }
}

@Composable
private fun preferenceListItemColors() = ListItemDefaults.colors(
    containerColor = MaterialTheme.colorScheme.surfaceVariant,
    headlineColor = MaterialTheme.colorScheme.onSurfaceVariant,
    supportingColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.72f)
)

@Composable
fun SettingsSwitchPreference(
    title: String,
    summary: String? = null,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    PreferenceCard(onClick = { onCheckedChange(!checked) }) {
        ListItem(
            headlineContent = { Text(title) },
            supportingContent = summary?.let { { Text(it) } },
            trailingContent = {
                Switch(checked = checked, onCheckedChange = null)
            },
            colors = preferenceListItemColors()
        )
    }
}

@Composable
fun SettingsInfoPreference(
    title: String,
    summary: String,
    onClick: (() -> Unit)? = null
) {
    PreferenceCard(onClick = onClick) {
        ListItem(
            headlineContent = { Text(title) },
            supportingContent = { Text(summary) },
            colors = preferenceListItemColors()
        )
    }
}

@Composable
private fun CrashReportPreference(
    report: CrashReportItem
) {
    PreferenceCard {
        ListItem(
            headlineContent = { Text(report.dateTime) },
            supportingContent = {
                Text(
                    stringResource(
                        Res.string.settings_crash_report_details_format,
                        report.fileName,
                        report.sizeText
                    )
                )
            },
            colors = preferenceListItemColors()
        )
    }
}

@Composable
private fun DiagnosticsExportPreference(
    reportCount: Int,
    onExportDiagnostics: () -> Unit
) {
    var showDisclosure by remember { mutableStateOf(false) }

    PreferenceCard(onClick = { showDisclosure = true }) {
        ListItem(
            headlineContent = { Text(stringResource(Res.string.settings_export_diagnostics_action)) },
            supportingContent = {
                Text(
                    pluralStringResource(
                        Res.plurals.settings_stored_crash_report_count,
                        reportCount,
                        reportCount
                    )
                )
            },
            colors = preferenceListItemColors()
        )
    }

    if (showDisclosure) {
        CellularityAlertDialog(
            onDismissRequest = { showDisclosure = false },
            title = { Text(stringResource(Res.string.settings_export_diagnostics_title)) },
            text = {
                Text(
                    text = stringResource(Res.string.settings_export_diagnostics_disclosure),
                    modifier = Modifier.verticalScroll(rememberScrollState())
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    showDisclosure = false
                    onExportDiagnostics()
                }) {
                    Text(stringResource(Res.string.common_export_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { showDisclosure = false }) {
                    Text(stringResource(Res.string.common_cancel_action))
                }
            }
        )
    }
}

@Composable
fun SettingsListPreference(
    title: String,
    currentValue: String,
    options: List<SettingsOption>,
    cancel: String,
    onValueChange: (String) -> Unit
) {
    val dimensions = CellularityDimensions.current
    val currentEntryLabel = options.firstOrNull { it.value == currentValue }?.label ?: currentValue
    var showDialog by remember { mutableStateOf(false) }

    PreferenceCard(onClick = { showDialog = true }) {
        ListItem(
            headlineContent = { Text(title) },
            supportingContent = { Text(currentEntryLabel) },
            colors = preferenceListItemColors()
        )
    }

    if (showDialog) {
        CellularityAlertDialog(
            onDismissRequest = { showDialog = false },
            title = { Text(title) },
            text = {
                Column(
                    Modifier
                        .selectableGroup()
                        .verticalScroll(rememberScrollState())
                ) {
                    options.forEach { option ->
                        DefaultLabeledRadioButton(
                            label = option.label,
                            selected = option.value == currentValue,
                            modifier = Modifier
                                .height(dimensions.toolbarHeight)
                                .padding(horizontal = dimensions.xxxLarge),
                            onClick = {
                                onValueChange(option.value)
                                showDialog = false
                            }
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { showDialog = false }) {
                    Text(cancel)
                }
            }
        )
    }
}

@Composable
private fun EditTextPreference(
    title: String,
    value: String,
    ok: String,
    cancel: String,
    keyboardType: KeyboardType = KeyboardType.Number,
    invalidValueMessage: String,
    isValueValid: (String) -> Boolean,
    onValueChange: (String) -> Unit
) {
    var showDialog by remember { mutableStateOf(false) }
    var tempValue by remember(value) { mutableStateOf(value) }
    var validationFailed by remember(value) { mutableStateOf(false) }

    PreferenceCard(
        onClick = {
            tempValue = value
            validationFailed = false
            showDialog = true
        }
    ) {
        ListItem(
            headlineContent = { Text(title) },
            supportingContent = { Text(value) },
            colors = preferenceListItemColors()
        )
    }

    if (showDialog) {
        CellularityAlertDialog(
            onDismissRequest = { showDialog = false },
            title = { Text(title) },
            text = {
                OutlinedTextField(
                    value = tempValue,
                    onValueChange = {
                        tempValue = it
                        validationFailed = false
                    },
                    keyboardOptions = KeyboardOptions(keyboardType = keyboardType),
                    singleLine = true,
                    isError = validationFailed,
                    supportingText = if (validationFailed) {
                        { Text(invalidValueMessage) }
                    } else {
                        null
                    },
                    colors = cellularityOutlinedTextFieldColors()
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    if (isValueValid(tempValue)) {
                        onValueChange(tempValue)
                        showDialog = false
                    } else {
                        validationFailed = true
                    }
                }) {
                    Text(ok)
                }
            },
            dismissButton = {
                TextButton(onClick = { showDialog = false }) {
                    Text(cancel)
                }
            }
        )
    }
}

private fun parseSettingsDouble(value: String): Double? {
    return value.trim().replace(',', '.').toDoubleOrNull()?.takeIf { it.isFinite() }
}

private fun formatSettingsDouble(value: Double): String {
    return formatLocalizedNumber(value, minimumFractionDigits = 0, maximumFractionDigits = 12)
}
