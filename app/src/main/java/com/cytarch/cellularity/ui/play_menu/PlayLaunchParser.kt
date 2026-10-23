package com.cytarch.cellularity.ui.play_menu

import android.content.Intent
import android.os.Bundle
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.ui.play.PlayDialog
import com.cytarch.cellularity.shared.ui.play.PlayFeedback
import com.cytarch.cellularity.shared.ui.play.PlayLaunchContract
import com.cytarch.cellularity.shared.ui.play.PlayLaunchDecodeResult
import com.cytarch.cellularity.shared.ui.play.PlayLaunchFailureReason
import com.cytarch.cellularity.shared.ui.play.PlayLaunchSource
import com.cytarch.cellularity.shared.ui.play.PlayPersistenceState
import com.cytarch.cellularity.shared.ui.play.PlayRestoredUiState
import com.cytarch.cellularity.shared.ui.play.PlayTab

internal data class ParsedPlayLaunch(
    val source: PlayLaunchSource,
    val restoredUi: PlayRestoredUiState
)

internal sealed interface PlayLaunchParseResult {
    data class Valid(val launch: ParsedPlayLaunch) : PlayLaunchParseResult

    data class Invalid(val feedback: PlayFeedback) : PlayLaunchParseResult
}

/** Android-only Intent/Bundle codec. It persists IDs and primitive UI state only. */
internal object PlayLaunchParser {
    private const val STATE_SCHEMA = "play.state.schema"
    private const val STATE_SCHEMA_VERSION = 1
    private const val SOURCE_KIND = "play.source.kind"
    private const val SOURCE_NEW = "new"
    private const val SOURCE_STORED = "stored"
    private const val SOURCE_NAME = "play.source.name"
    private const val MAX_CELLS = "play.source.maxCells"
    private const val MAX_FOODS = "play.source.maxFoods"
    private const val RADIUS = "play.source.radius"
    private const val INNER_RADIUS = "play.source.innerRadius"
    private const val SELECTED_TAB = "play.ui.selectedTab"
    private const val DIALOG = "play.ui.dialog"
    private const val SAVE_NAME = "play.ui.saveName"

    fun parse(intent: Intent, savedInstanceState: Bundle?): PlayLaunchParseResult {
        if (savedInstanceState?.containsKey(STATE_SCHEMA) == true) {
            return if (savedInstanceState.getInt(STATE_SCHEMA) == STATE_SCHEMA_VERSION) {
                parseSavedState(savedInstanceState)
            } else {
                PlayLaunchParseResult.Invalid(PlayFeedback.LOAD_FAILED)
            }
        }
        if (intent.data != null) {
            return PlayLaunchParseResult.Invalid(PlayFeedback.LOAD_FAILED)
        }
        return decode(
            stored = intent.getBooleanExtra("old", false),
            name = intent.getStringExtra("name"),
            maxCells = intent.getIntExtra(
                "maxCells",
                SimulationContract.defaultConfiguration.maxCellCount
            ),
            maxFoods = intent.getIntExtra(
                "maxFoods",
                SimulationContract.defaultConfiguration.maxFoodCount
            ),
            radius = intent.getDoubleExtra(
                "radius",
                SimulationContract.defaultConfiguration.radius
            ),
            innerRadius = intent.getDoubleExtra("innerRadius", 0.0),
            restoredUi = PlayRestoredUiState()
        )
    }

    fun save(outState: Bundle, state: PlayPersistenceState) {
        outState.putInt(STATE_SCHEMA, STATE_SCHEMA_VERSION)
        when (val source = state.source) {
            is PlayLaunchSource.New -> {
                outState.putString(SOURCE_KIND, SOURCE_NEW)
                outState.putInt(MAX_CELLS, source.configuration.maxCellCount)
                outState.putInt(MAX_FOODS, source.configuration.maxFoodCount)
                outState.putDouble(RADIUS, source.configuration.radius)
                outState.putDouble(INNER_RADIUS, source.configuration.innerRadius)
            }
            is PlayLaunchSource.Stored -> {
                outState.putString(SOURCE_KIND, SOURCE_STORED)
                outState.putString(SOURCE_NAME, source.id.value)
            }
        }
        outState.putString(SELECTED_TAB, state.ui.selectedTab.name)
        outState.putString(DIALOG, state.ui.dialog.name)
        outState.putString(SAVE_NAME, state.ui.saveName)
    }

    private fun parseSavedState(saved: Bundle): PlayLaunchParseResult {
        val kind = saved.getString(SOURCE_KIND)
            ?: return PlayLaunchParseResult.Invalid(PlayFeedback.LOAD_FAILED)
        val restoredUi = PlayRestoredUiState(
            selectedTab = saved.getString(SELECTED_TAB).toEnumOrDefault(PlayTab.SUBSTRATE),
            dialog = saved.getString(DIALOG).toEnumOrDefault(PlayDialog.NONE),
            saveName = saved.getString(SAVE_NAME).orEmpty()
        )
        return when (kind) {
            SOURCE_STORED -> decode(
                stored = true,
                name = saved.getString(SOURCE_NAME),
                maxCells = 0,
                maxFoods = 0,
                radius = 0.0,
                restoredUi = restoredUi
            )
            SOURCE_NEW -> decode(
                stored = false,
                name = null,
                maxCells = saved.getInt(MAX_CELLS, 0),
                maxFoods = saved.getInt(MAX_FOODS, -1),
                radius = saved.getDouble(RADIUS, Double.NaN),
                innerRadius = saved.getDouble(INNER_RADIUS, 0.0),
                restoredUi = restoredUi
            )
            else -> PlayLaunchParseResult.Invalid(PlayFeedback.LOAD_FAILED)
        }
    }

    private fun decode(
        stored: Boolean,
        name: String?,
        maxCells: Int,
        maxFoods: Int,
        radius: Double,
        restoredUi: PlayRestoredUiState,
        innerRadius: Double = 0.0
    ): PlayLaunchParseResult = when (val result = PlayLaunchContract.decode(
        stored = stored,
        name = name,
        maxCells = maxCells,
        maxFoods = maxFoods,
        radius = radius,
        innerRadius = innerRadius
    )) {
        is PlayLaunchDecodeResult.Valid -> PlayLaunchParseResult.Valid(
            ParsedPlayLaunch(result.source, restoredUi)
        )
        is PlayLaunchDecodeResult.Invalid -> PlayLaunchParseResult.Invalid(
            when (result.reason) {
                PlayLaunchFailureReason.BAD_STORED_ID -> PlayFeedback.LOAD_FAILED
                PlayLaunchFailureReason.UNSUPPORTED_CONFIGURATION ->
                    PlayFeedback.FILE_TOO_LARGE
            }
        )
    }

    private inline fun <reified T : Enum<T>> String?.toEnumOrDefault(default: T): T =
        this?.let { value -> enumValues<T>().firstOrNull { it.name == value } } ?: default
}
