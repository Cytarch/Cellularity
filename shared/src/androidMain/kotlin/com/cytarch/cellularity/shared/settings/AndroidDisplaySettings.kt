package com.cytarch.cellularity.shared.settings

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.intOrNull

@Serializable
data class AndroidDisplaySettings(
    val avoidCameraCutout: Boolean = false
) {
    companion object {
        val Default = AndroidDisplaySettings()
    }
}

object AndroidDisplaySettingsJson {
    const val CURRENT_SCHEMA_VERSION = 1

    private val json = Json {
        encodeDefaults = true
        ignoreUnknownKeys = true
        prettyPrint = true
    }

    fun encode(settings: AndroidDisplaySettings): String {
        return json.encodeToString(
            AndroidDisplaySettingsDocument.serializer(),
            AndroidDisplaySettingsDocument(
                schemaVersion = CURRENT_SCHEMA_VERSION,
                settings = settings
            )
        )
    }

    fun decode(jsonText: String): SettingsResult<AndroidDisplaySettings> {
        val root = try {
            json.parseToJsonElement(jsonText).jsonObject
        } catch (failure: Exception) {
            return malformed(failure.message)
        }
        val schemaPrimitive = root["schemaVersion"] as? JsonPrimitive
            ?: return malformed("Missing Android display settings schema version")
        if (schemaPrimitive.isString) {
            return malformed("Android display settings schema version must be an integer")
        }
        val schemaVersion = schemaPrimitive.intOrNull
            ?: return malformed("Android display settings schema version must be an integer")
        if (schemaVersion != CURRENT_SCHEMA_VERSION) {
            return SettingsResult.Failure(
                SettingsError(
                    SettingsErrorCode.UNSUPPORTED_SCHEMA,
                    "Unsupported Android display settings schema version: $schemaVersion"
                )
            )
        }
        val settingsObject = root["settings"] as? JsonObject
            ?: return malformed("Missing Android display settings object")
        val avoidCameraCutout = settingsObject["avoidCameraCutout"]
        if (avoidCameraCutout is JsonPrimitive && avoidCameraCutout.isString) {
            return malformed("Android display setting avoidCameraCutout must be a boolean")
        }

        return try {
            val document = json.decodeFromJsonElement(
                AndroidDisplaySettingsDocument.serializer(),
                root
            )
            SettingsResult.Success(document.settings)
        } catch (failure: Exception) {
            malformed(failure.message)
        }
    }

    private fun malformed(detail: String?): SettingsResult.Failure = SettingsResult.Failure(
        SettingsError(SettingsErrorCode.MALFORMED, detail.orEmpty())
    )
}

@Serializable
private data class AndroidDisplaySettingsDocument(
    val schemaVersion: Int,
    val settings: AndroidDisplaySettings
)
