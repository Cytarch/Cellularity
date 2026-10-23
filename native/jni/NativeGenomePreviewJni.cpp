#include <jni.h>

#include <algorithm>
#include <exception>

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
    try {
        if (genomeBytes == nullptr) {
            throw DecodeError(DecodeErrorCode::InvalidValue,
                    "Genome draft payload is required");
        }
        ByteReader reader(readJavaBytes(env, genomeBytes, "Genome draft"));
        (void)readGenomeDraft(reader);
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    } catch (...) {
        throwUnknownDecodeException(env);
    }
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
    try {
        ByteReader reader(readJavaBytes(env, genomeBytes, "Genome draft"));
        const cellularity::GenomeData genome = readGenomeDraft(reader);
        cellularity::save_format::release::EncodedPreview preview;
        if (previewCodec != static_cast<jint>(cellularity::save_format::release::PreviewCodec::None)) {
            if (previewBytes == nullptr) {
                throw DecodeError(DecodeErrorCode::InvalidValue,
                        "Encoded genome preview bytes are required");
            }
            if (previewCodec < static_cast<jint>(
                        cellularity::save_format::release::PreviewCodec::Png)
                    || previewCodec > static_cast<jint>(
                               cellularity::save_format::release::PreviewCodec::WebpLossless)) {
                throw DecodeError(DecodeErrorCode::UnsupportedFeature,
                        "Unsupported genome preview codec");
            }
            if (previewWidth <= 0 || previewHeight <= 0) {
                throw DecodeError(DecodeErrorCode::InvalidValue,
                        "Encoded genome preview dimensions are invalid");
            }
            preview.codec = static_cast<
                    cellularity::save_format::release::PreviewCodec>(previewCodec);
            preview.pixelWidth = static_cast<std::uint32_t>(previewWidth);
            preview.pixelHeight = static_cast<std::uint32_t>(previewHeight);
            preview.bytes = readJavaBytes(env, previewBytes, "Genome preview");
        } else if (previewBytes != nullptr || previewWidth != 0 || previewHeight != 0) {
            throw DecodeError(DecodeErrorCode::InvalidValue,
                    "NONE genome preview has nonzero descriptor data");
        }
        return toJavaByteArray(
                env,
                cellularity::save_format::release::encodeReleaseGenome(
                        genome, preview));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    } catch (...) {
        throwUnknownDecodeException(env);
    }
    return nullptr;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_NativeCellularityControllerBridge_nativeDecodeReleaseGenome(
        JNIEnv* env,
        jclass,
        jbyteArray releaseBytes) {
    try {
        const cellularity::GenomeData genome =
                cellularity::save_format::release::decodeReleaseGenome(
                        readJavaBytes(env, releaseBytes, "Release genome"));
        return toJavaByteArray(env, writeGenomeDraft(genome));
    } catch (const std::exception& exception) {
        throwRuntimeException(env, exception);
    } catch (...) {
        throwUnknownDecodeException(env);
    }
    return nullptr;
}
