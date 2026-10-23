package com.cytarch.cellularity.platform.render.preview;

import androidx.annotation.Keep;

import com.cytarch.cellularity.NativeCellularityLibraryLoader;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

@Keep
public final class NativePreviewCaptureBridge {
    static {
        NativeCellularityLibraryLoader.requireLoaded();
    }

    private NativePreviewCaptureBridge() {}

    public static NativePreviewCaptureRequest enqueueGenomeCapture(
            long endpointHandle,
            long requestId,
            byte[] genomeDraftBytes,
            double maximumSimulationTime) {
        if (requestId <= 0L || genomeDraftBytes == null) {
            return null;
        }
        ByteBuffer admission = ByteBuffer.allocateDirect(24)
                .order(ByteOrder.LITTLE_ENDIAN);
        enqueueGenome(
                endpointHandle,
                requestId,
                genomeDraftBytes,
                maximumSimulationTime,
                admission);
        return decodeAdmission(admission, requestId);
    }

    public static NativePreviewCaptureRequest enqueueSubstrateCapture(
            long endpointHandle,
            long requestId,
            long substratePreviewToken,
            long attachmentGeneration) {
        if (requestId <= 0L
                || substratePreviewToken == 0L
                || attachmentGeneration == 0L) {
            return null;
        }
        ByteBuffer admission = ByteBuffer.allocateDirect(24)
                .order(ByteOrder.LITTLE_ENDIAN);
        enqueueSubstrate(
                endpointHandle,
                requestId,
                substratePreviewToken,
                attachmentGeneration,
                admission);
        return decodeAdmission(admission, requestId);
    }

    private static NativePreviewCaptureRequest decodeAdmission(
            ByteBuffer buffer, long requestId) {
        ByteBuffer bytes = buffer.order(ByteOrder.LITTLE_ENDIAN);
        if (bytes.getInt(0) != 0x31414350
                || (bytes.getShort(4) & 0xFFFF) != 1
                || (bytes.getShort(6) & 0xFFFF) != 24
                || bytes.getInt(12) != 0) {
            throw new IllegalStateException(
                    "Native preview capture admission metadata is invalid");
        }
        int status = bytes.getInt(8);
        long requestHandle = bytes.getLong(16);
        if (status == 0) {
            if (requestHandle == 0L) {
                throw new IllegalStateException(
                        "Accepted preview capture has a null request handle");
            }
            try {
                return new JniNativePreviewCaptureRequest(requestHandle, requestId);
            } catch (Throwable failure) {
                try {
                    nativeDestroy(requestHandle);
                } catch (Throwable ignored) {
                    // Preserve the allocation/constructor failure that lost ownership.
                }
                throw failure;
            }
        }
        if (status < 1 || status > 6 || requestHandle != 0L) {
            throw new IllegalStateException(
                    "Native preview capture admission status is invalid");
        }
        return null;
    }

    static void enqueueGenome(
            long endpointHandle,
            long requestId,
            byte[] genomeDraftBytes,
            double maximumSimulationTime,
            ByteBuffer admissionBuffer) {
        requireWritableDirect(admissionBuffer, 24, "admission");
        nativeEnqueueGenome(
                endpointHandle,
                requestId,
                genomeDraftBytes,
                maximumSimulationTime,
                admissionBuffer);
    }

    static void enqueueSubstrate(
            long endpointHandle,
            long requestId,
            long substratePreviewToken,
            long attachmentGeneration,
            ByteBuffer admissionBuffer) {
        requireWritableDirect(admissionBuffer, 24, "admission");
        nativeEnqueueSubstrate(
                endpointHandle,
                requestId,
                substratePreviewToken,
                attachmentGeneration,
                admissionBuffer);
    }

    static byte[] await(
            long requestHandle,
            long timeoutMillis,
            ByteBuffer resultBuffer) {
        if (timeoutMillis <= 0L) {
            throw new IllegalArgumentException("Preview capture timeout must be positive");
        }
        requireWritableDirect(resultBuffer, 56, "result");
        return nativeAwait(requestHandle, timeoutMillis, resultBuffer);
    }

    static int cancel(long requestHandle) {
        return nativeCancel(requestHandle);
    }

    static void destroy(long requestHandle) {
        nativeDestroy(requestHandle);
    }

    private static void requireWritableDirect(
            ByteBuffer buffer, int minimumBytes, String name) {
        if (buffer == null
                || !buffer.isDirect()
                || buffer.isReadOnly()
                || buffer.capacity() < minimumBytes) {
            throw new IllegalArgumentException(
                    "Preview capture " + name
                            + " buffer must be writable, direct, and at least "
                            + minimumBytes + " bytes");
        }
    }

    @Keep
    private static native void nativeEnqueueGenome(
            long endpointHandle,
            long requestId,
            byte[] genomeDraftBytes,
            double maximumSimulationTime,
            ByteBuffer admissionBuffer);

    @Keep
    private static native void nativeEnqueueSubstrate(
            long endpointHandle,
            long requestId,
            long substratePreviewToken,
            long attachmentGeneration,
            ByteBuffer admissionBuffer);

    @Keep
    private static native byte[] nativeAwait(
            long requestHandle,
            long timeoutMillis,
            ByteBuffer resultBuffer);

    @Keep
    private static native int nativeCancel(long requestHandle);

    @Keep
    private static native void nativeDestroy(long requestHandle);
}
