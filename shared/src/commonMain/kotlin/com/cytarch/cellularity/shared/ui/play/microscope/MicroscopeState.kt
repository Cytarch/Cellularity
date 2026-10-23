package com.cytarch.cellularity.shared.ui.play.microscope

import com.cytarch.cellularity.shared.model.CellInspectionSnapshot
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

enum class MicroscopeTool(val nativeOrdinal: Int) {
    CELL(0),
    MOVE(1),
    ADD(2),
    REMOVE(3),
    DIAGNOSIS(4)
}

enum class MicroscopeTemperature(val nativeMode: Int) {
    FREEZE(0),
    OBSERVE(1),
    INCUBATE(2),
    SLOW(3),
    INCUBATE_NO_RENDER(4);

    companion object {
        fun fromNativeMode(mode: Int): MicroscopeTemperature? =
            entries.firstOrNull { it.nativeMode == mode }
    }
}

enum class MicroscopeDialog {
    NONE,
    TOOL_SELECTION,
    TEMPERATURE_SELECTION
}

data class MicroscopeState(
    val selectedTool: MicroscopeTool = MicroscopeTool.CELL,
    val selectedTemperature: MicroscopeTemperature = MicroscopeTemperature.FREEZE,
    val dialog: MicroscopeDialog = MicroscopeDialog.NONE,
    val zoomLabel: MicroscopeText? = null,
    val renderPreparationFailed: Boolean = false,
    val diagnosis: MicroscopeDiagnosis = MicroscopeDiagnosis.noSelection()
) {
    val isDiagnosisVisible: Boolean
        get() = selectedTool == MicroscopeTool.DIAGNOSIS
}

sealed interface MicroscopeAction {
    data object ShowToolSelection : MicroscopeAction
    data object ShowTemperatureSelection : MicroscopeAction
    data object DismissDialog : MicroscopeAction
    data class SelectTool(val tool: MicroscopeTool) : MicroscopeAction
    data class SelectTemperature(val temperature: MicroscopeTemperature) : MicroscopeAction
    data class InspectionObserved(val inspection: CellInspectionSnapshot?) : MicroscopeAction
    data class ZoomObserved(val magnification: Double?) : MicroscopeAction
    data class RenderPreparationFailureObserved(val failed: Boolean) : MicroscopeAction
}

object MicroscopeReducer {
    fun reduce(state: MicroscopeState, action: MicroscopeAction): MicroscopeState = when (action) {
        MicroscopeAction.ShowToolSelection -> state.copy(
            dialog = MicroscopeDialog.TOOL_SELECTION
        )
        MicroscopeAction.ShowTemperatureSelection -> state.copy(
            dialog = MicroscopeDialog.TEMPERATURE_SELECTION
        )
        MicroscopeAction.DismissDialog -> state.copy(dialog = MicroscopeDialog.NONE)
        is MicroscopeAction.SelectTool -> state.copy(
            selectedTool = action.tool,
            dialog = MicroscopeDialog.NONE,
            diagnosis = if (
                action.tool == MicroscopeTool.DIAGNOSIS &&
                state.selectedTool != MicroscopeTool.DIAGNOSIS
            ) {
                MicroscopeDiagnosis.noSelection()
            } else {
                state.diagnosis
            }
        )
        is MicroscopeAction.SelectTemperature -> state.copy(
            selectedTemperature = action.temperature,
            dialog = MicroscopeDialog.NONE
        )
        is MicroscopeAction.InspectionObserved -> if (state.isDiagnosisVisible) {
            state.copy(diagnosis = MicroscopeDiagnosisMapper.map(action.inspection))
        } else {
            state
        }
        is MicroscopeAction.ZoomObserved -> state.copy(
            zoomLabel = action.magnification
                ?.takeIf { it.isFinite() && it > 0.0 }
                ?.let(MicroscopeTextFormatter::zoom)
        )
        is MicroscopeAction.RenderPreparationFailureObserved -> state.copy(
            renderPreparationFailed = action.failed
        )
    }
}

/** Retained, thread-safe owner for microscope UI state. */
class MicroscopeStore(initialState: MicroscopeState = MicroscopeState()) {
    private val mutableState = MutableStateFlow(initialState)

    val state: StateFlow<MicroscopeState> = mutableState.asStateFlow()

    fun dispatch(action: MicroscopeAction) {
        mutableState.update { current -> MicroscopeReducer.reduce(current, action) }
    }
}
