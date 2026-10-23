package com.cytarch.cellularity.shared.settings

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.coroutines.flow.StateFlow
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonObject

@Serializable
data class CellularityThemeColors(
    val name: String = "Cellularity",
    val primary: String = "#68CC4B",
    val secondary: String = "#03DAC6",
    val primaryContainer: String = "#303030",
    val background: String = "#505050",
    val surfaceVariant: String = "#3A3A3A",
    val onSurfaceVariant: String = "#D3D3D3",
    val textFieldText: String = "#45B436"
) {
    fun normalized(): CellularityThemeColors = copy(
        primary = normalizeHexColor(primary, Default.primary),
        secondary = normalizeHexColor(secondary, Default.secondary),
        primaryContainer = normalizeHexColor(primaryContainer, Default.primaryContainer),
        background = normalizeHexColor(background, Default.background),
        surfaceVariant = normalizeHexColor(surfaceVariant, Default.surfaceVariant),
        onSurfaceVariant = normalizeHexColor(onSurfaceVariant, Default.onSurfaceVariant),
        textFieldText = normalizeHexColor(textFieldText, Default.textFieldText)
    )

    companion object {
        val Default = CellularityThemeColors()
    }
}

@Serializable
data class CellularityThemeComponents(
    val cardCorner: Float = 8f,
    val cardElevation: Float = 2f,
    val itemSpacing: Float = 8f,
    val contentPadding: Float = 12f,
    val listPadding: Float = 10f,
    val iconButtonSize: Float = 40f,
    val sliderThumbSize: Float = 16f,
    val sliderTrackHeight: Float = 4f,
    val swatchSize: Float = 24f
) {
    fun normalized(): CellularityThemeComponents = copy(
        cardCorner = cardCorner.normalized(0f, 32f, Default.cardCorner),
        cardElevation = cardElevation.normalized(0f, 16f, Default.cardElevation),
        itemSpacing = itemSpacing.normalized(0f, 32f, Default.itemSpacing),
        contentPadding = contentPadding.normalized(0f, 48f, Default.contentPadding),
        listPadding = listPadding.normalized(0f, 48f, Default.listPadding),
        iconButtonSize = iconButtonSize.normalized(24f, 72f, Default.iconButtonSize),
        sliderThumbSize = sliderThumbSize.normalized(8f, 36f, Default.sliderThumbSize),
        sliderTrackHeight = sliderTrackHeight.normalized(1f, 16f, Default.sliderTrackHeight),
        swatchSize = swatchSize.normalized(12f, 48f, Default.swatchSize)
    )

    companion object {
        val Default = CellularityThemeComponents()
    }
}

@Serializable
data class CellularityThemeFile(
    val format: String = CLTHEME_FORMAT,
    val version: Int = CLTHEME_VERSION,
    val theme: CellularityThemeColors = CellularityThemeColors.Default,
    val components: CellularityThemeComponents = CellularityThemeComponents.Default
) {
    companion object {
        const val CLTHEME_FORMAT = "cltheme"
        const val CLTHEME_VERSION = 2
    }
}

data class CellularityThemePreset(
    val colors: CellularityThemeColors,
    val components: CellularityThemeComponents
)

@Serializable
enum class CellularityLanguage(val languageTag: String) {
    @SerialName("en")
    ENGLISH("en"),

    @SerialName("es")
    SPANISH("es"),

    @SerialName("ko")
    KOREAN("ko"),

    @SerialName("pl")
    POLISH("pl"),

    @SerialName("pt")
    PORTUGUESE("pt"),

    @SerialName("ru")
    RUSSIAN("ru");

    companion object {
        fun fromLanguageTag(languageTag: String?): CellularityLanguage? {
            val normalizedTag = languageTag?.trim()?.lowercase()
            return entries.firstOrNull { it.languageTag == normalizedTag }
        }
    }
}

@Serializable
enum class ExperimentSortField {
    @SerialName("name")
    NAME,

    @SerialName("created")
    CREATED,

    @SerialName("age")
    AGE,

    @SerialName("cell_count")
    CELL_COUNT,

    @SerialName("maximum_cell_count")
    MAXIMUM_CELL_COUNT,

    @SerialName("food_count")
    FOOD_COUNT,

    @SerialName("maximum_food_count")
    MAXIMUM_FOOD_COUNT,

    @SerialName("food_mass")
    FOOD_MASS,

    @SerialName("dish_size")
    DISH_SIZE
}

@Serializable
enum class GeneBankSortField {
    @SerialName("name")
    NAME,

    @SerialName("gene_count")
    GENE_COUNT,

    @SerialName("unique_gene_count")
    UNIQUE_GENE_COUNT,

    @SerialName("initial_gene")
    INITIAL_GENE,

    @SerialName("cell_type_count")
    CELL_TYPE_COUNT
}

@Serializable
enum class CollectionSortDirection {
    @SerialName("ascending")
    ASCENDING,

    @SerialName("descending")
    DESCENDING
}

@Serializable
enum class CollectionLayoutMode {
    @SerialName("grid")
    GRID,

    @SerialName("list")
    LIST
}

@Serializable
data class ExperimentCollectionPreferences(
    val sortField: ExperimentSortField = ExperimentSortField.NAME,
    val sortDirection: CollectionSortDirection = CollectionSortDirection.ASCENDING,
    val layoutMode: CollectionLayoutMode = CollectionLayoutMode.GRID
) {
    companion object {
        val Default = ExperimentCollectionPreferences()
    }
}

@Serializable
data class GeneBankCollectionPreferences(
    val sortField: GeneBankSortField = GeneBankSortField.NAME,
    val sortDirection: CollectionSortDirection = CollectionSortDirection.ASCENDING,
    val layoutMode: CollectionLayoutMode = CollectionLayoutMode.GRID
) {
    companion object {
        val Default = GeneBankCollectionPreferences()
    }
}

@Serializable
data class CellularitySettings(
    val genomePreviewMaxCells: Int = DefaultGenomePreviewMaxCells,
    val incubateTargetUps: Double = DefaultIncubateTargetUps,
    val performanceHudEnabled: Boolean = false,
    val language: CellularityLanguage = CellularityLanguage.ENGLISH,
    val keepOn: Boolean = false,
    val askSterilize: Boolean = false,
    val experimentCollection: ExperimentCollectionPreferences =
        ExperimentCollectionPreferences.Default,
    val geneBankCollection: GeneBankCollectionPreferences =
        GeneBankCollectionPreferences.Default,
    val theme: CellularityThemeColors = CellularityThemeColors.Default,
    val themeComponents: CellularityThemeComponents = CellularityThemeComponents.Default
) {
    fun normalized(): CellularitySettings = copy(
        genomePreviewMaxCells = genomePreviewMaxCells.coerceIn(
            MinGenomePreviewMaxCells,
            MaxGenomePreviewMaxCells
        ),
        incubateTargetUps = incubateTargetUps.normalized(
            MinIncubateTargetUps,
            MaxIncubateTargetUps,
            DefaultIncubateTargetUps
        ),
        theme = theme.normalized(),
        themeComponents = themeComponents.normalized()
    )

    companion object {
        const val MinGenomePreviewMaxCells = 1
        const val MaxGenomePreviewMaxCells = 512
        const val DefaultGenomePreviewMaxCells = 64
        const val MinIncubateTargetUps = 1.0
        const val MaxIncubateTargetUps = 10_000.0
        const val DefaultIncubateTargetUps = 1700.0
        val Default = CellularitySettings()
    }
}

interface CellularitySettingsRepository {
    val settings: StateFlow<CellularitySettings>
    val recovery: StateFlow<SettingsRecoveryState?>

    /** Loads the process-owned snapshot. Calling this more than once is idempotent. */
    suspend fun initialize(): SettingsResult<CellularitySettings>

    /** Publishes [settings] only after its normalized value is durably committed. */
    suspend fun writeSettings(settings: CellularitySettings): SettingsResult<CellularitySettings>

    /** Explicitly replaces an unreadable document without requiring a recovery copy. */
    suspend fun resetSettings(): SettingsResult<CellularitySettings>
}

enum class SettingsErrorCode {
    MALFORMED,
    UNSUPPORTED_SCHEMA,
    TOO_LARGE,
    IO_FAILURE
}

data class SettingsError(
    val code: SettingsErrorCode,
    val detail: String = ""
)

sealed interface SettingsResult<out T> {
    data class Success<T>(val value: T) : SettingsResult<T>
    data class Failure(val error: SettingsError) : SettingsResult<Nothing>
}

data class SettingsRecoveryState(
    val error: SettingsError,
    val recoverableCopyAvailable: Boolean
)

object CellularitySettingsSchema {
    const val CURRENT_VERSION = 1
}

@Serializable
private data class CellularitySettingsDocument(
    val schemaVersion: Int,
    val settings: CellularitySettings
)

object CellularitySettingsJson {
    private val json = Json {
        encodeDefaults = true
        ignoreUnknownKeys = true
        prettyPrint = true
    }

    fun encode(settings: CellularitySettings): String {
        return json.encodeToString(
            CellularitySettingsDocument.serializer(),
            CellularitySettingsDocument(
                schemaVersion = CellularitySettingsSchema.CURRENT_VERSION,
                settings = settings.normalized()
            )
        )
    }

    fun decode(jsonText: String): SettingsResult<CellularitySettings> {
        val root = try {
            json.parseToJsonElement(jsonText).jsonObject
        } catch (failure: Exception) {
            return malformed(failure.message)
        }
        val schemaPrimitive = root["schemaVersion"] as? JsonPrimitive
            ?: return malformed("Missing settings schema version")
        if (schemaPrimitive.isString) {
            return malformed("Settings schema version must be an integer")
        }
        val schemaVersion = schemaPrimitive.intOrNull
            ?: return malformed("Settings schema version must be an integer")
        if (schemaVersion != CellularitySettingsSchema.CURRENT_VERSION) {
            return SettingsResult.Failure(
                SettingsError(
                    SettingsErrorCode.UNSUPPORTED_SCHEMA,
                    "Unsupported settings schema version: $schemaVersion"
                )
            )
        }
        val settingsObject = root["settings"] as? JsonObject
            ?: return malformed("Missing settings object")
        validateCurrentFieldTypes(settingsObject)?.let { detail ->
            return malformed(detail)
        }

        return try {
            val document = json.decodeFromJsonElement(
                CellularitySettingsDocument.serializer(),
                root
            )
            SettingsResult.Success(document.settings.normalized())
        } catch (failure: Exception) {
            malformed(failure.message)
        }
    }

    private fun malformed(detail: String?): SettingsResult.Failure = SettingsResult.Failure(
        SettingsError(SettingsErrorCode.MALFORMED, detail.orEmpty())
    )

    /** Rejects coercions that existed only for private pre-release settings documents. */
    private fun validateCurrentFieldTypes(settings: JsonObject): String? {
        val numericKeys = listOf("genomePreviewMaxCells", "incubateTargetUps")
        val booleanKeys = listOf("performanceHudEnabled", "keepOn", "askSterilize")
        (numericKeys + booleanKeys).forEach { key ->
            val value = settings[key] ?: return@forEach
            if (value is JsonPrimitive && value.isString) {
                return "Settings field $key has a pre-release string representation"
            }
        }
        for (key in listOf("language")) {
            val value = settings[key] ?: continue
            if (value !is JsonPrimitive || !value.isString) {
                return "Settings field $key must be a string"
            }
        }
        for (key in listOf("experimentCollection", "geneBankCollection")) {
            val value = settings[key] ?: continue
            val collection = value as? JsonObject
                ?: return "Settings $key must be an object"
            for (optionKey in listOf("sortField", "sortDirection", "layoutMode")) {
                val option = collection[optionKey] ?: continue
                if (option !is JsonPrimitive || !option.isString) {
                    return "Collection preferences must be strings"
                }
            }
        }
        settings["theme"]?.let { value ->
            val theme = value as? JsonObject ?: return "Settings theme must be an object"
            listOf(
                "name",
                "primary",
                "secondary",
                "primaryContainer",
                "background",
                "surfaceVariant",
                "onSurfaceVariant",
                "textFieldText"
            ).mapNotNull(theme::get).forEach { color ->
                if (color !is JsonPrimitive || !color.isString) {
                    return "Theme values must be strings"
                }
            }
        }
        settings["themeComponents"]?.let { value ->
            val components = value as? JsonObject
                ?: return "Settings themeComponents must be an object"
            listOf(
                "cardCorner",
                "cardElevation",
                "itemSpacing",
                "contentPadding",
                "listPadding",
                "iconButtonSize",
                "sliderThumbSize",
                "sliderTrackHeight",
                "swatchSize"
            ).mapNotNull(components::get).forEach { component ->
                if (component is JsonPrimitive && component.isString) {
                    return "Theme component values must be numbers"
                }
            }
        }
        return null
    }
}

object CellularityThemeFileJson {
    private val json = Json {
        encodeDefaults = true
        ignoreUnknownKeys = true
        prettyPrint = true
    }

    fun encode(theme: CellularityThemeColors, components: CellularityThemeComponents): String {
        return json.encodeToString(
            CellularityThemeFile.serializer(),
            CellularityThemeFile(
                theme = theme.normalized(),
                components = components.normalized()
            )
        )
    }

    fun decode(jsonText: String): CellularityThemePreset {
        val file = json.decodeFromString(CellularityThemeFile.serializer(), jsonText)
        require(file.format == CellularityThemeFile.CLTHEME_FORMAT) { "Unsupported theme format: ${file.format}" }
        require(file.version in 1..CellularityThemeFile.CLTHEME_VERSION) { "Unsupported theme version: ${file.version}" }
        return CellularityThemePreset(
            colors = file.theme.normalized(),
            components = file.components.normalized()
        )
    }
}

fun normalizeHexColor(value: String, fallback: String): String {
    val trimmed = value.trim()
    val hex = when {
        trimmed.matches(Regex("^#[0-9a-fA-F]{6}$")) -> trimmed.drop(1)
        trimmed.matches(Regex("^[0-9a-fA-F]{6}$")) -> trimmed
        trimmed.matches(Regex("^#[0-9a-fA-F]{8}$")) -> trimmed.drop(3)
        trimmed.matches(Regex("^[0-9a-fA-F]{8}$")) -> trimmed.drop(2)
        else -> return fallback
    }
    return "#${hex.uppercase()}"
}

private fun Float.normalized(minimum: Float, maximum: Float, fallback: Float): Float {
    return if (isFinite()) coerceIn(minimum, maximum) else fallback
}

private fun Double.normalized(minimum: Double, maximum: Double, fallback: Double): Double {
    return if (isFinite()) coerceIn(minimum, maximum) else fallback
}
