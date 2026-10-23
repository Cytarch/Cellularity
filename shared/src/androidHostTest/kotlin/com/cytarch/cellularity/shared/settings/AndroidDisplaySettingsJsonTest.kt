package com.cytarch.cellularity.shared.settings

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs

class AndroidDisplaySettingsJsonTest {
    @Test
    fun platformSettingRoundTripsInItsOwnDocument() {
        val expected = AndroidDisplaySettings(avoidCameraCutout = true)

        val decoded = assertIs<SettingsResult.Success<AndroidDisplaySettings>>(
            AndroidDisplaySettingsJson.decode(AndroidDisplaySettingsJson.encode(expected))
        )
        assertEquals(expected, decoded.value)
    }

    @Test
    fun malformedAndPreReleasePlatformDocumentsAreRejected() {
        listOf(
            "{broken",
            "[]",
            """{"avoidCameraCutout":true}"""
        ).forEach { document ->
            val failure = assertIs<SettingsResult.Failure>(
                AndroidDisplaySettingsJson.decode(document)
            )
            assertEquals(SettingsErrorCode.MALFORMED, failure.error.code)
        }
    }

    @Test
    fun newerPlatformSchemaIsReportedSeparately() {
        val failure = assertIs<SettingsResult.Failure>(
            AndroidDisplaySettingsJson.decode(
                """{"schemaVersion":2,"settings":{}}"""
            )
        )
        assertEquals(SettingsErrorCode.UNSUPPORTED_SCHEMA, failure.error.code)
    }
}
