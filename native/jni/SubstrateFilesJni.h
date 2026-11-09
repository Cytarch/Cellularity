#pragma once

// Substrate file inspection, validation and preview reads as JNI, shared by the Android
// bridge (NativeCellularityControllerBridge) and the desktop bridge (DesktopNativeRenderer).
// Both export the same entry points under their own class names; the work and the Java
// exceptions thrown on failure are this one implementation.

#include <jni.h>

#include <exception>
#include <string>

#include "NativeJniUtils.h"
#include "core/Environment.h"
#include "core/SimulationMemoryPolicy.h"
#include "formats/NativeSubstrateLoader.h"
#include "formats/SubstrateFileCodec.h"

namespace cellularity::jni {

[[nodiscard]] inline Environment substrateFallbackEnvironment(
        bool previewMode, int maxCellCount, int maxFoodCount, double radius) {
    return previewMode
            ? Environment::setupPreviewSimulationEnvironment(maxCellCount)
            : Environment::constructDefaultEnvironment(maxCellCount, maxFoodCount, radius);
}

[[nodiscard]] inline jbyteArray inspectSubstratePath(JNIEnv* env, jstring substratePath,
        jboolean previewMode, jint fallbackMaxCellCount, jint fallbackMaxFoodCount,
        jdouble fallbackRadius) {
    try {
        Environment fallbackEnvironment = substrateFallbackEnvironment(previewMode,
                fallbackMaxCellCount, fallbackMaxFoodCount, fallbackRadius);
        NativeSubstrateInspection inspection = ::inspectSubstratePath(
                readJavaString(env, substratePath, "SubstratePath"), previewMode,
                fallbackEnvironment);
        return toJavaByteArray(env, writeSubstrateInspectionSnapshot(inspection));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    } catch (...) {
        throwUnknownDecodeException(env);
    }
    return nullptr;
}

inline void validateSubstratePath(JNIEnv* env, jstring substratePath, jboolean previewMode,
        jint fallbackMaxCellCount, jint fallbackMaxFoodCount, jdouble fallbackRadius) {
    try {
        Environment fallbackEnvironment = substrateFallbackEnvironment(previewMode,
                fallbackMaxCellCount, fallbackMaxFoodCount, fallbackRadius);
        validateNativeSubstratePath(
                readJavaString(env, substratePath, "SubstratePath"), previewMode,
                fallbackEnvironment);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    } catch (...) {
        throwUnknownDecodeException(env);
    }
}

[[nodiscard]] inline jbyteArray readSubstratePreviewPath(JNIEnv* env, jstring substratePath) {
    try {
        return toJavaByteArray(env,
                ::readSubstratePreviewPath(readJavaString(env, substratePath, "SubstratePath")));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    } catch (...) {
        throwUnknownDecodeException(env);
    }
    return nullptr;
}

// absolute file bytes, operational file bytes, world budget bytes, address bits.
[[nodiscard]] inline jlongArray runtimeLimits(JNIEnv* env) {
    const cellularity::memory::RuntimeLimits limits = cellularity::memory::currentRuntimeLimits();
    const jlong values[4] = {
            static_cast<jlong>(limits.absoluteSubstrateFileBytes),
            static_cast<jlong>(limits.operationalSubstrateFileBytes),
            static_cast<jlong>(limits.worldAllocationBudgetBytes),
            static_cast<jlong>(limits.addressBits)
    };
    jlongArray result = env->NewLongArray(4);
    if (result != nullptr) {
        env->SetLongArrayRegion(result, 0, 4, values);
    }
    return result;
}

} // namespace cellularity::jni
