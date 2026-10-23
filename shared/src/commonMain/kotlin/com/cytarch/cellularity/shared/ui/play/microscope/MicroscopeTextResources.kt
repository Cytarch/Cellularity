package com.cytarch.cellularity.shared.ui.play.microscope

import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.BaselineShift
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.em
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_active_mode_label
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_age_format
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_density_label
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_diameter_format
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_injury_format
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_lift_format
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_lipids_format
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_mass_format
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_mirrored_label
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_mutations_label
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_nitrogen_reserve_format
import com.cytarch.cellularity.shared.generated.resources.microscope_select_cell_prompt
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_speed_format
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_tag_label
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_tag_challenge
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_tag_contamination
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_tag_hybrid
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_tag_infected_challenge
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_tag_infected_contamination
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_tag_infected_user
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_tag_user
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_telomeres_label
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_toxins_format
import com.cytarch.cellularity.shared.generated.resources.microscope_cell_type_label
import com.cytarch.cellularity.shared.generated.resources.simulation_cell_type_names
import com.cytarch.cellularity.shared.generated.resources.genome_editor_sense_type_label
import com.cytarch.cellularity.shared.generated.resources.genome_editor_compatible_mode_label
import com.cytarch.cellularity.shared.generated.resources.genome_editor_mode_after_fertilization_label
import com.cytarch.cellularity.shared.generated.resources.genome_editor_secretion_label
import com.cytarch.cellularity.shared.generated.resources.simulation_hour_abbreviation
import com.cytarch.cellularity.shared.generated.resources.microscope_diagnosis_title
import com.cytarch.cellularity.shared.generated.resources.microscope_density_value_format
import com.cytarch.cellularity.shared.generated.resources.microscope_gene_name_format
import com.cytarch.cellularity.shared.generated.resources.microscope_labeled_value_format
import com.cytarch.cellularity.shared.generated.resources.microscope_mode_title_format
import com.cytarch.cellularity.shared.generated.resources.microscope_no_split_value
import com.cytarch.cellularity.shared.generated.resources.microscope_numbered_label_format
import com.cytarch.cellularity.shared.generated.resources.microscope_output_label
import com.cytarch.cellularity.shared.generated.resources.microscope_output_rate_format
import com.cytarch.cellularity.shared.generated.resources.microscope_plate_options_content_description
import com.cytarch.cellularity.shared.generated.resources.microscope_percent_value_format
import com.cytarch.cellularity.shared.generated.resources.microscope_rate_with_unit_format
import com.cytarch.cellularity.shared.generated.resources.microscope_rgb_value_format
import com.cytarch.cellularity.shared.generated.resources.microscope_section_cell
import com.cytarch.cellularity.shared.generated.resources.microscope_section_genome
import com.cytarch.cellularity.shared.generated.resources.microscope_section_growth
import com.cytarch.cellularity.shared.generated.resources.microscope_section_signals
import com.cytarch.cellularity.shared.generated.resources.microscope_sense_amplitude_label
import com.cytarch.cellularity.shared.generated.resources.microscope_sense_output_label
import com.cytarch.cellularity.shared.generated.resources.microscope_sense_rgb_label
import com.cytarch.cellularity.shared.generated.resources.microscope_sense_threshold_label
import com.cytarch.cellularity.shared.generated.resources.microscope_scaled_force_value_format
import com.cytarch.cellularity.shared.generated.resources.microscope_scientific_zoom_value_format
import com.cytarch.cellularity.shared.generated.resources.microscope_signal_name_format
import com.cytarch.cellularity.shared.generated.resources.microscope_signed_signal_name_format
import com.cytarch.cellularity.shared.generated.resources.microscope_split_label
import com.cytarch.cellularity.shared.generated.resources.microscope_split_progress_label
import com.cytarch.cellularity.shared.generated.resources.microscope_stem_threshold_1_label
import com.cytarch.cellularity.shared.generated.resources.microscope_stem_threshold_2_label
import com.cytarch.cellularity.shared.generated.resources.microscope_swim_force_label
import com.cytarch.cellularity.shared.generated.resources.microscope_unit_density_delta
import com.cytarch.cellularity.shared.generated.resources.microscope_unit_micrometre
import com.cytarch.cellularity.shared.generated.resources.microscope_unit_nanogram
import com.cytarch.cellularity.shared.generated.resources.microscope_value_with_unit_format
import com.cytarch.cellularity.shared.generated.resources.microscope_virus_source_gene_label
import com.cytarch.cellularity.shared.generated.resources.microscope_virus_target_gene_label
import com.cytarch.cellularity.shared.generated.resources.microscope_zoom_value_format
import com.cytarch.cellularity.shared.generated.resources.cell_property_muscle_bending
import com.cytarch.cellularity.shared.generated.resources.cell_property_muscle_contraction
import com.cytarch.cellularity.shared.generated.resources.cell_property_muscle_lifting
import com.cytarch.cellularity.shared.generated.resources.cell_property_secretion_coated_nutrient_smell
import com.cytarch.cellularity.shared.generated.resources.cell_property_secretion_hcn
import com.cytarch.cellularity.shared.generated.resources.cell_property_secretion_nutrient_smell
import com.cytarch.cellularity.shared.generated.resources.cell_property_secretion_light_sense_activator
import com.cytarch.cellularity.shared.generated.resources.cell_property_secretion_protease
import com.cytarch.cellularity.shared.generated.resources.cell_property_secretion_lipase
import com.cytarch.cellularity.shared.generated.resources.cell_property_secretion_wall_sense_activator
import com.cytarch.cellularity.shared.generated.resources.cell_property_signal_substance_prefix
import com.cytarch.cellularity.shared.generated.resources.cell_property_signal_substance_unit
import com.cytarch.cellularity.shared.generated.resources.cell_property_sense_cell
import com.cytarch.cellularity.shared.generated.resources.cell_property_sense_nutrient
import com.cytarch.cellularity.shared.generated.resources.cell_property_sense_light
import com.cytarch.cellularity.shared.generated.resources.cell_property_sense_coated_nutrient
import com.cytarch.cellularity.shared.generated.resources.cell_property_sense_velocity
import com.cytarch.cellularity.shared.generated.resources.cell_property_sense_wall
import com.cytarch.cellularity.shared.generated.resources.cell_property_false_value
import com.cytarch.cellularity.shared.generated.resources.cell_property_true_value
import com.cytarch.cellularity.shared.ui.text.localizeCanonicalNumber
import org.jetbrains.compose.resources.StringResource
import org.jetbrains.compose.resources.stringArrayResource
import org.jetbrains.compose.resources.stringResource

/** Keeps platform resource identifiers out of retained microscope state. */
internal data class MicroscopeItemPresentationDescriptor(
    val key: MicroscopeTextKey,
    val resource: StringResource
)

internal fun MicroscopeTextKey.presentationDescriptor(): MicroscopeItemPresentationDescriptor =
    MicroscopeItemPresentationDescriptor(this, resource())

@Composable
internal fun microscopeString(key: MicroscopeTextKey): String =
    transformMicroscopeResourceText(key, stringResource(key.presentationDescriptor().resource))

@Composable
internal fun resolveMicroscopeText(text: MicroscopeText): AnnotatedString {
    val cellTypeNames = stringArrayResource(Res.array.simulation_cell_type_names)
    return resolveMicroscopeText(text, cellTypeNames)
}

@Composable
private fun resolveMicroscopeText(
    text: MicroscopeText,
    cellTypeNames: List<String>
): AnnotatedString = buildAnnotatedString {
    for (part in text.parts) {
        append(
            when (part) {
                is MicroscopeTextPart.Literal -> styledMicroscopeText(
                    value = AnnotatedString(part.value),
                    baseline = part.baseline,
                    color = part.color
                )
                is MicroscopeTextPart.Resource -> AnnotatedString(microscopeString(part.key))
                is MicroscopeTextPart.CellTypeName -> AnnotatedString(
                    cellTypeNames.getOrElse(part.type.nativeValue) {
                        part.type.nativeValue.toString()
                    }
                )
                is MicroscopeTextPart.Number -> styledMicroscopeText(
                    value = AnnotatedString(localizeCanonicalNumber(part.canonicalValue)),
                    baseline = part.baseline,
                    color = part.color
                )
                is MicroscopeTextPart.Template -> resolveMicroscopeTemplate(
                    part = part,
                    cellTypeNames = cellTypeNames
                )
            }
        )
    }
}

@Composable
private fun resolveMicroscopeTemplate(
    part: MicroscopeTextPart.Template,
    cellTypeNames: List<String>
): AnnotatedString {
    val arguments = mutableListOf<AnnotatedString>()
    for (argument in part.arguments) {
        arguments += resolveMicroscopeText(argument, cellTypeNames)
    }
    val markers = arguments.indices.map { index -> "\uFDD0$index\uFDD1" }
    val formatted = stringResource(
        part.key.presentationDescriptor().resource,
        *markers.toTypedArray()
    )
    val value = interpolateMicroscopeTemplate(formatted, markers, arguments)
    return styledMicroscopeText(value, part.baseline, part.color)
}

internal fun interpolateMicroscopeTemplate(
    formatted: String,
    markers: List<String>,
    arguments: List<AnnotatedString>
): AnnotatedString = buildAnnotatedString {
    var cursor = 0
    while (cursor < formatted.length) {
        var nextPosition = -1
        var nextArgument = -1
        for (index in markers.indices) {
            val position = formatted.indexOf(markers[index], startIndex = cursor)
            if (position >= 0 && (nextPosition < 0 || position < nextPosition)) {
                nextPosition = position
                nextArgument = index
            }
        }
        if (nextPosition < 0) {
            append(formatted.substring(cursor))
            break
        }
        append(formatted.substring(cursor, nextPosition))
        append(arguments[nextArgument])
        cursor = nextPosition + markers[nextArgument].length
    }
}

private fun styledMicroscopeText(
    value: AnnotatedString,
    baseline: MicroscopeTextBaseline,
    color: MicroscopeTextColor?
): AnnotatedString {
    if (baseline == MicroscopeTextBaseline.NORMAL && color == null) return value
    val style = SpanStyle(
        color = color?.let { Color(it.red, it.green, it.blue) } ?: Color.Unspecified,
        baselineShift = when (baseline) {
            MicroscopeTextBaseline.NORMAL -> null
            MicroscopeTextBaseline.SUBSCRIPT -> BaselineShift.Subscript
            MicroscopeTextBaseline.SUPERSCRIPT -> BaselineShift.Superscript
        },
        fontSize = if (baseline == MicroscopeTextBaseline.NORMAL) {
            androidx.compose.ui.unit.TextUnit.Unspecified
        } else {
            0.75.em
        }
    )
    return buildAnnotatedString {
        withStyle(style) { append(value) }
    }
}

private fun transformMicroscopeResourceText(
    key: MicroscopeTextKey,
    value: String
): String = when (key.transform) {
    MicroscopeTextTransform.NONE -> value
    MicroscopeTextTransform.PREFIX_LABEL -> value.toMicroscopeLabel()
    MicroscopeTextTransform.FORMATTED_LABEL -> value
        .substringBefore('%')
        .replace(HTML_TAG, "")
        .toMicroscopeLabel()
}

private fun String.toMicroscopeLabel(): String =
    trim().removePrefix("\"").removeSuffix("\"").trim().removeSuffix(":").trim()

private fun MicroscopeTextKey.resource(): StringResource = when (this) {
    MicroscopeTextKey.MICROSCOPE_SELECT_CELL_PROMPT -> Res.string.microscope_select_cell_prompt
    MicroscopeTextKey.MICROSCOPE_CELL_AGE_FORMAT -> Res.string.microscope_cell_age_format
    MicroscopeTextKey.MICROSCOPE_CELL_MASS_FORMAT -> Res.string.microscope_cell_mass_format
    MicroscopeTextKey.MICROSCOPE_CELL_DIAMETER_FORMAT -> Res.string.microscope_cell_diameter_format
    MicroscopeTextKey.MICROSCOPE_CELL_NITROGEN_RESERVE_FORMAT -> Res.string.microscope_cell_nitrogen_reserve_format
    MicroscopeTextKey.MICROSCOPE_CELL_SPEED_FORMAT -> Res.string.microscope_cell_speed_format
    MicroscopeTextKey.MICROSCOPE_CELL_TOXINS_FORMAT -> Res.string.microscope_cell_toxins_format
    MicroscopeTextKey.MICROSCOPE_CELL_INJURY_FORMAT -> Res.string.microscope_cell_injury_format
    MicroscopeTextKey.MICROSCOPE_CELL_LIFT_FORMAT -> Res.string.microscope_cell_lift_format
    MicroscopeTextKey.MICROSCOPE_CELL_ACTIVE_MODE_LABEL -> Res.string.microscope_cell_active_mode_label
    MicroscopeTextKey.MICROSCOPE_CELL_TYPE_LABEL -> Res.string.microscope_cell_type_label
    MicroscopeTextKey.MICROSCOPE_CELL_MIRRORED_LABEL -> Res.string.microscope_cell_mirrored_label
    MicroscopeTextKey.MICROSCOPE_CELL_TAG_LABEL -> Res.string.microscope_cell_tag_label
    MicroscopeTextKey.MICROSCOPE_CELL_MUTATIONS_LABEL -> Res.string.microscope_cell_mutations_label
    MicroscopeTextKey.MICROSCOPE_CELL_TELOMERES_LABEL -> Res.string.microscope_cell_telomeres_label
    MicroscopeTextKey.MICROSCOPE_CELL_DENSITY_LABEL -> Res.string.microscope_cell_density_label
    MicroscopeTextKey.MICROSCOPE_CELL_LIPIDS_FORMAT -> Res.string.microscope_cell_lipids_format
    MicroscopeTextKey.MICROSCOPE_CELL_TAG_USER -> Res.string.microscope_cell_tag_user
    MicroscopeTextKey.MICROSCOPE_CELL_TAG_CHALLENGE -> Res.string.microscope_cell_tag_challenge
    MicroscopeTextKey.MICROSCOPE_CELL_TAG_CONTAMINATION -> Res.string.microscope_cell_tag_contamination
    MicroscopeTextKey.MICROSCOPE_CELL_TAG_INFECTED_USER -> Res.string.microscope_cell_tag_infected_user
    MicroscopeTextKey.MICROSCOPE_CELL_TAG_INFECTED_CHALLENGE ->
        Res.string.microscope_cell_tag_infected_challenge
    MicroscopeTextKey.MICROSCOPE_CELL_TAG_INFECTED_CONTAMINATION ->
        Res.string.microscope_cell_tag_infected_contamination
    MicroscopeTextKey.MICROSCOPE_CELL_TAG_HYBRID -> Res.string.microscope_cell_tag_hybrid
    MicroscopeTextKey.CELL_PROPERTY_TRUE_VALUE -> Res.string.cell_property_true_value
    MicroscopeTextKey.CELL_PROPERTY_FALSE_VALUE -> Res.string.cell_property_false_value
    MicroscopeTextKey.CELL_PROPERTY_SIGNAL_SUBSTANCE_PREFIX -> Res.string.cell_property_signal_substance_prefix
    MicroscopeTextKey.CELL_PROPERTY_SIGNAL_SUBSTANCE_UNIT -> Res.string.cell_property_signal_substance_unit
    MicroscopeTextKey.SIMULATION_HOUR_ABBREVIATION -> Res.string.simulation_hour_abbreviation
    MicroscopeTextKey.CELL_PROPERTY_MUSCLE_CONTRACTION -> Res.string.cell_property_muscle_contraction
    MicroscopeTextKey.CELL_PROPERTY_MUSCLE_BENDING -> Res.string.cell_property_muscle_bending
    MicroscopeTextKey.CELL_PROPERTY_MUSCLE_LIFTING -> Res.string.cell_property_muscle_lifting
    MicroscopeTextKey.GENOME_EDITOR_SECRETION_LABEL -> Res.string.genome_editor_secretion_label
    MicroscopeTextKey.GENOME_EDITOR_SENSE_TYPE_LABEL -> Res.string.genome_editor_sense_type_label
    MicroscopeTextKey.GENOME_EDITOR_COMPATIBLE_MODE_LABEL -> Res.string.genome_editor_compatible_mode_label
    MicroscopeTextKey.GENOME_EDITOR_MODE_AFTER_FERTILIZATION_LABEL -> Res.string.genome_editor_mode_after_fertilization_label
    MicroscopeTextKey.CELL_PROPERTY_SECRETION_HCN -> Res.string.cell_property_secretion_hcn
    MicroscopeTextKey.CELL_PROPERTY_SECRETION_NUTRIENT_SMELL -> Res.string.cell_property_secretion_nutrient_smell
    MicroscopeTextKey.CELL_PROPERTY_SECRETION_COATED_NUTRIENT_SMELL -> Res.string.cell_property_secretion_coated_nutrient_smell
    MicroscopeTextKey.CELL_PROPERTY_SECRETION_LIPASE -> Res.string.cell_property_secretion_lipase
    MicroscopeTextKey.CELL_PROPERTY_SECRETION_PROTEASE -> Res.string.cell_property_secretion_protease
    MicroscopeTextKey.CELL_PROPERTY_SECRETION_LIGHT_SENSE_ACTIVATOR -> Res.string.cell_property_secretion_light_sense_activator
    MicroscopeTextKey.CELL_PROPERTY_SECRETION_WALL_SENSE_ACTIVATOR -> Res.string.cell_property_secretion_wall_sense_activator
    MicroscopeTextKey.CELL_PROPERTY_SENSE_CELL -> Res.string.cell_property_sense_cell
    MicroscopeTextKey.CELL_PROPERTY_SENSE_WALL -> Res.string.cell_property_sense_wall
    MicroscopeTextKey.CELL_PROPERTY_SENSE_LIGHT -> Res.string.cell_property_sense_light
    MicroscopeTextKey.CELL_PROPERTY_SENSE_NUTRIENT -> Res.string.cell_property_sense_nutrient
    MicroscopeTextKey.CELL_PROPERTY_SENSE_COATED_NUTRIENT -> Res.string.cell_property_sense_coated_nutrient
    MicroscopeTextKey.CELL_PROPERTY_SENSE_VELOCITY -> Res.string.cell_property_sense_velocity
    MicroscopeTextKey.MICROSCOPE_DIAGNOSIS_TITLE -> Res.string.microscope_diagnosis_title
    MicroscopeTextKey.MICROSCOPE_PLATE_OPTIONS_CONTENT_DESCRIPTION -> Res.string.microscope_plate_options_content_description
    MicroscopeTextKey.MICROSCOPE_SECTION_CELL -> Res.string.microscope_section_cell
    MicroscopeTextKey.MICROSCOPE_SECTION_GROWTH -> Res.string.microscope_section_growth
    MicroscopeTextKey.MICROSCOPE_SECTION_GENOME -> Res.string.microscope_section_genome
    MicroscopeTextKey.MICROSCOPE_SECTION_SIGNALS -> Res.string.microscope_section_signals
    MicroscopeTextKey.MICROSCOPE_SPLIT_LABEL -> Res.string.microscope_split_label
    MicroscopeTextKey.MICROSCOPE_NO_SPLIT_VALUE -> Res.string.microscope_no_split_value
    MicroscopeTextKey.MICROSCOPE_SPLIT_PROGRESS_LABEL -> Res.string.microscope_split_progress_label
    MicroscopeTextKey.MICROSCOPE_SWIM_FORCE_LABEL -> Res.string.microscope_swim_force_label
    MicroscopeTextKey.MICROSCOPE_SENSE_OUTPUT_LABEL -> Res.string.microscope_sense_output_label
    MicroscopeTextKey.MICROSCOPE_SENSE_AMPLITUDE_LABEL -> Res.string.microscope_sense_amplitude_label
    MicroscopeTextKey.MICROSCOPE_SENSE_RGB_LABEL -> Res.string.microscope_sense_rgb_label
    MicroscopeTextKey.MICROSCOPE_SENSE_THRESHOLD_LABEL -> Res.string.microscope_sense_threshold_label
    MicroscopeTextKey.MICROSCOPE_STEM_THRESHOLD_1_LABEL -> Res.string.microscope_stem_threshold_1_label
    MicroscopeTextKey.MICROSCOPE_STEM_THRESHOLD_2_LABEL -> Res.string.microscope_stem_threshold_2_label
    MicroscopeTextKey.MICROSCOPE_VIRUS_SOURCE_GENE_LABEL -> Res.string.microscope_virus_source_gene_label
    MicroscopeTextKey.MICROSCOPE_VIRUS_TARGET_GENE_LABEL -> Res.string.microscope_virus_target_gene_label
    MicroscopeTextKey.MICROSCOPE_OUTPUT_LABEL -> Res.string.microscope_output_label
    MicroscopeTextKey.MICROSCOPE_VALUE_WITH_UNIT_FORMAT -> Res.string.microscope_value_with_unit_format
    MicroscopeTextKey.MICROSCOPE_RATE_WITH_UNIT_FORMAT -> Res.string.microscope_rate_with_unit_format
    MicroscopeTextKey.MICROSCOPE_PERCENT_VALUE_FORMAT -> Res.string.microscope_percent_value_format
    MicroscopeTextKey.MICROSCOPE_MODE_TITLE_FORMAT -> Res.string.microscope_mode_title_format
    MicroscopeTextKey.MICROSCOPE_LABELED_VALUE_FORMAT -> Res.string.microscope_labeled_value_format
    MicroscopeTextKey.MICROSCOPE_DENSITY_VALUE_FORMAT -> Res.string.microscope_density_value_format
    MicroscopeTextKey.MICROSCOPE_SCALED_FORCE_VALUE_FORMAT ->
        Res.string.microscope_scaled_force_value_format
    MicroscopeTextKey.MICROSCOPE_RGB_VALUE_FORMAT -> Res.string.microscope_rgb_value_format
    MicroscopeTextKey.MICROSCOPE_NUMBERED_LABEL_FORMAT -> Res.string.microscope_numbered_label_format
    MicroscopeTextKey.MICROSCOPE_OUTPUT_RATE_FORMAT -> Res.string.microscope_output_rate_format
    MicroscopeTextKey.MICROSCOPE_GENE_NAME_FORMAT -> Res.string.microscope_gene_name_format
    MicroscopeTextKey.MICROSCOPE_SIGNAL_NAME_FORMAT -> Res.string.microscope_signal_name_format
    MicroscopeTextKey.MICROSCOPE_SIGNED_SIGNAL_NAME_FORMAT ->
        Res.string.microscope_signed_signal_name_format
    MicroscopeTextKey.MICROSCOPE_ZOOM_VALUE_FORMAT -> Res.string.microscope_zoom_value_format
    MicroscopeTextKey.MICROSCOPE_SCIENTIFIC_ZOOM_VALUE_FORMAT ->
        Res.string.microscope_scientific_zoom_value_format
    MicroscopeTextKey.MICROSCOPE_UNIT_NANOGRAM -> Res.string.microscope_unit_nanogram
    MicroscopeTextKey.MICROSCOPE_UNIT_MICROMETRE -> Res.string.microscope_unit_micrometre
    MicroscopeTextKey.MICROSCOPE_UNIT_DENSITY_DELTA ->
        Res.string.microscope_unit_density_delta
}

private val HTML_TAG = Regex("<[^>]+>")
