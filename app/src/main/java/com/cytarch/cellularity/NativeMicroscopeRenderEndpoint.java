package com.cytarch.cellularity;

import com.cytarch.cellularity.platform.render.microscope.NativeRenderEndpointOwner;
import com.cytarch.cellularity.platform.render.microscope.NativeMicroscopeRenderAccess;
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest;
import java.nio.ByteBuffer;

/**
 * Typed owner of the durable microscope render attachment.
 *
 * <p>The native endpoint owns leases rather than a controller wrapper, so a
 * world replacement can drain and rebind the same endpoint without exposing a
 * stale native pointer to the render thread.</p>
 */
public final class NativeMicroscopeRenderEndpoint
        implements NativeMicroscopeRenderAccess, AutoCloseable {
    private final NativeCellularityControllerBridge bridge;
    private final NativeRenderEndpointOwner owner;
    private final Object surfaceStateLock = new Object();
    private boolean surfaceAttached;
    private volatile boolean closed;

    NativeMicroscopeRenderEndpoint(
            NativeCellularityControllerBridge bridge,
            NativeRenderEndpointOwner owner) {
        this.bridge = bridge;
        this.owner = owner;
    }

    public NativeRenderEndpointOwner.Borrow borrowRenderEndpointForWorker() {
        ensureOpen();
        NativeRenderEndpointOwner.Borrow borrow = owner.borrow();
        if (borrow == null) {
            throw new IllegalStateException(
                    "Native microscope render endpoint is closed");
        }
        return borrow;
    }

    public long authorizeRenderWorker(
            long surfaceToken,
            long workerIdentity) {
        return bridge.authorizeRenderWorker(
                owner, surfaceToken, workerIdentity);
    }

    public long activateRenderSurface(
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration,
            long platformSurfaceGeneration,
            int width,
            int height) {
        return bridge.activateRenderSurface(
                owner, surfaceToken, workerIdentity,
                workerBindingGeneration, platformSurfaceGeneration,
                width, height);
    }

    public void updateRenderSurfaceDimensions(
            long surfaceToken,
            int width,
            int height) {
        bridge.updateRenderSurfaceDimensions(
                owner, surfaceToken, width, height);
    }

    public void deactivateRenderSurface(long surfaceToken) {
        bridge.deactivateRenderSurface(owner, surfaceToken);
    }

    public void markRenderWorkerClosing(
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration) {
        bridge.markRenderWorkerClosing(
                owner, surfaceToken, workerIdentity,
                workerBindingGeneration);
    }

    public void finishRenderWorkerClose(
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration) {
        bridge.finishRenderWorkerClose(
                owner, surfaceToken, workerIdentity,
                workerBindingGeneration);
    }

    public void onSurfaceCreated() {
        synchronized (surfaceStateLock) {
            ensureOpen();
            if (surfaceAttached) return;
            bridge.onPreviewSurfaceCreated(owner);
            surfaceAttached = true;
        }
    }

    public void onSurfaceDestroyed() {
        Throwable failure = null;
        synchronized (surfaceStateLock) {
            if (!surfaceAttached) return;
            surfaceAttached = false;
            try {
                bridge.onPreviewSurfaceDestroyed(owner);
            } catch (Throwable previewFailure) {
                failure = previewFailure;
            }
            try {
                bridge.onSurfaceDestroyed(owner);
            } catch (Throwable surfaceFailure) {
                failure = mergeFailure(failure, surfaceFailure);
            }
        }
        if (failure != null) rethrowUnchecked(failure);
    }

    public void preparePreviewSurfaceResources() {
        bridge.preparePreviewSurfaceResources(owner);
    }

    public void setViewportSurfaceSize(int width, int height) {
        bridge.setViewportSurfaceSize(owner, width, height);
    }

    public void setViewportCamera(float cameraX, float cameraY, float zoom) {
        bridge.setViewportCamera(owner, cameraX, cameraY, zoom);
    }

    public long draw(long surfaceToken) {
        return bridge.drawMicroscope(owner, surfaceToken);
    }

    public long clearWithoutRendering(long surfaceToken) {
        return bridge.clearMicroscopeNoRender(owner, surfaceToken);
    }

    public NativePreviewCaptureRequest enqueueSubstratePreviewCapture(
            long requestId,
            long substratePreviewToken) {
        return bridge.enqueueSubstratePreviewCapture(
                owner, requestId, substratePreviewToken);
    }

    public long publishPlacementGenome(byte[] genomeDraftBytes) {
        return bridge.publishMicroscopePlacementGenome(
                owner, genomeDraftBytes);
    }

    public void submitInputBatch(
            long surfaceToken,
            ByteBuffer input,
            ByteBuffer result,
            long expectedPlacementGenomeRevision) {
        bridge.submitMicroscopeInputBatch(
                owner, surfaceToken, input, result,
                expectedPlacementGenomeRevision);
    }

    public void readPresentationMotionSnapshot(ByteBuffer result) {
        bridge.readMicroscopePresentationMotionSnapshot(owner, result);
    }

    public boolean requestInputResync(long surfaceToken) {
        return bridge.requestMicroscopeInputResync(owner, surfaceToken);
    }

    public long activeInputEpoch(long surfaceToken) {
        return bridge.microscopeActiveInputEpoch(owner, surfaceToken);
    }

    public long completeInputResync(
            long surfaceToken,
            boolean activePointersDrained) {
        return bridge.completeMicroscopeInputResync(
                owner, surfaceToken, activePointersDrained);
    }

    public int activePointerGenerationCount() {
        return bridge.microscopeActivePointerGenerationCount(owner);
    }

    public void setToolOrdinal(int toolOrdinal) {
        bridge.setMicroscopeToolOrdinal(owner, toolOrdinal);
    }

    public void setTemperatureMode(int temperatureMode) {
        bridge.setMicroscopeTemperatureMode(owner, temperatureMode);
    }

    public void setSlowMotionSpeed(float speed) {
        bridge.setMicroscopeSlowMotionSpeed(owner, speed);
    }

    public void setIncubateTargetUps(double targetUps) {
        bridge.setMicroscopeIncubateTargetUps(owner, targetUps);
    }

    public void setPerformanceHudEnabled(boolean enabled) {
        bridge.setMicroscopePerformanceHudEnabled(owner, enabled);
    }

    void onControllerReplaced() {
        synchronized (surfaceStateLock) {
            if (!closed && surfaceAttached) {
                bridge.onPreviewSurfaceCreated(owner);
            }
        }
    }

    @Override
    public void close() {
        Throwable failure = null;
        synchronized (surfaceStateLock) {
            if (closed) return;
            closed = true;
            if (surfaceAttached) {
                surfaceAttached = false;
                try {
                    bridge.onPreviewSurfaceDestroyed(owner);
                } catch (Throwable previewFailure) {
                    failure = previewFailure;
                }
                try {
                    bridge.onSurfaceDestroyed(owner);
                } catch (Throwable surfaceFailure) {
                    failure = mergeFailure(failure, surfaceFailure);
                }
            }
        }
        try {
            owner.close();
        } catch (Throwable ownerFailure) {
            failure = mergeFailure(failure, ownerFailure);
        }
        if (failure != null) rethrowUnchecked(failure);
    }

    private void ensureOpen() {
        if (closed) {
            throw new IllegalStateException(
                    "Native microscope render endpoint is closed");
        }
    }

    private static Throwable mergeFailure(
            Throwable current,
            Throwable next) {
        if (current == null) return next;
        if (current != next) current.addSuppressed(next);
        return current;
    }

    private static void rethrowUnchecked(Throwable failure) {
        if (failure instanceof RuntimeException) {
            throw (RuntimeException) failure;
        }
        if (failure instanceof Error) throw (Error) failure;
        throw new IllegalStateException(
                "Native microscope render endpoint shutdown failed",
                failure);
    }
}
