package com.cytarch.cellularity.shared.ui.play.microscope

import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.CellInspectionSnapshot
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.GenomeDraft
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class MicroscopeStateTest {
    @Test
    fun everyMicroscopeTextKeyHasAnExistingPresentationResource() {
        val descriptors = MicroscopeTextKey.entries.map { it.presentationDescriptor() }

        assertEquals(MicroscopeTextKey.entries, descriptors.map { it.key })
        descriptors.forEach { descriptor ->
            assertTrue(descriptor.resource.key.isNotBlank(), descriptor.key.name)
        }
    }

    @Test
    fun nativeControlValuesAreExplicitAndStable() {
        assertEquals(
            listOf(
                MicroscopeTool.CELL to 0,
                MicroscopeTool.MOVE to 1,
                MicroscopeTool.ADD to 2,
                MicroscopeTool.REMOVE to 3,
                MicroscopeTool.DIAGNOSIS to 4
            ),
            MicroscopeTool.entries.map { it to it.nativeOrdinal }
        )
        assertEquals(
            listOf(
                MicroscopeTemperature.FREEZE to 0,
                MicroscopeTemperature.OBSERVE to 1,
                MicroscopeTemperature.INCUBATE to 2,
                MicroscopeTemperature.SLOW to 3,
                MicroscopeTemperature.INCUBATE_NO_RENDER to 4
            ),
            MicroscopeTemperature.entries.map { it to it.nativeMode }
        )
        for (temperature in MicroscopeTemperature.entries) {
            assertEquals(
                temperature,
                MicroscopeTemperature.fromNativeMode(temperature.nativeMode)
            )
        }
        assertNull(MicroscopeTemperature.fromNativeMode(-1))
        assertNull(MicroscopeTemperature.fromNativeMode(5))
    }

    @Test
    fun dialogsToolAndTemperatureAreControlledByImmutableTransitions() {
        val initial = MicroscopeState()
        val toolDialog = MicroscopeReducer.reduce(
            initial,
            MicroscopeAction.ShowToolSelection
        )
        assertEquals(MicroscopeDialog.TOOL_SELECTION, toolDialog.dialog)
        assertEquals(MicroscopeTool.CELL, initial.selectedTool)

        val diagnosis = MicroscopeReducer.reduce(
            toolDialog,
            MicroscopeAction.SelectTool(MicroscopeTool.DIAGNOSIS)
        )
        assertEquals(MicroscopeTool.DIAGNOSIS, diagnosis.selectedTool)
        assertEquals(MicroscopeDialog.NONE, diagnosis.dialog)
        assertTrue(diagnosis.isDiagnosisVisible)

        val temperatureDialog = MicroscopeReducer.reduce(
            diagnosis,
            MicroscopeAction.ShowTemperatureSelection
        )
        val incubating = MicroscopeReducer.reduce(
            temperatureDialog,
            MicroscopeAction.SelectTemperature(MicroscopeTemperature.INCUBATE_NO_RENDER)
        )
        assertEquals(MicroscopeTemperature.INCUBATE_NO_RENDER, incubating.selectedTemperature)
        assertEquals(MicroscopeDialog.NONE, incubating.dialog)
        assertFalse(initial === incubating)
    }

    @Test
    fun selectionLossReplacesDiagnosticsAndStaleInspectionIsIgnoredOutsideTool() {
        val inspection = inspection(CellType.MYOCYTE)
        val store = MicroscopeStore(
            MicroscopeState(selectedTool = MicroscopeTool.DIAGNOSIS)
        )

        store.dispatch(MicroscopeAction.InspectionObserved(inspection))
        val selected = store.state.value.diagnosis
        assertTrue(selected.sections.isNotEmpty())
        assertNotEquals(MicroscopeDiagnosis.noSelection(), selected)

        store.dispatch(MicroscopeAction.InspectionObserved(null))
        assertEquals(MicroscopeDiagnosis.noSelection(), store.state.value.diagnosis)

        store.dispatch(MicroscopeAction.SelectTool(MicroscopeTool.MOVE))
        store.dispatch(MicroscopeAction.InspectionObserved(inspection))
        assertEquals(MicroscopeDiagnosis.noSelection(), store.state.value.diagnosis)
    }

    @Test
    fun diagnosisProjectionContainsOnlyTheSelectedCellTypeSection() {
        val myocyte = MicroscopeDiagnosisMapper.map(inspection(CellType.MYOCYTE))
        val specialized = myocyte.sections.last()
        assertEquals(
            MicroscopeText.cellType(CellType.MYOCYTE),
            specialized.title
        )
        assertEquals(
            listOf(
                MicroscopeTextKey.CELL_PROPERTY_MUSCLE_CONTRACTION,
                MicroscopeTextKey.CELL_PROPERTY_MUSCLE_BENDING,
                MicroscopeTextKey.CELL_PROPERTY_MUSCLE_LIFTING
            ),
            specialized.items.map { item -> item.label.singleResourceKey() }
        )

        val phagocyte = MicroscopeDiagnosisMapper.map(inspection(CellType.PHAGOCYTE))
        assertFalse(
            phagocyte.sections.any { section ->
                section.title == MicroscopeText.cellType(CellType.PHAGOCYTE)
            }
        )
    }

    @Test
    fun zoomAndSpeedLabelsAreDerivedAndInvalidSamplesAreRemoved() {
        var state = MicroscopeReducer.reduce(
            MicroscopeState(),
            MicroscopeAction.ZoomObserved(1_234.0)
        )
        val zoom = state.zoomLabel?.parts?.single() as MicroscopeTextPart.Template
        assertEquals(
            MicroscopeTextKey.MICROSCOPE_SCIENTIFIC_ZOOM_VALUE_FORMAT,
            zoom.key
        )
        assertEquals("1.23", zoom.arguments[0].singleNumber().canonicalValue)
        val exponent = zoom.arguments[1].singleNumber()
        assertEquals("3", exponent.canonicalValue)
        assertEquals(
            MicroscopeTextBaseline.SUPERSCRIPT,
            exponent.baseline
        )

        state = MicroscopeReducer.reduce(state, MicroscopeAction.ZoomObserved(null))
        assertNull(state.zoomLabel)
    }

    @Test
    fun renderPreparationFailureIsAReversibleNonTerminalUiState() {
        val failed = MicroscopeReducer.reduce(
            MicroscopeState(),
            MicroscopeAction.RenderPreparationFailureObserved(true)
        )
        val recovered = MicroscopeReducer.reduce(
            failed,
            MicroscopeAction.RenderPreparationFailureObserved(false)
        )

        assertTrue(failed.renderPreparationFailed)
        assertFalse(recovered.renderPreparationFailed)
        assertEquals(MicroscopeTemperature.FREEZE, recovered.selectedTemperature)
    }

    @Test
    fun presentationAndPointerCollectionsDefensivelyCopyCallers() {
        val textParts = mutableListOf<MicroscopeTextPart>(
            MicroscopeTextPart.Literal("stable")
        )
        val items = mutableListOf(
            MicroscopeDiagnosisItem(
                MicroscopeText.literal("label"),
                MicroscopeText.literal("value")
            )
        )
        val sections = mutableListOf(
            MicroscopeDiagnosisSection(MicroscopeText.literal("section"), items)
        )
        val pointers = mutableListOf(MicroscopePointer(1, 2f, 3f))
        val text = MicroscopeText(textParts)
        val diagnosis = MicroscopeDiagnosis(MicroscopeText.literal("title"), sections = sections)
        val event = MicroscopePointerEvent(
            MicroscopePointerAction.MOVE,
            actionPointerId = null,
            pointers = pointers,
            timestampMillis = 0
        )

        textParts.clear()
        items.clear()
        sections.clear()
        pointers.clear()

        assertEquals(listOf(MicroscopeTextPart.Literal("stable")), text.parts)
        assertEquals(1, diagnosis.sections.size)
        assertEquals(1, diagnosis.sections.single().items.size)
        assertEquals(listOf(MicroscopePointer(1, 2f, 3f)), event.pointers)
        assertFalse(text.parts is MutableList<*>)
        assertFalse(diagnosis.sections is MutableList<*>)
        assertFalse(event.pointers is MutableList<*>)
    }

    private fun inspection(cellType: CellType): CellInspectionSnapshot {
        val baseGenome = GenomeDraft.editorDefaults { listOf(0.2f, 0.4f, 0.8f) }
        val genes = baseGenome.genes.toMutableList()
        genes[0] = genes[0].copy(cellType = cellType)
        return CellInspectionSnapshot(
            cell = CellSnapshot(
                id = 7,
                positionX = 1.0,
                positionY = 2.0,
                positionZ = 0.0,
                radius = 0.02,
                age = 3.0,
                mass = 0.1,
                nitrates = 0.5,
                velocityX = 0.003,
                velocityY = 0.004,
                angle = 0.0,
                angularVelocity = 0.0,
                toxin = 0.25f,
                toxication = 0.0f,
                lipids = 0.0f,
                geneIndex = 0,
                originTag = 0,
                selectTag = 0,
                mutationCount = 2,
                telomeresCount = -1,
                mirrored = false,
                cellType = cellType,
                color = listOf(0.2f, 0.4f, 0.8f),
                signalSubstance = List(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) { 0f },
                newSignalSubstance = List(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) { 0f }
            ),
            genome = GenomeDraft(genes)
        )
    }

    private fun MicroscopeText.singleResourceKey(): MicroscopeTextKey =
        (parts.single() as MicroscopeTextPart.Resource).key

    private fun MicroscopeText.singleNumber(): MicroscopeTextPart.Number =
        parts.single() as MicroscopeTextPart.Number
}
