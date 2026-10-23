package com.cytarch.cellularity.platform.render.preview;

import androidx.annotation.Keep;

import com.cytarch.cellularity.NativeCellularityLibraryLoader;
import java.nio.ByteBuffer;

@Keep
public final class EnvironmentFieldPreviewBridge {
    static {
        NativeCellularityLibraryLoader.requireLoaded();
    }

    private EnvironmentFieldPreviewBridge() {}

    public static void publishEnvironmentFieldPreviewState(
            long endpointHandle,
            long endpointGeneration,
            int targetWire,
            int fieldTypeWire,
            double base,
            double strength,
            double angle,
            double offset,
            double softness,
            ByteBuffer resultBuffer) {
        if (resultBuffer == null
                || !resultBuffer.isDirect()
                || resultBuffer.isReadOnly()
                || resultBuffer.capacity() < 24) {
            throw new IllegalArgumentException(
                    "Environment Field Preview result buffer must be writable, direct, and at least 24 bytes");
        }
        nativePublishEnvironmentFieldPreviewState(
                endpointHandle,
                endpointGeneration,
                targetWire,
                fieldTypeWire,
                base,
                strength,
                angle,
                offset,
                softness,
                resultBuffer);
    }

    @Keep
    private static native void nativePublishEnvironmentFieldPreviewState(
            long endpointHandle,
            long endpointGeneration,
            int targetWire,
            int fieldTypeWire,
            double base,
            double strength,
            double angle,
            double offset,
            double softness,
            ByteBuffer resultBuffer);
}
