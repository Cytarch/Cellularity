package com.cytarch.cellularity.shared.ui.play.microscope

import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.CellInspectionSnapshot
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.SimulationContract
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/** Pure projection from an immutable simulation inspection to shared diagnosis text tokens. */
object MicroscopeDiagnosisMapper {
    fun map(inspection: CellInspectionSnapshot?): MicroscopeDiagnosis {
        if (inspection == null) return MicroscopeDiagnosis.noSelection()
        val cell = inspection.cell
        val activeGene = inspection.genome.genes.getOrNull(cell.geneIndex)
        val cellTypeText = MicroscopeText.cellType(cell.cellType)

        val cellItems = mutableListOf(
            item(
                MicroscopeTextKey.MICROSCOPE_CELL_AGE_FORMAT,
                valueWithUnit(cell.age, MicroscopeTextKey.SIMULATION_HOUR_ABBREVIATION)
            ),
            item(
                MicroscopeTextKey.MICROSCOPE_CELL_MASS_FORMAT,
                valueWithUnit(cell.mass * 10.0, MicroscopeTextKey.MICROSCOPE_UNIT_NANOGRAM)
            ),
            item(
                MicroscopeTextKey.MICROSCOPE_CELL_DIAMETER_FORMAT,
                valueWithUnit(
                    cell.radius * 2000.0,
                    MicroscopeTextKey.MICROSCOPE_UNIT_MICROMETRE
                )
            ),
            item(MicroscopeTextKey.MICROSCOPE_CELL_NITROGEN_RESERVE_FORMAT, percentValue(cell.nitrates * 100.0)),
            item(
                MicroscopeTextKey.MICROSCOPE_CELL_SPEED_FORMAT,
                rateWithUnit(
                    cellSpeed(cell),
                    MicroscopeTextKey.MICROSCOPE_UNIT_MICROMETRE,
                    MicroscopeTextKey.SIMULATION_HOUR_ABBREVIATION
                )
            ),
            item(
                MicroscopeTextKey.MICROSCOPE_CELL_TOXINS_FORMAT,
                valueWithUnit(cell.toxin, MicroscopeTextKey.CELL_PROPERTY_SIGNAL_SUBSTANCE_UNIT)
            )
        )
        if (cell.toxication != 0f) {
            cellItems += item(
                MicroscopeTextKey.MICROSCOPE_CELL_INJURY_FORMAT,
                percentValue(100.0 * cell.toxication)
            )
        }
        if (cell.positionZ != 0.0) {
            cellItems += item(MicroscopeTextKey.MICROSCOPE_CELL_LIFT_FORMAT, value(cell.positionZ))
        }

        val genomeItems = mutableListOf(
            item(
                MicroscopeTextKey.MICROSCOPE_CELL_ACTIVE_MODE_LABEL,
                coloredModeValue(cell.geneIndex, cell.color)
            ),
            item(MicroscopeTextKey.MICROSCOPE_CELL_TYPE_LABEL, cellTypeText),
            item(
                MicroscopeTextKey.MICROSCOPE_CELL_MIRRORED_LABEL,
                MicroscopeText.resource(
                    if (cell.mirrored) {
                        MicroscopeTextKey.CELL_PROPERTY_TRUE_VALUE
                    } else {
                        MicroscopeTextKey.CELL_PROPERTY_FALSE_VALUE
                    }
                )
            ),
            item(MicroscopeTextKey.MICROSCOPE_CELL_TAG_LABEL, originTagName(cell.originTag)),
            item(
                MicroscopeTextKey.MICROSCOPE_CELL_MUTATIONS_LABEL,
                integer(cell.mutationCount)
            )
        )
        if (cell.telomeresCount != -1) {
            genomeItems += item(
                MicroscopeTextKey.MICROSCOPE_CELL_TELOMERES_LABEL,
                integer(cell.telomeresCount)
            )
        }

        val growthItems = mutableListOf<MicroscopeDiagnosisItem>()
        if (activeGene != null) {
            val splitMass = activeGene.splitMass
            if (splitMass >= CellDataConstants.MAX_MASS) {
                growthItems += item(
                    MicroscopeTextKey.MICROSCOPE_SPLIT_LABEL,
                    MicroscopeText.resource(MicroscopeTextKey.MICROSCOPE_NO_SPLIT_VALUE)
                )
            } else if (splitMass > 0.0f) {
                growthItems += item(
                    MicroscopeTextKey.MICROSCOPE_SPLIT_PROGRESS_LABEL,
                    percentValue(cell.mass / splitMass * 100.0)
                )
            }
        }

        val signalItems = buildList {
            for (index in 0 until CellDataConstants.SIGNAL_SUBSTANCE_COUNT) {
                add(
                    MicroscopeDiagnosisItem(
                        label = signalName(index),
                        value = valueWithUnit(
                            min(
                                1.0,
                                max(
                                    -1.0,
                                    cell.signalSubstance
                                        .getOrElse(index) { 0f }
                                        .toDouble()
                                )
                            ),
                            MicroscopeTextKey.CELL_PROPERTY_SIGNAL_SUBSTANCE_UNIT
                        )
                    )
                )
            }
        }

        val specializedItems = specializedItems(cell, activeGene)
        val sections = buildList {
            add(section(MicroscopeTextKey.MICROSCOPE_SECTION_CELL, cellItems))
            if (growthItems.isNotEmpty()) {
                add(section(MicroscopeTextKey.MICROSCOPE_SECTION_GROWTH, growthItems))
            }
            add(section(MicroscopeTextKey.MICROSCOPE_SECTION_GENOME, genomeItems))
            add(section(MicroscopeTextKey.MICROSCOPE_SECTION_SIGNALS, signalItems))
            if (specializedItems.isNotEmpty()) {
                add(MicroscopeDiagnosisSection(cellTypeText, specializedItems))
            }
        }
        return MicroscopeDiagnosis(
            title = template(
                MicroscopeTextKey.MICROSCOPE_MODE_TITLE_FORMAT,
                geneName(cell.geneIndex),
                cellTypeText
            ),
            subtitle = template(
                MicroscopeTextKey.MICROSCOPE_LABELED_VALUE_FORMAT,
                MicroscopeText.resource(MicroscopeTextKey.MICROSCOPE_CELL_TAG_LABEL),
                originTagName(cell.originTag)
            ),
            sections = sections
        )
    }

    private fun specializedItems(
        cell: CellSnapshot,
        activeGene: GeneDraft?
    ): List<MicroscopeDiagnosisItem> = buildList {
        when (cell.cellType) {
            CellType.BUOYOCYTE -> {
                val density = cellValue(activeGene, cell, GenomeContract.DENSITY_VALUE_INDEX)
                add(
                    item(
                        MicroscopeTextKey.MICROSCOPE_CELL_DENSITY_LABEL,
                        template(
                            MicroscopeTextKey.MICROSCOPE_DENSITY_VALUE_FORMAT,
                            MicroscopeText.literal("ρH₂O"),
                            concat(
                                MicroscopeText.literal(if (density < 0f) "-" else "+"),
                                number(100.0 * abs(density))
                            ),
                            resourceText(MicroscopeTextKey.MICROSCOPE_UNIT_DENSITY_DELTA)
                        )
                    )
                )
            }
            CellType.FLAGELLOCYTE -> add(
                item(
                    MicroscopeTextKey.MICROSCOPE_SWIM_FORCE_LABEL,
                    template(
                        MicroscopeTextKey.MICROSCOPE_SCALED_FORCE_VALUE_FORMAT,
                        number(
                            cellValue(activeGene, cell, GenomeContract.SWIM_FORCE_VALUE_INDEX) *
                                3.8580247
                        )
                    )
                )
            )
            CellType.MYOCYTE -> {
                add(
                    item(
                        MicroscopeTextKey.CELL_PROPERTY_MUSCLE_CONTRACTION,
                        value(
                            cellValue(
                                activeGene,
                                cell,
                                GenomeContract.MYOCYTE_CONTRACTION_VALUE_INDEX
                            )
                        )
                    )
                )
                add(
                    item(
                        MicroscopeTextKey.CELL_PROPERTY_MUSCLE_BENDING,
                        value(
                            cellValue(
                                activeGene,
                                cell,
                                GenomeContract.MYOCYTE_BENDING_VALUE_INDEX
                            )
                        )
                    )
                )
                add(
                    item(
                        MicroscopeTextKey.CELL_PROPERTY_MUSCLE_LIFTING,
                        value(
                            cellValue(
                                activeGene,
                                cell,
                                GenomeContract.MYOCYTE_LIFTING_VALUE_INDEX
                            )
                        )
                    )
                )
            }
            CellType.LIPOCYTE -> add(
                item(
                    MicroscopeTextKey.MICROSCOPE_CELL_LIPIDS_FORMAT,
                    valueWithUnit(
                        cell.lipids * 10.0,
                        MicroscopeTextKey.MICROSCOPE_UNIT_NANOGRAM
                    )
                )
            )
            CellType.SECROCYTE -> add(
                item(
                    MicroscopeTextKey.GENOME_EDITOR_SECRETION_LABEL,
                    secreteTypeName(
                        geneInteger(activeGene, GenomeContract.SECRETE_TYPE_INTEGER_INDEX)
                    )
                )
            )
            CellType.SENSEOCYTE,
            CellType.STEREOCYTE -> {
                add(
                    item(
                        MicroscopeTextKey.GENOME_EDITOR_SENSE_TYPE_LABEL,
                        smellTypeName(
                            geneInteger(activeGene, GenomeContract.SMELL_TYPE_INTEGER_INDEX)
                        )
                    )
                )
                if (activeGene != null) {
                    add(
                        item(
                            MicroscopeTextKey.MICROSCOPE_SENSE_OUTPUT_LABEL,
                            signalName(
                                geneInteger(activeGene, GenomeContract.SMELL_OUT_INTEGER_INDEX)
                            )
                        )
                    )
                    add(
                        item(
                            MicroscopeTextKey.MICROSCOPE_SENSE_AMPLITUDE_LABEL,
                            value(activeGene.floatValues.smellAmplitude)
                        )
                    )
                    add(
                        item(
                            MicroscopeTextKey.MICROSCOPE_SENSE_RGB_LABEL,
                            template(
                                MicroscopeTextKey.MICROSCOPE_RGB_VALUE_FORMAT,
                                number(activeGene.floatValues.smellRed),
                                number(activeGene.floatValues.smellGreen),
                                number(activeGene.floatValues.smellBlue)
                            )
                        )
                    )
                    add(
                        item(
                            MicroscopeTextKey.MICROSCOPE_SENSE_THRESHOLD_LABEL,
                            value(activeGene.floatValues.smellThreshold)
                        )
                    )
                }
            }
            CellType.STEMOCYTE -> {
                add(
                    item(
                        MicroscopeTextKey.MICROSCOPE_STEM_THRESHOLD_1_LABEL,
                        value(
                            cellValue(
                                activeGene,
                                cell,
                                GenomeContract.STEM_THRESHOLD_1_VALUE_INDEX
                            )
                        )
                    )
                )
                add(
                    item(
                        MicroscopeTextKey.MICROSCOPE_STEM_THRESHOLD_2_LABEL,
                        value(
                            cellValue(
                                activeGene,
                                cell,
                                GenomeContract.STEM_THRESHOLD_2_VALUE_INDEX
                            )
                        )
                    )
                )
            }
            CellType.VIROCYTE -> if (activeGene != null) {
                add(
                    item(
                        MicroscopeTextKey.MICROSCOPE_VIRUS_SOURCE_GENE_LABEL,
                        geneName(
                            geneInteger(activeGene, GenomeContract.VIRUS_FROM_INTEGER_INDEX)
                        )
                    )
                )
                add(
                    item(
                        MicroscopeTextKey.MICROSCOPE_VIRUS_TARGET_GENE_LABEL,
                        geneName(geneInteger(activeGene, GenomeContract.VIRUS_TO_INTEGER_INDEX))
                    )
                )
            }
            CellType.GAMETE -> if (activeGene != null) {
                add(
                    item(
                        MicroscopeTextKey.GENOME_EDITOR_COMPATIBLE_MODE_LABEL,
                        geneName(geneInteger(activeGene, GenomeContract.VIRUS_TO_INTEGER_INDEX))
                    )
                )
                add(
                    item(
                        MicroscopeTextKey.GENOME_EDITOR_MODE_AFTER_FERTILIZATION_LABEL,
                        geneName(
                            geneInteger(activeGene, GenomeContract.VIRUS_FROM_INTEGER_INDEX)
                        )
                    )
                )
            }
            else -> Unit
        }

        if (
            cell.cellType == CellType.NEUROCYTE ||
            cell.cellType == CellType.SENSEOCYTE ||
            cell.cellType == CellType.STEREOCYTE
        ) {
            addSignalRates(cell, activeGene)
        }
    }

    private fun MutableList<MicroscopeDiagnosisItem>.addSignalRates(
        cell: CellSnapshot,
        activeGene: GeneDraft?
    ) {
        for (index in 0 until CellDataConstants.SIGNAL_SUBSTANCE_COUNT) {
            add(
                MicroscopeDiagnosisItem(
                    label = signalName(index),
                    value = rateValue(
                        cell.newSignalSubstance.getOrElse(index) { 0f }.toDouble()
                    )
                )
            )
        }
        if (activeGene == null) return
        for (index in 0 until CellDataConstants.NEUROCYTE_OUTPUT_COUNT) {
            val signal = signalName(
                geneInteger(
                    activeGene,
                    GenomeContract.GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + index
                )
            )
            val amount = MicroscopeTextFormatter.significant(
                cellValue(
                    activeGene,
                    cell,
                    GenomeContract.GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + index
                ).toDouble()
            )
            add(
                MicroscopeDiagnosisItem(
                    label = template(
                        MicroscopeTextKey.MICROSCOPE_NUMBERED_LABEL_FORMAT,
                        resourceText(MicroscopeTextKey.MICROSCOPE_OUTPUT_LABEL),
                        integer(index + 1)
                    ),
                    value = template(
                        MicroscopeTextKey.MICROSCOPE_OUTPUT_RATE_FORMAT,
                        signal,
                        MicroscopeText.number(amount),
                        resourceText(MicroscopeTextKey.CELL_PROPERTY_SIGNAL_SUBSTANCE_UNIT),
                        resourceText(MicroscopeTextKey.SIMULATION_HOUR_ABBREVIATION)
                    )
                )
            )
        }
    }

    private fun rateValue(value: Double): MicroscopeText = rateWithUnit(
        value,
        MicroscopeTextKey.CELL_PROPERTY_SIGNAL_SUBSTANCE_UNIT,
        MicroscopeTextKey.SIMULATION_HOUR_ABBREVIATION
    )

    private fun geneInteger(gene: GeneDraft?, integerIndex: Int): Int =
        gene?.integerValues?.getOrNull(integerIndex) ?: 0

    private fun cellValue(gene: GeneDraft?, cell: CellSnapshot, valueIndex: Int): Float {
        val parametricValue = gene?.parametricValues?.getOrNull(valueIndex) ?: return 0.0f
        val formulaType = parametricValue.formulaType.toInt()
        val normalized = if (formulaType == 0) {
            parametricValue.variableB
        } else {
            val signalIndex = parametricValue.signalSubstanceChannelIndex.toInt()
            val input = when (signalIndex) {
                in 0 until CellDataConstants.SIGNAL_SUBSTANCE_COUNT ->
                    cell.signalSubstance.getOrElse(signalIndex) { 0f }
                CellDataConstants.SIGNAL_SUBSTANCE_COUNT ->
                    (cell.mass / CellDataConstants.MAX_MASS).toFloat()
                CellDataConstants.SIGNAL_SUBSTANCE_COUNT + 1 ->
                    (cell.age / CellDataConstants.MAX_AGE).toFloat()
                CellDataConstants.SIGNAL_SUBSTANCE_COUNT + 2 -> cell.nitrates.toFloat()
                else -> 0.0f
            }
            when (formulaType) {
                1 -> input * parametricValue.variableA + parametricValue.variableB
                2 -> if (input < parametricValue.variableC) {
                    parametricValue.variableA
                } else {
                    parametricValue.variableB
                }
                else -> parametricValue.variableA *
                    sin(input * parametricValue.variableC - parametricValue.variableB)
            }
        }.coerceIn(-1.0f, 1.0f)
        val minimum = GenomeContract.parametricMinimums[valueIndex]
        val maximum = GenomeContract.parametricMaximums[valueIndex]
        return minimum + (maximum - minimum) * 0.5f * (normalized + 1.0f)
    }

    private fun cellSpeed(cell: CellSnapshot): Double =
        1000.0 * sqrt(cell.velocityX * cell.velocityX + cell.velocityY * cell.velocityY)

    private fun coloredModeValue(geneIndex: Int, color: List<Float>): MicroscopeText =
        MicroscopeText(
            listOf(
                MicroscopeTextPart.Template(
                    key = MicroscopeTextKey.MICROSCOPE_GENE_NAME_FORMAT,
                    arguments = listOf(integer(geneIndex + 1)),
                    color = MicroscopeTextColor(
                        red = color.getOrElse(0) { 0f }.coerceIn(0f, 1f),
                        green = color.getOrElse(1) { 0f }.coerceIn(0f, 1f),
                        blue = color.getOrElse(2) { 0f }.coerceIn(0f, 1f)
                    )
                )
            )
        )

    private fun geneName(index: Int): MicroscopeText =
        template(MicroscopeTextKey.MICROSCOPE_GENE_NAME_FORMAT, integer(index + 1))

    private fun signalName(index: Int): MicroscopeText = if (
        index in 0 until CellDataConstants.SIGNAL_SUBSTANCE_COUNT
    ) {
        template(
            MicroscopeTextKey.MICROSCOPE_SIGNAL_NAME_FORMAT,
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SIGNAL_SUBSTANCE_PREFIX),
            integer(index + 1)
        )
    } else {
        integer(index)
    }

    private fun originTagName(originTag: Int): MicroscopeText = when (originTag) {
        SimulationContract.USER_CELL_ORIGIN ->
            resourceText(MicroscopeTextKey.MICROSCOPE_CELL_TAG_USER)
        SimulationContract.LEVEL_CELL_ORIGIN ->
            resourceText(MicroscopeTextKey.MICROSCOPE_CELL_TAG_CHALLENGE)
        SimulationContract.RANDOM_CELL_ORIGIN ->
            resourceText(MicroscopeTextKey.MICROSCOPE_CELL_TAG_CONTAMINATION)
        SimulationContract.INFECTED_USER_CELL_ORIGIN ->
            resourceText(MicroscopeTextKey.MICROSCOPE_CELL_TAG_INFECTED_USER)
        SimulationContract.INFECTED_LEVEL_CELL_ORIGIN ->
            resourceText(MicroscopeTextKey.MICROSCOPE_CELL_TAG_INFECTED_CHALLENGE)
        SimulationContract.INFECTED_RANDOM_CELL_ORIGIN ->
            resourceText(MicroscopeTextKey.MICROSCOPE_CELL_TAG_INFECTED_CONTAMINATION)
        SimulationContract.HYBRID_CELL_ORIGIN ->
            resourceText(MicroscopeTextKey.MICROSCOPE_CELL_TAG_HYBRID)
        else -> integer(originTag)
    }

    private fun secreteTypeName(type: Int): MicroscopeText = when {
        type == GenomeContract.SECRETE_TYPE_CYANIDE ->
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SECRETION_HCN)
        type == GenomeContract.SECRETE_TYPE_FOOD_SMELL ->
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SECRETION_NUTRIENT_SMELL)
        type == GenomeContract.SECRETE_TYPE_COATED_FOOD_SMELL ->
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SECRETION_COATED_NUTRIENT_SMELL)
        type == GenomeContract.SECRETE_TYPE_LIPASE ->
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SECRETION_LIPASE)
        type == GenomeContract.SECRETE_TYPE_PROTEASE ->
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SECRETION_PROTEASE)
        type == GenomeContract.SECRETE_TYPE_LIGHT_SMELL ->
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SECRETION_LIGHT_SENSE_ACTIVATOR)
        type == GenomeContract.SECRETE_TYPE_WALL_SMELL ->
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SECRETION_WALL_SENSE_ACTIVATOR)
        type >= GenomeContract.SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT &&
            type < CellDataConstants.SIGNAL_SUBSTANCE_COUNT +
            GenomeContract.SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT -> template(
            MicroscopeTextKey.MICROSCOPE_SIGNED_SIGNAL_NAME_FORMAT,
            MicroscopeText.literal("-"),
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SIGNAL_SUBSTANCE_PREFIX),
            integer(1 + type - GenomeContract.SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT)
        )
        type >= CellDataConstants.SIGNAL_SUBSTANCE_COUNT +
            GenomeContract.SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT -> template(
            MicroscopeTextKey.MICROSCOPE_SIGNED_SIGNAL_NAME_FORMAT,
            MicroscopeText.literal("+"),
            resourceText(MicroscopeTextKey.CELL_PROPERTY_SIGNAL_SUBSTANCE_PREFIX),
            integer(
                1 + type - GenomeContract.SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT -
                    CellDataConstants.SIGNAL_SUBSTANCE_COUNT
            )
        )
        else -> integer(type)
    }

    private fun smellTypeName(type: Int): MicroscopeText {
        val key = when (type) {
            CellDataConstants.SMELL_TYPE_CELL -> MicroscopeTextKey.CELL_PROPERTY_SENSE_CELL
            CellDataConstants.SMELL_TYPE_WALL -> MicroscopeTextKey.CELL_PROPERTY_SENSE_WALL
            CellDataConstants.SMELL_TYPE_LIGHT -> MicroscopeTextKey.CELL_PROPERTY_SENSE_LIGHT
            CellDataConstants.SMELL_TYPE_FOOD -> MicroscopeTextKey.CELL_PROPERTY_SENSE_NUTRIENT
            CellDataConstants.SMELL_TYPE_FAT -> MicroscopeTextKey.CELL_PROPERTY_SENSE_COATED_NUTRIENT
            CellDataConstants.SMELL_TYPE_VELOCITY -> MicroscopeTextKey.CELL_PROPERTY_SENSE_VELOCITY
            else -> return integer(type)
        }
        return resourceText(key)
    }

    private fun section(
        title: MicroscopeTextKey,
        items: List<MicroscopeDiagnosisItem>
    ): MicroscopeDiagnosisSection = MicroscopeDiagnosisSection(
        MicroscopeText.resource(title),
        items.toList()
    )

    private fun item(
        label: MicroscopeTextKey,
        value: MicroscopeText
    ): MicroscopeDiagnosisItem = MicroscopeDiagnosisItem(
        MicroscopeText.resource(label),
        value
    )

    private fun value(value: Number): MicroscopeText = number(value)

    private fun valueWithUnit(value: Number, unit: MicroscopeTextKey): MicroscopeText =
        template(
            MicroscopeTextKey.MICROSCOPE_VALUE_WITH_UNIT_FORMAT,
            number(value),
            resourceText(unit)
        )

    private fun rateWithUnit(
        value: Number,
        unit: MicroscopeTextKey,
        denominator: MicroscopeTextKey
    ): MicroscopeText = template(
        MicroscopeTextKey.MICROSCOPE_RATE_WITH_UNIT_FORMAT,
        number(value),
        resourceText(unit),
        resourceText(denominator)
    )

    private fun percentValue(value: Number): MicroscopeText = template(
        MicroscopeTextKey.MICROSCOPE_PERCENT_VALUE_FORMAT,
        number(value)
    )

    private fun number(value: Number): MicroscopeText =
        MicroscopeText.number(MicroscopeTextFormatter.significant(value.toDouble()))

    private fun integer(value: Int): MicroscopeText = MicroscopeText.number(value.toString())

    private fun template(
        key: MicroscopeTextKey,
        vararg arguments: MicroscopeText
    ): MicroscopeText = MicroscopeText.template(key, *arguments)

    private fun resourceText(key: MicroscopeTextKey): MicroscopeText =
        MicroscopeText.resource(key)

    private fun concat(vararg texts: MicroscopeText): MicroscopeText =
        MicroscopeText(texts.flatMap(MicroscopeText::parts))
}
