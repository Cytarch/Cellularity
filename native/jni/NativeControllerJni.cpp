#include <jni.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include "NativeControllerHandle.h"
#include "NativeJniUtils.h"
#include "SubstrateFilesJni.h"
#include "app/NativeCellularityController.h"
#include "app/NativeControllerFactory.h"
#include "app/NativeWorldEditing.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/EnvironmentBatch.h"
#include "core/SimulationMemoryPolicy.h"
#include "formats/DecodedWorldValidation.h"
#include "formats/NativeSubstrateLoader.h"
#include "formats/SubstrateFileCodec.h"
#include "io/NativeByteStream.h"

namespace {

jlong createControllerHandle(bool previewMode, const Environment& environment,
        std::int64_t createdAtUnixMillis) {
    return createNativeControllerHandle(cellularity::app::createNewPlateController(
            previewMode, environment, createdAtUnixMillis));
}

Environment createFallbackEnvironment(bool previewMode, int maxCellCount,
        int maxFoodCount, double radius) {
    return cellularity::jni::substrateFallbackEnvironment(
            previewMode, maxCellCount, maxFoodCount, radius);
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeCreate(
        JNIEnv* env, jclass, jboolean previewMode, jint maxCellCount,
        jint maxFoodCount, jdouble radius, jdouble innerRadius, jlong createdAtUnixMillis) {
    try {
        Environment environment = cellularity::app::newPlateEnvironment(
                previewMode, maxCellCount, maxFoodCount, radius, innerRadius);
        return createControllerHandle(
                previewMode, environment,
                static_cast<std::int64_t>(createdAtUnixMillis));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeRuntimeLimits(
        JNIEnv* env, jclass) {
    return cellularity::jni::runtimeLimits(env);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeIsConfigurationSupported(
        JNIEnv*, jclass, jint maxCellCount, jint maxFoodCount, jdouble radius) {
    try {
        const Environment environment = Environment::constructDefaultEnvironment(
                maxCellCount, maxFoodCount, radius);
        return cellularity::memory::isWorldAllocationSupported(environment)
                ? JNI_TRUE
                : JNI_FALSE;
    } catch (...) {
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeEstimateWorldAllocationBytes(
        JNIEnv*, jclass, jint maxCellCount, jint maxFoodCount, jdouble radius) {
    return static_cast<jlong>(cellularity::app::estimateNewPlateAllocationBytes(
            maxCellCount, maxFoodCount, radius));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeCreateFromSubstratePath(
        JNIEnv* env, jclass, jboolean previewMode, jstring substratePath,
        jint fallbackMaxCellCount, jint fallbackMaxFoodCount,
        jdouble fallbackRadius, jlong fallbackCreatedAtUnixMillis) {
    try {
        Environment fallbackEnvironment =
                createFallbackEnvironment(previewMode, fallbackMaxCellCount,
                        fallbackMaxFoodCount, fallbackRadius);
        return createNativeControllerHandle(cellularity::app::createStoredPlateController(
                readJavaString(env, substratePath, "SubstratePath"), previewMode,
                fallbackEnvironment,
                static_cast<std::int64_t>(fallbackCreatedAtUnixMillis)));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    } catch (...) {
        throwUnknownDecodeException(env);
        return 0;
    }
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeInspectSubstratePath(
        JNIEnv* env, jclass, jstring substratePath, jboolean previewMode,
        jint fallbackMaxCellCount, jint fallbackMaxFoodCount,
        jdouble fallbackRadius) {
    return cellularity::jni::inspectSubstratePath(env, substratePath, previewMode,
            fallbackMaxCellCount, fallbackMaxFoodCount, fallbackRadius);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeValidateSubstratePath(
        JNIEnv* env, jclass, jstring substratePath, jboolean previewMode,
        jint fallbackMaxCellCount, jint fallbackMaxFoodCount,
        jdouble fallbackRadius) {
    cellularity::jni::validateSubstratePath(env, substratePath, previewMode,
            fallbackMaxCellCount, fallbackMaxFoodCount, fallbackRadius);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeWriteSubstratePath(
        JNIEnv* env, jclass, jlong handle, jstring substratePath) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return 0;
        }
        const std::string path =
                readJavaString(env, substratePath, "SubstratePath");
        return static_cast<jlong>(
                controller->writeSubstratePathAndPreparePreview(path));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    } catch (...) {
        throwUnknownDecodeException(env);
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeAttachSubstratePreviewPath(
        JNIEnv* env, jclass, jstring substratePath, jbyteArray webpBytes) {
    try {
        attachSubstratePreviewPath(
                readJavaString(env, substratePath, "SubstratePath"),
                readJavaBytes(env, webpBytes, "SubstratePreview"));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    } catch (...) {
        throwUnknownDecodeException(env);
    }
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeReadSubstratePreviewPath(
        JNIEnv* env, jclass, jstring substratePath) {
    return cellularity::jni::readSubstratePreviewPath(env, substratePath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeDestroy(
        JNIEnv* env, jclass, jlong handle) {
    try {
        auto controller = closeAndDestroyNativeControllerHandle(env, handle);
        controller.reset();
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeControllerGeneration(
        JNIEnv* env, jclass, jlong handle) {
    try {
        return nativeControllerGenerationFromHandle(env, handle);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeCommitControllerReplacement(
        JNIEnv* env, jclass, jlong liveHandle, jlong candidateHandle,
        jlong expectedControllerGeneration) {
    try {
        return commitNativeControllerReplacement(env, liveHandle,
                candidateHandle,
                static_cast<std::uint64_t>(expectedControllerGeneration));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeDetachControllerForClose(
        JNIEnv* env, jclass, jlong handle) {
    try {
        return detachNativeControllerHandleForClose(env, handle);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeCloseRetiredController(
        JNIEnv*, jclass, jlong handle) {
    closeAndDestroyNativeRetiredControllerHandle(handle);
}

extern "C" JNIEXPORT jdouble JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeTime(
        JNIEnv* env, jclass, jlong handle) {
    auto controller = controllerSharedFromHandle(env, handle);
    if (controller == nullptr) {
        return 0.0;
    }
    auto access = controller->readWorld();
    return access.world().mTime;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeCompletedUpdateCount(
        JNIEnv* env, jclass, jlong handle) {
    auto controller = controllerSharedFromHandle(env, handle);
    return controller == nullptr
            ? 0
            : static_cast<jlong>(controller->completedUpdateCount());
}

extern "C" JNIEXPORT jdouble JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeRadius(
        JNIEnv* env, jclass, jlong handle) {
    auto controller = controllerSharedFromHandle(env, handle);
    if (controller == nullptr) {
        return 0.0;
    }
    auto access = controller->readWorld();
    return access.world().mEnvironment.mRadius;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeEnvironmentState(
        JNIEnv* env, jclass, jlong handle) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return nullptr;
        }
        if (controller->microscopeRuntimeActive()) {
            return toJavaByteArray(env,
                    controller->inspectMicroscopeEnvironment());
        }
        auto access = controller->readWorld();
        return toJavaByteArray(
                env, EnvironmentBatch::writeState(access.world().mEnvironment, access.environmentRevision()));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return nullptr;
    }
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeApplyEnvironmentPatch(
        JNIEnv* env, jclass, jlong handle, jbyteArray patchBytes) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return nullptr;
        }
        std::vector<uint8_t> bytes =
                readJavaBytes(env, patchBytes, "EnvironmentPatch");
        if (controller->microscopeRuntimeActive()) {
            return toJavaByteArray(
                    env, controller->applyMicroscopeEnvironmentPatch(std::move(bytes)));
        }
        auto access = controller->writeWorld();
        EnvironmentBatch::ApplyResult result = EnvironmentBatch::applyPatch(
                access.world().mEnvironment, access.environmentRevision(),
                std::move(bytes));
        if (result.applied) {
            const std::int64_t appliedRevision = access.markEnvironmentChanged();
            if (appliedRevision != result.revision) {
                throw std::logic_error(
                        "Environment revision changed during atomic patch");
            }
            controller->invalidateMicroscopeFrameData();
        }
        return toJavaByteArray(env, EnvironmentBatch::writeApplyResult(result));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return nullptr;
    } catch (...) {
        throwUnknownDecodeException(env);
        return nullptr;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeCellCount(
        JNIEnv* env, jclass, jlong handle) {
    auto controller = controllerSharedFromHandle(env, handle);
    if (controller == nullptr) {
        return 0;
    }
    auto access = controller->readWorld();
    return access.world().mCellData.mCellCount;
}

extern "C" JNIEXPORT jdouble JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeFoodMass(
        JNIEnv* env, jclass, jlong handle) {
    auto controller = controllerSharedFromHandle(env, handle);
    if (controller == nullptr) {
        return 0.0;
    }
    auto access = controller->readWorld();
    return access.world().mFoodData.getFoodMass();
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeReset(
        JNIEnv* env, jclass, jlong handle) {
    auto controller = controllerSharedFromHandle(env, handle);
    if (controller == nullptr) {
        return;
    }
    if (controller->microscopeRuntimeActive()) {
        try {
            controller->resetMicroscopeWorld();
        } catch (const std::exception& exception) {
            throwRuntimeException(env, exception);
        }
        return;
    }
    auto access = controller->writeWorld();
    SimulationWorld& world = access.world();
    resetWorldContents(world);
    access.resetCompletedUpdateCount();
    world.updateGrids();
    controller->invalidateRenderWorldEpoch(access.markWorldContentsReplaced());
    controller->clearMicroscopeInputQueues();
    controller->invalidateMicroscopeFrameData();
}
