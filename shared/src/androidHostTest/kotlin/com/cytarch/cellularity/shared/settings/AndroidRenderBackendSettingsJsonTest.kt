package com.cytarch.cellularity.shared.settings

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs

class AndroidRenderBackendSettingsJsonTest {
    @Test
    fun roundTripPreservesEveryPreference() {
        AndroidRenderBackendPreference.entries.forEach { backend ->
            val expected = AndroidRenderBackendSettings(backend)
            val decoded = AndroidRenderBackendSettingsJson.decode(
                AndroidRenderBackendSettingsJson.encode(expected)
            )

            assertEquals(expected, assertIs<SettingsResult.Success<*>>(decoded).value)
        }
    }

    @Test
    fun missingBackendUsesStableGlesDefaultAndUnknownFieldsAreIgnored() {
        val decoded = AndroidRenderBackendSettingsJson.decode(
            """
            {
              "schemaVersion": 1,
              "settings": { "futureOption": true },
              "futureDocumentField": 42
            }
            """.trimIndent()
        )

        assertEquals(
            AndroidRenderBackendSettings.Default,
            assertIs<SettingsResult.Success<*>>(decoded).value
        )
    }

    @Test
    fun malformedBackendTypeIsRejected() {
        val decoded = AndroidRenderBackendSettingsJson.decode(
            """{"schemaVersion":1,"settings":{"backend":7}}"""
        )

        assertEquals(
            SettingsErrorCode.MALFORMED,
            assertIs<SettingsResult.Failure>(decoded).error.code
        )
    }

    @Test
    fun unsupportedSchemaIsRejected() {
        val decoded = AndroidRenderBackendSettingsJson.decode(
            """{"schemaVersion":2,"settings":{"backend":"auto"}}"""
        )

        assertEquals(
            SettingsErrorCode.UNSUPPORTED_SCHEMA,
            assertIs<SettingsResult.Failure>(decoded).error.code
        )
    }
}
