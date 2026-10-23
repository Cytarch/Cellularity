package com.cytarch.cellularity.shared.settings

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonObject

@Serializable
enum class AndroidRenderBackendPreference {
    @SerialName("open_gl_es")
    OPEN_GL_ES,

    @SerialName("auto")
    AUTO,

    @SerialName("vulkan")
    VULKAN
}

@Serializable
data class AndroidRenderBackendSettings(
    val backend: AndroidRenderBackendPreference = AndroidRenderBackendPreference.OPEN_GL_ES
) {
    companion object {
        val Default = AndroidRenderBackendSettings()
    }
}

object AndroidRenderBackendSettingsJson {
    const val CURRENT_SCHEMA_VERSION = 1

    private val json = Json {
        encodeDefaults = true
        ignoreUnknownKeys = true
        prettyPrint = true
    }

    fun encode(settings: AndroidRenderBackendSettings): String {
        return json.encodeToString(
            AndroidRenderBackendSettingsDocument.serializer(),
            AndroidRenderBackendSettingsDocument(
                schemaVersion = CURRENT_SCHEMA_VERSION,
                settings = settings
            )
        )
    }

    fun decode(jsonText: String): SettingsResult<AndroidRenderBackendSettings> {
        val root = try {
            json.parseToJsonElement(jsonText).jsonObject
        } catch (failure: Exception) {
            return malformed(failure.message)
        }
        val schemaPrimitive = root["schemaVersion"] as? JsonPrimitive
            ?: return malformed("Missing Android render backend settings schema version")
        if (schemaPrimitive.isString) {
            return malformed("Android render backend settings schema version must be an integer")
        }
        val schemaVersion = schemaPrimitive.intOrNull
            ?: return malformed("Android render backend settings schema version must be an integer")
        if (schemaVersion != CURRENT_SCHEMA_VERSION) {
            return SettingsResult.Failure(
                SettingsError(
                    SettingsErrorCode.UNSUPPORTED_SCHEMA,
                    "Unsupported Android render backend settings schema version: $schemaVersion"
                )
            )
        }
        val settingsObject = root["settings"] as? JsonObject
            ?: return malformed("Missing Android render backend settings object")
        val backend = settingsObject["backend"]
        if (backend != null && (backend !is JsonPrimitive || !backend.isString)) {
            return malformed("Android render backend setting backend must be a string")
        }

        return try {
            val document = json.decodeFromJsonElement(
                AndroidRenderBackendSettingsDocument.serializer(),
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
private data class AndroidRenderBackendSettingsDocument(
    val schemaVersion: Int,
    val settings: AndroidRenderBackendSettings
)
