package com.cytarch.cellularity.platform.render.microscope;

import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest;
import java.nio.ByteBuffer;

/**
 * Narrow native-render contract used by one microscope surface attachment.
 *
 * <p>The concrete endpoint owns the native attachment lease. Keeping this
 * contract separate from {@code MicroscopeSimulationBackend} prevents render
 * lifecycle and input operations from leaking back into the simulation API.</p>
 */
public interface NativeMicroscopeRenderAccess {
    NativeRenderEndpointOwner.Borrow borrowRenderEndpointForWorker();

    long authorizeRenderWorker(long surfaceToken, long workerIdentity);

    long activateRenderSurface(
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration,
            long platformSurfaceGeneration,
            int width,
            int height);

    void updateRenderSurfaceDimensions(long surfaceToken, int width, int height);

    void deactivateRenderSurface(long surfaceToken);

    void markRenderWorkerClosing(
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration);

    void finishRenderWorkerClose(
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration);

    void onSurfaceCreated();

    void onSurfaceDestroyed();

    long draw(long surfaceToken);

    long clearWithoutRendering(long surfaceToken);

    NativePreviewCaptureRequest enqueueSubstratePreviewCapture(
            long requestId,
            long substratePreviewToken);

    long publishPlacementGenome(byte[] genomeDraftBytes);

    void submitInputBatch(
            long surfaceToken,
            ByteBuffer input,
            ByteBuffer result,
            long expectedPlacementGenomeRevision);

    void readPresentationMotionSnapshot(ByteBuffer result);

    boolean requestInputResync(long surfaceToken);

    long activeInputEpoch(long surfaceToken);

    long completeInputResync(long surfaceToken, boolean activePointersDrained);

    int activePointerGenerationCount();

    void setToolOrdinal(int toolOrdinal);

    void setTemperatureMode(int temperatureMode);

    void setSlowMotionSpeed(float speed);

    void setIncubateTargetUps(double targetUps);

    void setPerformanceHudEnabled(boolean enabled);
}
