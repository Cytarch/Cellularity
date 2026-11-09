#include <jni.h>

#include <algorithm>
#include <exception>

#include "GenomeCodecJni.h"
#include "NativeJniUtils.h"
#include "app/NativeCellularityController.h"
#include "app/NativeWorldEditing.h"
#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/Gene.h"
#include "formats/GenomeDraftCodec.h"
#include "formats/release/ReleaseGenomeCodec.h"
#include "io/NativeByteStream.h"

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeValidateGenomeDraft(JNIEnv* env,
        jclass,
        jbyteArray genomeBytes) {
    cellularity::jni::validateGenomeDraft(env, genomeBytes);
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeEncodeReleaseGenome(
        JNIEnv* env,
        jclass,
        jbyteArray genomeBytes,
        jbyteArray previewBytes,
        jint previewCodec,
        jint previewWidth,
        jint previewHeight) {
    return cellularity::jni::encodeReleaseGenome(
            env, genomeBytes, previewBytes, previewCodec, previewWidth, previewHeight);
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeDecodeReleaseGenome(
        JNIEnv* env,
        jclass,
        jbyteArray releaseBytes) {
    return cellularity::jni::decodeReleaseGenome(env, releaseBytes);
}
