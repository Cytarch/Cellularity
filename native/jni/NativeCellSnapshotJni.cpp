#include <jni.h>

#include "NativeControllerHandle.h"
#include "NativeJniUtils.h"
#include "app/NativeCellularityController.h"
#include "formats/CellSnapshotWireCodec.h"

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeCellSnapshot(JNIEnv* env, jclass, jlong handle, jint cellId) {
    try {
        auto controller = controllerSharedFromHandle(env, handle);
        if (controller == nullptr) {
            return nullptr;
        }
        if (controller->microscopeRuntimeActive()) {
            auto snapshot = controller->inspectMicroscopeCell(cellId);
            return snapshot.has_value() ? toJavaByteArray(env, *snapshot) : nullptr;
        }
        auto access = controller->readWorld();
        if (cellId < 0 || cellId >= access.world().mCellData.mCellCount) {
            return nullptr;
        }
        return toJavaByteArray(env, writeCellSnapshotWire(access.world(), cellId));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
        return nullptr;
    }
}
