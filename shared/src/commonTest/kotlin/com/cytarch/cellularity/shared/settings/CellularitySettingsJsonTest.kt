package com.cytarch.cellularity.shared.settings

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.boolean
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class CellularitySettingsJsonTest {
    @Test
    fun defaultsAreTypedAndWithinSupportedRanges() {
        val defaults = CellularitySettings.Default

        assertEquals(64, defaults.genomePreviewMaxCells)
        assertEquals(1700.0, defaults.incubateTargetUps)
        assertFalse(defaults.performanceHudEnabled)
        assertEquals(CellularityLanguage.ENGLISH, defaults.language)
        assertEquals("#45B436", defaults.theme.textFieldText)
        assertEquals(
            ExperimentCollectionPreferences.Default,
            defaults.experimentCollection
        )
        assertEquals(
            GeneBankCollectionPreferences.Default,
            defaults.geneBankCollection
        )
        assertTrue(defaults.genomePreviewMaxCells in
            CellularitySettings.MinGenomePreviewMaxCells..CellularitySettings.MaxGenomePreviewMaxCells)
        assertTrue(defaults.incubateTargetUps in
            CellularitySettings.MinIncubateTargetUps..CellularitySettings.MaxIncubateTargetUps)
    }

    @Test
    fun currentSchemaRoundTripsAndIgnoresUnknownCurrentSchemaFields() {
        val expected = CellularitySettings(
            genomePreviewMaxCells = 128,
            incubateTargetUps = 2450.5,
            performanceHudEnabled = true,
            language = CellularityLanguage.RUSSIAN,
            keepOn = true,
            askSterilize = true,
            experimentCollection = ExperimentCollectionPreferences(
                sortField = ExperimentSortField.FOOD_MASS,
                sortDirection = CollectionSortDirection.DESCENDING,
                layoutMode = CollectionLayoutMode.LIST
            ),
            geneBankCollection = GeneBankCollectionPreferences(
                sortField = GeneBankSortField.UNIQUE_GENE_COUNT,
                sortDirection = CollectionSortDirection.DESCENDING,
                layoutMode = CollectionLayoutMode.LIST
            ),
            theme = CellularityThemeColors(name = "Saved", primary = "#123456"),
            themeComponents = CellularityThemeComponents(cardCorner = 14f)
        ).normalized()
        val encoded = CellularitySettingsJson.encode(expected)
        val rootClosingBrace = encoded.lastIndexOf('}')
        val withUnknownField = encoded.substring(0, rootClosingBrace) +
            ",\n  \"futureSetting\": true\n}"

        assertEquals(expected, success(CellularitySettingsJson.decode(encoded)))
        assertEquals(expected, success(CellularitySettingsJson.decode(withUnknownField)))
    }

    @Test
    fun malformedAndPreReleaseDocumentsReturnTypedFailures() {
        listOf(
            "{broken",
            "[]",
            "{}",
            """{"genomePreviewMaxCells":128}""",
            """{"schemaVersion":"1","settings":{}}""",
            """{"schemaVersion":1,"settings":{"genomePreviewMaxCells":"128"}}""",
            """{"schemaVersion":1,"settings":{"performanceHudEnabled":"true"}}""",
            """{"schemaVersion":1,"settings":{"experimentCollection":"grid"}}""",
            """{"schemaVersion":1,"settings":{"geneBankCollection":[]}}""",
            """{"schemaVersion":1,"settings":{"theme":{"textFieldText":42}}}"""
        ).forEach { document ->
            val failure = assertIs<SettingsResult.Failure>(
                CellularitySettingsJson.decode(document)
            )
            assertEquals(SettingsErrorCode.MALFORMED, failure.error.code)
        }
    }

    @Test
    fun unsupportedSchemaIsDistinctFromMalformedJson() {
        val failure = assertIs<SettingsResult.Failure>(
            CellularitySettingsJson.decode(
                """{"schemaVersion":2,"settings":{}}"""
            )
        )

        assertEquals(SettingsErrorCode.UNSUPPORTED_SCHEMA, failure.error.code)
    }

    @Test
    fun decodeAndDirectUpdatesNormalizeNumericBounds() {
        val decoded = success(
            CellularitySettingsJson.decode(
                """
                {
                  "schemaVersion": 1,
                  "settings": {
                    "genomePreviewMaxCells": 999999,
                    "incubateTargetUps": -2
                  }
                }
                """.trimIndent()
            )
        )
        assertEquals(CellularitySettings.MaxGenomePreviewMaxCells, decoded.genomePreviewMaxCells)
        assertEquals(CellularitySettings.MinIncubateTargetUps, decoded.incubateTargetUps)

        val normalized = CellularitySettings(
            genomePreviewMaxCells = -10,
            incubateTargetUps = Double.POSITIVE_INFINITY,
            themeComponents = CellularityThemeComponents(cardCorner = Float.NaN)
        ).normalized()
        assertEquals(CellularitySettings.MinGenomePreviewMaxCells, normalized.genomePreviewMaxCells)
        assertEquals(CellularitySettings.DefaultIncubateTargetUps, normalized.incubateTargetUps)
        assertEquals(CellularityThemeComponents.Default.cardCorner, normalized.themeComponents.cardCorner)
    }

    @Test
    fun textFieldTextColorNormalizesPersistsAndDefaultsWhenAbsent() {
        val expected = CellularitySettings(
            theme = CellularityThemeColors(textFieldText = "12abEF")
        ).normalized()
        assertEquals("#12ABEF", expected.theme.textFieldText)
        assertEquals(
            expected,
            success(
                CellularitySettingsJson.decode(
                    CellularitySettingsJson.encode(expected)
                )
            )
        )

        val withoutTextFieldColor = success(
            CellularitySettingsJson.decode(
                """{"schemaVersion":1,"settings":{"theme":{"name":"Existing"}}}"""
            )
        )
        assertEquals(
            CellularityThemeColors.Default.textFieldText,
            withoutTextFieldColor.theme.textFieldText
        )
        assertEquals(
            CellularityThemeColors.Default.textFieldText,
            CellularityThemeColors(textFieldText = "invalid")
                .normalized()
                .textFieldText
        )
    }

    @Test
    fun encodedDocumentDeclaresSchemaAndDoesNotWriteRetiredFields() {
        val root = Json.parseToJsonElement(
            CellularitySettingsJson.encode(CellularitySettings.Default)
        ).jsonObject
        val settings = root.getValue("settings").jsonObject

        assertEquals(
            CellularitySettingsSchema.CURRENT_VERSION,
            root.getValue("schemaVersion").jsonPrimitive.int
        )
        assertEquals(64, settings.getValue("genomePreviewMaxCells").jsonPrimitive.int)
        assertFalse(settings.getValue("genomePreviewMaxCells").jsonPrimitive.isString)
        assertFalse(settings.getValue("incubateTargetUps").jsonPrimitive.isString)
        assertFalse(settings.getValue("performanceHudEnabled").jsonPrimitive.boolean)
        assertEquals("en", settings.getValue("language").jsonPrimitive.content)
        val experimentCollection = settings.getValue("experimentCollection").jsonObject
        assertEquals(
            "name",
            experimentCollection.getValue("sortField").jsonPrimitive.content
        )
        assertEquals(
            "ascending",
            experimentCollection.getValue("sortDirection").jsonPrimitive.content
        )
        assertEquals(
            "grid",
            experimentCollection.getValue("layoutMode").jsonPrimitive.content
        )
        val geneBankCollection = settings.getValue("geneBankCollection").jsonObject
        assertEquals(
            "name",
            geneBankCollection.getValue("sortField").jsonPrimitive.content
        )
        assertEquals(
            "ascending",
            geneBankCollection.getValue("sortDirection").jsonPrimitive.content
        )
        assertEquals(
            "grid",
            geneBankCollection.getValue("layoutMode").jsonPrimitive.content
        )
        assertFalse("askDiscard" in settings)
        assertFalse("avoidCameraCutout" in settings)
        assertFalse("lockPreview" in settings)
        assertFalse("aaMode" in settings)
    }

    @Test
    fun everyDisplayedCommonSettingSurvivesPersistence() {
        val expected = CellularitySettings(
            genomePreviewMaxCells = 96,
            incubateTargetUps = 3200.25,
            performanceHudEnabled = true,
            language = CellularityLanguage.PORTUGUESE,
            keepOn = true,
            askSterilize = true,
            experimentCollection = ExperimentCollectionPreferences(
                sortField = ExperimentSortField.CREATED,
                sortDirection = CollectionSortDirection.DESCENDING,
                layoutMode = CollectionLayoutMode.LIST
            ),
            geneBankCollection = GeneBankCollectionPreferences(
                sortField = GeneBankSortField.CELL_TYPE_COUNT,
                sortDirection = CollectionSortDirection.ASCENDING,
                layoutMode = CollectionLayoutMode.LIST
            ),
            theme = CellularityThemeColors(
                name = "Custom",
                primary = "#ABCDEF"
            ),
            themeComponents = CellularityThemeComponents(cardCorner = 12f)
        ).normalized()

        assertEquals(
            expected,
            success(CellularitySettingsJson.decode(CellularitySettingsJson.encode(expected)))
        )
    }

    private fun success(result: SettingsResult<CellularitySettings>): CellularitySettings =
        assertIs<SettingsResult.Success<CellularitySettings>>(result).value
}
