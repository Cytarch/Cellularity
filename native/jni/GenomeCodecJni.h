#pragma once

// The release-genome (CLTY) codec as JNI, shared by the Android bridge
// (NativeCellularityControllerBridge) and the desktop bridge (DesktopNativeRenderer).
// Only the exported symbol names differ between the two; the conversion, validation
// and the Java exceptions thrown on failure are this one implementation.

#include <jni.h>

#include <cstdint>
#include <exception>

#include "NativeJniUtils.h"
#include "formats/GenomeDraftCodec.h"
#include "formats/release/ReleaseGenomeCodec.h"
#include "io/NativeByteStream.h"

namespace cellularity::jni {

// Throws the shared CellularityDataException when the draft is not a loadable genome.
inline void validateGenomeDraft(JNIEnv* env, jbyteArray genomeBytes) {
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

[[nodiscard]] inline jbyteArray encodeReleaseGenome(
        JNIEnv* env,
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

[[nodiscard]] inline jbyteArray decodeReleaseGenome(JNIEnv* env, jbyteArray releaseBytes) {
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

} // namespace cellularity::jni
