package com.cytarch.cellularity.shared.ui.play.microscope

import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.immutableListCopy
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.log10
import kotlin.math.pow

enum class MicroscopeTextTransform {
    NONE,
    PREFIX_LABEL,
    FORMATTED_LABEL
}

/** Stable resource vocabulary; platform resource identifiers never enter microscope state. */
enum class MicroscopeTextKey(
    val transform: MicroscopeTextTransform = MicroscopeTextTransform.NONE
) {
    MICROSCOPE_SELECT_CELL_PROMPT,
    MICROSCOPE_CELL_AGE_FORMAT(MicroscopeTextTransform.FORMATTED_LABEL),
    MICROSCOPE_CELL_MASS_FORMAT(MicroscopeTextTransform.FORMATTED_LABEL),
    MICROSCOPE_CELL_DIAMETER_FORMAT(MicroscopeTextTransform.FORMATTED_LABEL),
    MICROSCOPE_CELL_NITROGEN_RESERVE_FORMAT(MicroscopeTextTransform.FORMATTED_LABEL),
    MICROSCOPE_CELL_SPEED_FORMAT(MicroscopeTextTransform.FORMATTED_LABEL),
    MICROSCOPE_CELL_TOXINS_FORMAT(MicroscopeTextTransform.FORMATTED_LABEL),
    MICROSCOPE_CELL_INJURY_FORMAT(MicroscopeTextTransform.FORMATTED_LABEL),
    MICROSCOPE_CELL_LIFT_FORMAT(MicroscopeTextTransform.FORMATTED_LABEL),
    MICROSCOPE_CELL_ACTIVE_MODE_LABEL(MicroscopeTextTransform.PREFIX_LABEL),
    MICROSCOPE_CELL_TYPE_LABEL(MicroscopeTextTransform.PREFIX_LABEL),
    MICROSCOPE_CELL_MIRRORED_LABEL(MicroscopeTextTransform.PREFIX_LABEL),
    MICROSCOPE_CELL_TAG_LABEL(MicroscopeTextTransform.PREFIX_LABEL),
    MICROSCOPE_CELL_MUTATIONS_LABEL(MicroscopeTextTransform.PREFIX_LABEL),
    MICROSCOPE_CELL_TELOMERES_LABEL(MicroscopeTextTransform.PREFIX_LABEL),
    MICROSCOPE_CELL_DENSITY_LABEL(MicroscopeTextTransform.PREFIX_LABEL),
    MICROSCOPE_CELL_LIPIDS_FORMAT(MicroscopeTextTransform.FORMATTED_LABEL),
    MICROSCOPE_CELL_TAG_USER,
    MICROSCOPE_CELL_TAG_CHALLENGE,
    MICROSCOPE_CELL_TAG_CONTAMINATION,
    MICROSCOPE_CELL_TAG_INFECTED_USER,
    MICROSCOPE_CELL_TAG_INFECTED_CHALLENGE,
    MICROSCOPE_CELL_TAG_INFECTED_CONTAMINATION,
    MICROSCOPE_CELL_TAG_HYBRID,
    CELL_PROPERTY_TRUE_VALUE,
    CELL_PROPERTY_FALSE_VALUE,
    CELL_PROPERTY_SIGNAL_SUBSTANCE_PREFIX,
    CELL_PROPERTY_SIGNAL_SUBSTANCE_UNIT,
    SIMULATION_HOUR_ABBREVIATION,
    CELL_PROPERTY_MUSCLE_CONTRACTION,
    CELL_PROPERTY_MUSCLE_BENDING,
    CELL_PROPERTY_MUSCLE_LIFTING,
    GENOME_EDITOR_SECRETION_LABEL,
    GENOME_EDITOR_SENSE_TYPE_LABEL,
    GENOME_EDITOR_COMPATIBLE_MODE_LABEL,
    GENOME_EDITOR_MODE_AFTER_FERTILIZATION_LABEL,
    CELL_PROPERTY_SECRETION_HCN,
    CELL_PROPERTY_SECRETION_NUTRIENT_SMELL,
    CELL_PROPERTY_SECRETION_COATED_NUTRIENT_SMELL,
    CELL_PROPERTY_SECRETION_LIPASE,
    CELL_PROPERTY_SECRETION_PROTEASE,
    CELL_PROPERTY_SECRETION_LIGHT_SENSE_ACTIVATOR,
    CELL_PROPERTY_SECRETION_WALL_SENSE_ACTIVATOR,
    CELL_PROPERTY_SENSE_CELL,
    CELL_PROPERTY_SENSE_WALL,
    CELL_PROPERTY_SENSE_LIGHT,
    CELL_PROPERTY_SENSE_NUTRIENT,
    CELL_PROPERTY_SENSE_COATED_NUTRIENT,
    CELL_PROPERTY_SENSE_VELOCITY,
    MICROSCOPE_DIAGNOSIS_TITLE,
    MICROSCOPE_PLATE_OPTIONS_CONTENT_DESCRIPTION,
    MICROSCOPE_SECTION_CELL,
    MICROSCOPE_SECTION_GROWTH,
    MICROSCOPE_SECTION_GENOME,
    MICROSCOPE_SECTION_SIGNALS,
    MICROSCOPE_SPLIT_LABEL,
    MICROSCOPE_NO_SPLIT_VALUE,
    MICROSCOPE_SPLIT_PROGRESS_LABEL,
    MICROSCOPE_SWIM_FORCE_LABEL,
    MICROSCOPE_SENSE_OUTPUT_LABEL,
    MICROSCOPE_SENSE_AMPLITUDE_LABEL,
    MICROSCOPE_SENSE_RGB_LABEL,
    MICROSCOPE_SENSE_THRESHOLD_LABEL,
    MICROSCOPE_STEM_THRESHOLD_1_LABEL,
    MICROSCOPE_STEM_THRESHOLD_2_LABEL,
    MICROSCOPE_VIRUS_SOURCE_GENE_LABEL,
    MICROSCOPE_VIRUS_TARGET_GENE_LABEL,
    MICROSCOPE_OUTPUT_LABEL,
    MICROSCOPE_VALUE_WITH_UNIT_FORMAT,
    MICROSCOPE_RATE_WITH_UNIT_FORMAT,
    MICROSCOPE_PERCENT_VALUE_FORMAT,
    MICROSCOPE_MODE_TITLE_FORMAT,
    MICROSCOPE_LABELED_VALUE_FORMAT,
    MICROSCOPE_DENSITY_VALUE_FORMAT,
    MICROSCOPE_SCALED_FORCE_VALUE_FORMAT,
    MICROSCOPE_RGB_VALUE_FORMAT,
    MICROSCOPE_NUMBERED_LABEL_FORMAT,
    MICROSCOPE_OUTPUT_RATE_FORMAT,
    MICROSCOPE_GENE_NAME_FORMAT,
    MICROSCOPE_SIGNAL_NAME_FORMAT,
    MICROSCOPE_SIGNED_SIGNAL_NAME_FORMAT,
    MICROSCOPE_ZOOM_VALUE_FORMAT,
    MICROSCOPE_SCIENTIFIC_ZOOM_VALUE_FORMAT,
    MICROSCOPE_UNIT_NANOGRAM,
    MICROSCOPE_UNIT_MICROMETRE,
    MICROSCOPE_UNIT_DENSITY_DELTA
}

enum class MicroscopeTextBaseline {
    NORMAL,
    SUBSCRIPT,
    SUPERSCRIPT
}

data class MicroscopeTextColor(
    val red: Float,
    val green: Float,
    val blue: Float
)

sealed interface MicroscopeTextPart {
    data class Literal(
        val value: String,
        val baseline: MicroscopeTextBaseline = MicroscopeTextBaseline.NORMAL,
        val color: MicroscopeTextColor? = null
    ) : MicroscopeTextPart

    data class Resource(val key: MicroscopeTextKey) : MicroscopeTextPart

    data class CellTypeName(val type: CellType) : MicroscopeTextPart

    data class Number(
        val canonicalValue: String,
        val baseline: MicroscopeTextBaseline = MicroscopeTextBaseline.NORMAL,
        val color: MicroscopeTextColor? = null
    ) : MicroscopeTextPart

    class Template(
        val key: MicroscopeTextKey,
        arguments: Iterable<MicroscopeText>,
        val baseline: MicroscopeTextBaseline = MicroscopeTextBaseline.NORMAL,
        val color: MicroscopeTextColor? = null
    ) : MicroscopeTextPart {
        val arguments: List<MicroscopeText> = immutableListCopy(arguments)

        override fun equals(other: Any?): Boolean = other is Template &&
            key == other.key && arguments == other.arguments &&
            baseline == other.baseline && color == other.color

        override fun hashCode(): Int = 31 * (
            31 * (31 * key.hashCode() + arguments.hashCode()) + baseline.hashCode()
            ) + (color?.hashCode() ?: 0)

        override fun toString(): String =
            "Template(key=$key, arguments=$arguments, baseline=$baseline, color=$color)"
    }
}

class MicroscopeText(parts: Iterable<MicroscopeTextPart>) {
    val parts: List<MicroscopeTextPart> = immutableListCopy(parts)

    override fun equals(other: Any?): Boolean =
        other is MicroscopeText && parts == other.parts

    override fun hashCode(): Int = parts.hashCode()

    override fun toString(): String = "MicroscopeText(parts=$parts)"

    companion object {
        val Empty = MicroscopeText(emptyList())

        fun literal(value: String): MicroscopeText =
            MicroscopeText(listOf(MicroscopeTextPart.Literal(value)))

        fun resource(key: MicroscopeTextKey): MicroscopeText =
            MicroscopeText(listOf(MicroscopeTextPart.Resource(key)))

        fun number(canonicalValue: String): MicroscopeText =
            MicroscopeText(listOf(MicroscopeTextPart.Number(canonicalValue)))

        fun template(
            key: MicroscopeTextKey,
            vararg arguments: MicroscopeText
        ): MicroscopeText = MicroscopeText(
            listOf(MicroscopeTextPart.Template(key, arguments.asList()))
        )

        fun cellType(type: CellType): MicroscopeText =
            MicroscopeText(listOf(MicroscopeTextPart.CellTypeName(type)))

        fun of(vararg parts: MicroscopeTextPart): MicroscopeText =
            MicroscopeText(parts.toList())
    }
}

data class MicroscopeDiagnosisItem(
    val label: MicroscopeText,
    val value: MicroscopeText
)

class MicroscopeDiagnosisSection(
    val title: MicroscopeText,
    items: Iterable<MicroscopeDiagnosisItem>
) {
    val items: List<MicroscopeDiagnosisItem> = immutableListCopy(items)

    override fun equals(other: Any?): Boolean =
        other is MicroscopeDiagnosisSection && title == other.title && items == other.items

    override fun hashCode(): Int = 31 * title.hashCode() + items.hashCode()

    override fun toString(): String = "MicroscopeDiagnosisSection(title=$title, items=$items)"
}

class MicroscopeDiagnosis(
    val title: MicroscopeText,
    val subtitle: MicroscopeText = MicroscopeText.Empty,
    sections: Iterable<MicroscopeDiagnosisSection> = emptyList()
) {
    val sections: List<MicroscopeDiagnosisSection> = immutableListCopy(sections)

    override fun equals(other: Any?): Boolean = other is MicroscopeDiagnosis &&
        title == other.title && subtitle == other.subtitle && sections == other.sections

    override fun hashCode(): Int =
        31 * (31 * title.hashCode() + subtitle.hashCode()) + sections.hashCode()

    override fun toString(): String =
        "MicroscopeDiagnosis(title=$title, subtitle=$subtitle, sections=$sections)"

    companion object {
        fun noSelection(): MicroscopeDiagnosis = MicroscopeDiagnosis(
            title = MicroscopeText.resource(MicroscopeTextKey.MICROSCOPE_SELECT_CELL_PROMPT)
        )
    }
}

object MicroscopeTextFormatter {
    fun zoom(magnification: Double): MicroscopeText {
        if (magnification <= 1000.0) {
            return MicroscopeText.template(
                MicroscopeTextKey.MICROSCOPE_ZOOM_VALUE_FORMAT,
                MicroscopeText.number(formatFixed(magnification, 0))
            )
        }
        val exponent = floor(log10(magnification)).toInt()
        val mantissa = magnification * 10.0.pow(-exponent.toDouble())
        return MicroscopeText.template(
            MicroscopeTextKey.MICROSCOPE_SCIENTIFIC_ZOOM_VALUE_FORMAT,
            MicroscopeText.number(formatFixed(mantissa, 2)),
            MicroscopeText(
                listOf(
                    MicroscopeTextPart.Number(
                        canonicalValue = exponent.toString(),
                        baseline = MicroscopeTextBaseline.SUPERSCRIPT
                    )
                )
            )
        )
    }

    internal fun significant(value: Double): String {
        if (!value.isFinite()) return value.toString()
        if (abs(value) < 1e-9) return "0"
        val exponent = floor(log10(abs(value))).toInt()
        val decimalPlaces = SIGNIFICANT_DIGITS - 1 - exponent
        return if (decimalPlaces >= 0) {
            formatFixed(value, decimalPlaces.coerceAtMost(MAX_DECIMAL_PLACES), trimZeros = true)
        } else {
            val divisor = 10.0.pow(-decimalPlaces.toDouble())
            formatFixed(roundHalfUp(value / divisor) * divisor, 0)
        }
    }

    private fun formatFixed(
        value: Double,
        fractionDigits: Int,
        trimZeros: Boolean = false
    ): String {
        if (!value.isFinite()) return value.toString()
        val digits = fractionDigits.coerceIn(0, MAX_DECIMAL_PLACES)
        val scale = POWERS_OF_TEN[digits]
        val absoluteScaled = abs(value) * scale.toDouble()
        if (!absoluteScaled.isFinite() || absoluteScaled > Long.MAX_VALUE.toDouble()) {
            return value.toString()
        }
        val scaled = floor(absoluteScaled + 0.5 + ROUNDING_EPSILON).toLong()
        val sign = if (value < 0.0 && scaled != 0L) "-" else ""
        if (digits == 0) return sign + scaled
        val whole = scaled / scale
        var fraction = (scaled % scale).toString().padStart(digits, '0')
        if (trimZeros) fraction = fraction.trimEnd('0')
        return if (fraction.isEmpty()) "$sign$whole" else "$sign$whole.$fraction"
    }

    private const val SIGNIFICANT_DIGITS = 5
    private const val MAX_DECIMAL_PLACES = 12
    private const val ROUNDING_EPSILON = 1e-9

    private fun roundHalfUp(value: Double): Double = if (value < 0.0) {
        -floor(-value + 0.5 + ROUNDING_EPSILON)
    } else {
        floor(value + 0.5 + ROUNDING_EPSILON)
    }
    private val POWERS_OF_TEN = longArrayOf(
        1L,
        10L,
        100L,
        1_000L,
        10_000L,
        100_000L,
        1_000_000L,
        10_000_000L,
        100_000_000L,
        1_000_000_000L,
        10_000_000_000L,
        100_000_000_000L,
        1_000_000_000_000L
    )
}
