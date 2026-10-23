package com.cytarch.cellularity;

import android.content.Context;

import com.cytarch.cellularity.platform.render.RenderPerformanceTelemetry;
import com.cytarch.cellularity.platform.render.microscope.NativeRenderEndpointOwner;
import com.cytarch.cellularity.platform.render.preview.EnvironmentFieldPreviewBridge;
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureBridge;
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest;
import com.cytarch.cellularity.platform.render.preview.NativePreviewEndpointOwner;
import com.cytarch.cellularity.shared.model.SimulationConfiguration;
import com.cytarch.cellularity.shared.model.SimulationContract;
import java.io.File;
import java.nio.ByteBuffer;
import java.util.concurrent.locks.ReentrantReadWriteLock;

public final class NativeCellularityControllerBridge implements AutoCloseable {
    static {
        NativeCellularityLibraryLoader.requireLoaded();
    }

    private static final int OPERATIONAL_SUBSTRATE_LIMIT_INDEX = 1;
    private static final long[] RUNTIME_LIMITS = readRuntimeLimits();

    private final Context context;
    private volatile double radius;
    private volatile NativePreviewEndpointOwner environmentFieldPreviewEndpoint;
    private volatile NativePreviewEndpointOwner genomePreviewEndpoint;
    private final ReentrantReadWriteLock lifetimeLock = new ReentrantReadWriteLock(true);
    private long nativeHandle;
    private volatile long genomePreviewAttachmentGeneration;
    private volatile long substratePreviewAttachmentGeneration;

    public NativeCellularityControllerBridge(Context context,
                                         boolean previewMode,
                                         int maxCellCount,
        int maxFoodCount,
                                         double radius) {
        this(context, previewMode, maxCellCount, maxFoodCount, radius, 0.0);
    }

    public NativeCellularityControllerBridge(Context context,
                                         boolean previewMode,
                                         int maxCellCount,
                                         int maxFoodCount,
                                         double radius,
                                         double innerRadius) {
        this.context = context.getApplicationContext();
        this.radius = radius;
        long createdHandle = nativeCreate(
                previewMode,
                maxCellCount,
                maxFoodCount,
                radius,
                innerRadius,
                System.currentTimeMillis());
        NativePreviewEndpointOwner environmentEndpoint = null;
        NativePreviewEndpointOwner genomeEndpoint = null;
        boolean initialized = false;
        try {
            environmentEndpoint = NativePreviewEndpointOwner.create(
                    createdHandle,
                    NativePreviewEndpointOwner.Kind.ENVIRONMENT_FIELD_PREVIEW);
            genomeEndpoint = NativePreviewEndpointOwner.create(
                    createdHandle,
                    NativePreviewEndpointOwner.Kind.GENOME_PREVIEW);
            this.environmentFieldPreviewEndpoint = environmentEndpoint;
            this.genomePreviewEndpoint = genomeEndpoint;
            this.nativeHandle = createdHandle;
            initialized = true;
        } finally {
            if (!initialized && createdHandle != 0L) {
                if (genomeEndpoint != null) genomeEndpoint.close();
                if (environmentEndpoint != null) environmentEndpoint.close();
                nativeDestroy(createdHandle);
            }
        }
    }

    public NativeCellularityControllerBridge(Context context,
                                         boolean previewMode,
                                         String substratePath,
                                         SimulationConfiguration fallbackConfiguration) {
        this.context = context.getApplicationContext();
        long fallbackCreatedAtUnixMillis = new File(substratePath).lastModified();
        if (fallbackCreatedAtUnixMillis <= 0) {
            fallbackCreatedAtUnixMillis = System.currentTimeMillis();
        }
        long createdHandle = nativeCreateFromSubstratePath(
                previewMode,
                substratePath,
                fallbackConfiguration.getMaxCellCount(),
                fallbackConfiguration.getMaxFoodCount(),
                fallbackConfiguration.getRadius(),
                fallbackCreatedAtUnixMillis
        );
        NativePreviewEndpointOwner environmentEndpoint = null;
        NativePreviewEndpointOwner genomeEndpoint = null;
        boolean initialized = false;
        try {
            this.radius = nativeRadius(createdHandle);
            environmentEndpoint = NativePreviewEndpointOwner.create(
                    createdHandle,
                    NativePreviewEndpointOwner.Kind.ENVIRONMENT_FIELD_PREVIEW);
            genomeEndpoint = NativePreviewEndpointOwner.create(
                    createdHandle,
                    NativePreviewEndpointOwner.Kind.GENOME_PREVIEW);
            this.environmentFieldPreviewEndpoint = environmentEndpoint;
            this.genomePreviewEndpoint = genomeEndpoint;
            this.nativeHandle = createdHandle;
            initialized = true;
        } finally {
            if (!initialized && createdHandle != 0L) {
                if (genomeEndpoint != null) genomeEndpoint.close();
                if (environmentEndpoint != null) environmentEndpoint.close();
                nativeDestroy(createdHandle);
            }
        }
    }

    /** One-shot owner of a fully decoded controller that is not yet published. */
    public static final class PreparedControllerReplacement implements AutoCloseable {
        private static final int PREPARED = 0;
        private static final int COMMITTING = 1;
        private static final int COMMITTED = 2;
        private static final int DISCARDED = 3;

        private final long expectedControllerGeneration;
        private final double radius;
        private long nativeHandle;
        private NativePreviewEndpointOwner environmentEndpoint;
        private NativePreviewEndpointOwner genomeEndpoint;
        private int state = PREPARED;

        private PreparedControllerReplacement(
                long nativeHandle,
                long expectedControllerGeneration,
                double radius,
                NativePreviewEndpointOwner environmentEndpoint,
                NativePreviewEndpointOwner genomeEndpoint) {
            this.nativeHandle = nativeHandle;
            this.expectedControllerGeneration = expectedControllerGeneration;
            this.radius = radius;
            this.environmentEndpoint = environmentEndpoint;
            this.genomeEndpoint = genomeEndpoint;
        }

        public synchronized void attachRenderAssetsLease(long assetsLeaseHandle) {
            requirePrepared();
            if (assetsLeaseHandle == 0L) {
                throw new IllegalArgumentException(
                        "Android render assets lease handle is null");
            }
            nativeAttachRenderAssetsLease(nativeHandle, assetsLeaseHandle);
        }

        private synchronized long takeNativeHandleForCommit() {
            requirePrepared();
            long handle = nativeHandle;
            nativeHandle = 0L;
            state = COMMITTING;
            return handle;
        }

        private synchronized void completeCommit() {
            if (state != COMMITTING) {
                throw new IllegalStateException(
                        "Prepared replacement is not being committed");
            }
            state = COMMITTED;
        }

        private void failConsumedCommit() {
            NativePreviewEndpointOwner environment;
            NativePreviewEndpointOwner genome;
            synchronized (this) {
                if (state != COMMITTING) return;
                state = DISCARDED;
                environment = environmentEndpoint;
                genome = genomeEndpoint;
                environmentEndpoint = null;
                genomeEndpoint = null;
            }
            if (genome != null) genome.close();
            if (environment != null) environment.close();
        }

        private synchronized NativePreviewEndpointOwner takeEnvironmentEndpoint() {
            if (state != COMMITTED || environmentEndpoint == null) {
                throw new IllegalStateException(
                        "Replacement environment endpoint is unavailable");
            }
            NativePreviewEndpointOwner endpoint = environmentEndpoint;
            environmentEndpoint = null;
            return endpoint;
        }

        private synchronized NativePreviewEndpointOwner takeGenomeEndpoint() {
            if (state != COMMITTED || genomeEndpoint == null) {
                throw new IllegalStateException(
                        "Replacement genome endpoint is unavailable");
            }
            NativePreviewEndpointOwner endpoint = genomeEndpoint;
            genomeEndpoint = null;
            return endpoint;
        }

        private void requirePrepared() {
            if (state != PREPARED || nativeHandle == 0L) {
                throw new IllegalStateException(
                        "Prepared controller replacement was already consumed");
            }
        }

        @Override
        public void close() {
            long handle;
            NativePreviewEndpointOwner environment;
            NativePreviewEndpointOwner genome;
            synchronized (this) {
                if (state == COMMITTED || state == DISCARDED) return;
                if (state == COMMITTING) {
                    throw new IllegalStateException(
                            "Cannot discard a replacement during native commit");
                }
                state = DISCARDED;
                handle = nativeHandle;
                nativeHandle = 0L;
                environment = environmentEndpoint;
                genome = genomeEndpoint;
                environmentEndpoint = null;
                genomeEndpoint = null;
            }
            if (genome != null) genome.close();
            if (environment != null) environment.close();
            if (handle != 0L) nativeDestroy(handle);
        }
    }

    /** Heavy controller destruction is intentionally deferred beyond gateway locks. */
    public static final class RetiredController implements AutoCloseable {
        private long nativeHandle;
        private NativePreviewEndpointOwner environmentEndpoint;
        private NativePreviewEndpointOwner genomeEndpoint;

        private synchronized void initialize(
                long handle,
                NativePreviewEndpointOwner environment,
                NativePreviewEndpointOwner genome) {
            if (handle == 0L || nativeHandle != 0L) {
                throw new IllegalStateException(
                        "Retired native controller metadata is invalid");
            }
            nativeHandle = handle;
            environmentEndpoint = environment;
            genomeEndpoint = genome;
        }

        @Override
        public void close() {
            long handle;
            NativePreviewEndpointOwner environment;
            NativePreviewEndpointOwner genome;
            synchronized (this) {
                handle = nativeHandle;
                nativeHandle = 0L;
                environment = environmentEndpoint;
                genome = genomeEndpoint;
                environmentEndpoint = null;
                genomeEndpoint = null;
            }
            if (genome != null) genome.close();
            if (environment != null) environment.close();
            if (handle != 0L) nativeCloseRetiredController(handle);
        }
    }

    public static byte[] inspectSubstratePath(
            String substratePath,
            boolean previewMode,
            SimulationConfiguration fallbackConfiguration) {
        return nativeInspectSubstratePath(
                substratePath,
                previewMode,
                fallbackConfiguration.getMaxCellCount(),
                fallbackConfiguration.getMaxFoodCount(),
                fallbackConfiguration.getRadius());
    }

    public static void validateSubstratePath(
            String substratePath,
            boolean previewMode,
            SimulationConfiguration fallbackConfiguration) {
        nativeValidateSubstratePath(
                substratePath,
                previewMode,
                fallbackConfiguration.getMaxCellCount(),
                fallbackConfiguration.getMaxFoodCount(),
                fallbackConfiguration.getRadius());
    }

    public static long operationalSubstrateFileLimitBytes() {
        return RUNTIME_LIMITS[OPERATIONAL_SUBSTRATE_LIMIT_INDEX];
    }

    public static boolean isConfigurationSupported(SimulationConfiguration configuration) {
        if (configuration == null || !SimulationContract.INSTANCE.isConfigurationValid(configuration)) {
            return false;
        }
        return nativeIsConfigurationSupported(
                configuration.getMaxCellCount(),
                configuration.getMaxFoodCount(),
                configuration.getRadius());
    }

    public static long estimateWorldAllocationBytes(SimulationConfiguration configuration) {
        if (configuration == null || !SimulationContract.INSTANCE.isConfigurationValid(configuration)) {
            return -1;
        }
        return nativeEstimateWorldAllocationBytes(
                configuration.getMaxCellCount(),
                configuration.getMaxFoodCount(),
                configuration.getRadius());
    }

    private static long[] readRuntimeLimits() {
        long[] limits = nativeRuntimeLimits();
        if (limits == null || limits.length != 4) {
            throw new IllegalStateException("Native runtime limits are unavailable");
        }
        return limits;
    }

    /** Runs the authoritative native validation for the versioned GenomeDraft wire payload. */
    public static void validateGenomeDraft(byte[] genomeDraftBytes) {
        if (genomeDraftBytes == null) {
            throw new IllegalArgumentException("Genome draft payload is required");
        }
        nativeValidateGenomeDraft(genomeDraftBytes);
    }

    /** Converts the internal CGDW editor message into a persistent CLTY genome. */
    public static byte[] encodeReleaseGenome(
            byte[] genomeDraftBytes,
            byte[] previewBytes,
            int previewCodec,
            int previewWidth,
            int previewHeight) {
        if (genomeDraftBytes == null) {
            throw new IllegalArgumentException("Genome draft payload is required");
        }
        return nativeEncodeReleaseGenome(
                genomeDraftBytes,
                previewBytes,
                previewCodec,
                previewWidth,
                previewHeight);
    }

    /** Converts a persistent CLTY genome into the internal CGDW editor message. */
    public static byte[] decodeReleaseGenome(byte[] releaseGenomeBytes) {
        if (releaseGenomeBytes == null) {
            throw new IllegalArgumentException("Release genome payload is required");
        }
        return nativeDecodeReleaseGenome(releaseGenomeBytes);
    }

    public void attachRenderAssetsLease(long assetsLeaseHandle) {
        if (assetsLeaseHandle == 0L) {
            throw new IllegalArgumentException(
                    "Android render assets lease handle is null");
        }
        withHandleAction(handle -> nativeAttachRenderAssetsLease(
                handle, assetsLeaseHandle));
    }

    public NativeMicroscopeRenderEndpoint createMicroscopeRenderEndpoint() {
        ReentrantReadWriteLock.ReadLock lock = lifetimeLock.readLock();
        lock.lock();
        try {
            return new NativeMicroscopeRenderEndpoint(
                    this,
                    NativeRenderEndpointOwner.create(requireHandle()));
        } finally {
            lock.unlock();
        }
    }

    public PreparedControllerReplacement prepareSubstrateReplacement(
            boolean previewMode,
            String substratePath,
            SimulationConfiguration fallbackConfiguration) {
        long expectedGeneration = withHandle(
                NativeCellularityControllerBridge::nativeControllerGeneration);
        if (expectedGeneration == 0L) {
            throw new IllegalStateException(
                    "Native controller generation is unavailable");
        }

        long fallbackCreatedAtUnixMillis = new File(substratePath).lastModified();
        if (fallbackCreatedAtUnixMillis <= 0L) {
            fallbackCreatedAtUnixMillis = System.currentTimeMillis();
        }
        long candidateHandle = nativeCreateFromSubstratePath(
                previewMode,
                substratePath,
                fallbackConfiguration.getMaxCellCount(),
                fallbackConfiguration.getMaxFoodCount(),
                fallbackConfiguration.getRadius(),
                fallbackCreatedAtUnixMillis);
        NativePreviewEndpointOwner environment = null;
        NativePreviewEndpointOwner genome = null;
        boolean prepared = false;
        try {
            double candidateRadius = nativeRadius(candidateHandle);
            environment = NativePreviewEndpointOwner.create(
                    candidateHandle,
                    NativePreviewEndpointOwner.Kind.ENVIRONMENT_FIELD_PREVIEW);
            genome = NativePreviewEndpointOwner.create(
                    candidateHandle,
                    NativePreviewEndpointOwner.Kind.GENOME_PREVIEW);
            PreparedControllerReplacement replacement =
                    new PreparedControllerReplacement(
                            candidateHandle,
                            expectedGeneration,
                            candidateRadius,
                            environment,
                            genome);
            prepared = true;
            return replacement;
        } finally {
            if (!prepared) {
                if (genome != null) genome.close();
                if (environment != null) environment.close();
                if (candidateHandle != 0L) nativeDestroy(candidateHandle);
            }
        }
    }

    public synchronized RetiredController commitReplacement(
            PreparedControllerReplacement prepared) {
        if (prepared == null) {
            throw new IllegalArgumentException(
                    "Prepared controller replacement is required");
        }
        RetiredController retired = new RetiredController();
        ReentrantReadWriteLock.WriteLock lock = lifetimeLock.writeLock();
        lock.lock();
        boolean candidateConsumed = false;
        try {
            long liveHandle = requireHandle();
            long candidateHandle = prepared.takeNativeHandleForCommit();
            candidateConsumed = true;
            long retiredHandle = nativeCommitControllerReplacement(
                    liveHandle,
                    candidateHandle,
                    prepared.expectedControllerGeneration);
            if (retiredHandle == 0L) {
                throw new IllegalStateException(
                        "Native controller replacement returned no retired controller");
            }

            radius = prepared.radius;
            prepared.completeCommit();
            NativePreviewEndpointOwner nextEnvironment =
                    prepared.takeEnvironmentEndpoint();
            NativePreviewEndpointOwner nextGenome = prepared.takeGenomeEndpoint();
            NativePreviewEndpointOwner oldEnvironment =
                    environmentFieldPreviewEndpoint;
            NativePreviewEndpointOwner oldGenome = genomePreviewEndpoint;
            // Clear controller-local attachment generations before publishing
            // the volatile endpoint owners. A reader that observes either new
            // owner must also observe that no old attachment is transferable.
            genomePreviewAttachmentGeneration = 0L;
            substratePreviewAttachmentGeneration = 0L;
            environmentFieldPreviewEndpoint = nextEnvironment;
            genomePreviewEndpoint = nextGenome;
            retired.initialize(retiredHandle, oldEnvironment, oldGenome);
            return retired;
        } catch (Throwable failure) {
            if (candidateConsumed) prepared.failConsumedCommit();
            throw failure;
        } finally {
            lock.unlock();
        }
    }

    public synchronized RetiredController detachForClose() {
        RetiredController retired = new RetiredController();
        ReentrantReadWriteLock.WriteLock lock = lifetimeLock.writeLock();
        lock.lock();
        NativePreviewEndpointOwner detachedEnvironment = null;
        NativePreviewEndpointOwner detachedGenome = null;
        try {
            long handle = nativeHandle;
            if (handle == 0L) return retired;
            nativeHandle = 0L;
            detachedEnvironment = environmentFieldPreviewEndpoint;
            detachedGenome = genomePreviewEndpoint;
            environmentFieldPreviewEndpoint = null;
            genomePreviewEndpoint = null;
            genomePreviewAttachmentGeneration = 0L;
            substratePreviewAttachmentGeneration = 0L;
            long retiredHandle = nativeDetachControllerForClose(handle);
            if (retiredHandle == 0L) {
                throw new IllegalStateException(
                        "Native controller detach returned no retired controller");
            }
            retired.initialize(
                    retiredHandle, detachedEnvironment, detachedGenome);
            detachedEnvironment = null;
            detachedGenome = null;
            return retired;
        } catch (Throwable failure) {
            if (detachedGenome != null) detachedGenome.close();
            if (detachedEnvironment != null) detachedEnvironment.close();
            throw failure;
        } finally {
            lock.unlock();
        }
    }

    long authorizeRenderWorker(
            NativeRenderEndpointOwner owner,
            long surfaceToken,
            long workerIdentity) {
        if (surfaceToken == 0L || workerIdentity == 0L) {
            throw new IllegalArgumentException(
                    "Render worker identities must be nonzero");
        }
        long generation;
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            generation = nativeAuthorizeRenderWorker(
                    endpoint.getHandle(), surfaceToken, workerIdentity);
        }
        if (generation == 0L) {
            throw new IllegalStateException(
                    "Native render worker authorization is unavailable");
        }
        return generation;
    }

    long activateRenderSurface(
            NativeRenderEndpointOwner owner,
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration,
            long platformSurfaceGeneration,
            int width,
            int height) {
        long[] generations;
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            generations = nativeActivateRenderSurface(
                    endpoint.getHandle(), surfaceToken, workerIdentity,
                    workerBindingGeneration, platformSurfaceGeneration,
                    width, height);
        }
        if (generations == null || generations.length != 4
                || generations[0] == 0L || generations[1] == 0L
                || generations[2] == 0L) {
            throw new IllegalStateException(
                    "Native render surface activation metadata is invalid");
        }
        return generations[2];
    }

    void updateRenderSurfaceDimensions(
            NativeRenderEndpointOwner owner,
            long surfaceToken,
            int width,
            int height) {
        int status;
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            status = nativeUpdateRenderSurfaceDimensions(
                    endpoint.getHandle(), surfaceToken, width, height);
        }
        if (status != 0 && status != 1 && status != 3 && status != 8) {
            throw new IllegalStateException(
                    "Native render surface dimension update failed: " + status);
        }
    }

    void deactivateRenderSurface(
            NativeRenderEndpointOwner owner,
            long surfaceToken) {
        NativeRenderEndpointOwner.Borrow endpoint = owner.borrow();
        if (endpoint == null) return;
        try (endpoint) {
            nativeDeactivateRenderSurface(endpoint.getHandle(), surfaceToken);
        }
    }

    void markRenderWorkerClosing(
            NativeRenderEndpointOwner owner,
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration) {
        NativeRenderEndpointOwner.Borrow endpoint = owner.borrow();
        if (endpoint == null) return;
        try (endpoint) {
            nativeMarkRenderWorkerClosing(
                    endpoint.getHandle(), surfaceToken, workerIdentity,
                    workerBindingGeneration);
        }
    }

    void finishRenderWorkerClose(
            NativeRenderEndpointOwner owner,
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration) {
        NativeRenderEndpointOwner.Borrow endpoint = owner.borrow();
        if (endpoint == null) return;
        try (endpoint) {
            nativeFinishRenderWorkerClose(
                    endpoint.getHandle(), surfaceToken, workerIdentity,
                    workerBindingGeneration);
        }
    }

    /** Forgets handles owned by a lost GL context without destroying the native world. */
    void preparePreviewSurfaceResources(NativeRenderEndpointOwner owner) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            nativePreparePreviewSurfaceResources(endpoint.getHandle());
        }
    }

    /** Forgets handles owned by a lost GL context without destroying the native world. */
    void onSurfaceDestroyed(NativeRenderEndpointOwner owner) {
        NativeRenderEndpointOwner.Borrow endpoint = owner.borrow();
        if (endpoint == null) return;
        try (endpoint) {
            nativeSurfaceDestroyed(endpoint.getHandle());
        }
    }

    void setViewportSurfaceSize(
            NativeRenderEndpointOwner owner,
            int width,
            int height) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            nativeSetViewportSurfaceSize(endpoint.getHandle(), width, height);
        }
    }

    void setViewportCamera(
            NativeRenderEndpointOwner owner,
            float cameraX,
            float cameraY,
            float zoom) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            nativeSetViewportCamera(
                    endpoint.getHandle(), cameraX, cameraY, zoom);
        }
    }

    void moveViewportByScreenDelta(
            NativeRenderEndpointOwner owner,
            float dx,
            float dy) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            recordBoundaryJniCall(
                    RenderPerformanceTelemetry.JNI_GENOME_PREVIEW_CONTROL);
            nativeMoveViewportByScreenDelta(endpoint.getHandle(), dx, dy);
        }
    }

    void zoomViewportAt(
            NativeRenderEndpointOwner owner,
            float factor,
            float screenX,
            float screenY) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            recordBoundaryJniCall(
                    RenderPerformanceTelemetry.JNI_GENOME_PREVIEW_CONTROL);
            nativeZoomViewportAt(
                    endpoint.getHandle(), factor, screenX, screenY);
        }
    }

    float[] viewportState(NativeRenderEndpointOwner owner) {
        float[] state;
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            recordBoundaryJniCall(RenderPerformanceTelemetry.JNI_VIEWPORT_SNAPSHOT);
            state = nativeViewportState(endpoint.getHandle());
        }
        if (BuildConfig.ENABLE_RENDER_PROFILING && state != null) {
            RenderPerformanceTelemetry.recordViewportSnapshot(state.length);
        }
        return state;
    }

    public synchronized long drawGenomePreview(ByteBuffer acknowledgmentBuffer) {
        requireGenomePreviewAcknowledgmentBuffer(acknowledgmentBuffer);
        long handle = requireHandle();
        recordBoundaryJniCall(RenderPerformanceTelemetry.JNI_DRAW_GENOME_PREVIEW);
        return nativeDrawGenomePreview(handle, acknowledgmentBuffer);
    }

    public boolean readGenomePreviewAcknowledgment(
            ByteBuffer acknowledgmentBuffer) {
        requireGenomePreviewAcknowledgmentBuffer(acknowledgmentBuffer);
        try (NativePreviewEndpointOwner.Borrow endpoint =
                     requirePreviewBorrow(genomePreviewEndpoint)) {
            return nativeReadGenomePreviewAcknowledgment(
                    endpoint.getHandle(),
                    endpoint.getGeneration(),
                    acknowledgmentBuffer);
        }
    }

    public void publishGenomePreviewContent(
            byte[] genomeDraftBytes,
            double simulationTimeHours,
            long producerSequence,
            ByteBuffer resultBuffer) {
        requireGenomePreviewResultBuffer(resultBuffer);
        try (NativePreviewEndpointOwner.Borrow endpoint =
                     requirePreviewBorrow(genomePreviewEndpoint)) {
            nativePublishGenomePreviewContent(
                    endpoint.getHandle(),
                    endpoint.getGeneration(),
                    genomeDraftBytes,
                    simulationTimeHours,
                    producerSequence,
                    resultBuffer);
        }
    }

    public void publishGenomePreviewViewState(
            int selectedGeneIndex,
            boolean gridEnabled,
            boolean treeEnabled,
            int viewportWidth,
            int viewportHeight,
            long producerSequence,
            ByteBuffer resultBuffer) {
        requireGenomePreviewResultBuffer(resultBuffer);
        try (NativePreviewEndpointOwner.Borrow endpoint =
                     requirePreviewBorrow(genomePreviewEndpoint)) {
            nativePublishGenomePreviewViewState(
                    endpoint.getHandle(),
                    endpoint.getGeneration(),
                    selectedGeneIndex,
                    gridEnabled,
                    treeEnabled,
                    viewportWidth,
                    viewportHeight,
                    producerSequence,
                    resultBuffer);
        }
    }

    public int enqueueGenomePreviewInteraction(
            long completionToken,
            int kind,
            float first,
            float second,
            float third) {
        try (NativePreviewEndpointOwner.Borrow endpoint =
                     requirePreviewBorrow(genomePreviewEndpoint)) {
            return nativeEnqueueGenomePreviewInteraction(
                    endpoint.getHandle(),
                    endpoint.getGeneration(),
                    genomePreviewAttachmentGeneration,
                    completionToken,
                    kind,
                    first,
                    second,
                    third);
        }
    }

    public NativePreviewCaptureRequest enqueueGenomePreviewCapture(
            long requestId,
            byte[] genomeDraftBytes,
            double maximumSimulationTime) {
        try (NativePreviewEndpointOwner.Borrow endpoint =
                     requirePreviewBorrow(genomePreviewEndpoint)) {
            return NativePreviewCaptureBridge.enqueueGenomeCapture(
                    endpoint.getHandle(),
                    requestId,
                    genomeDraftBytes,
                    maximumSimulationTime);
        }
    }

    public NativePreviewCaptureRequest enqueueSubstratePreviewCapture(
            NativeRenderEndpointOwner owner,
            long requestId,
            long substratePreviewToken) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            final long attachmentGeneration =
                    substratePreviewAttachmentGeneration;
            if (attachmentGeneration == 0L) return null;
            return NativePreviewCaptureBridge.enqueueSubstrateCapture(
                    endpoint.getHandle(),
                    requestId,
                    substratePreviewToken,
                    attachmentGeneration);
        }
    }

    public long genomePreviewEndpointGeneration() {
        NativePreviewEndpointOwner endpoint = genomePreviewEndpoint;
        return endpoint == null ? 0L : endpoint.getGeneration();
    }

    public NativePreviewEndpointOwner.Borrow borrowGenomePreviewEndpoint() {
        return requirePreviewBorrow(genomePreviewEndpoint);
    }

    public NativePreviewEndpointOwner.Borrow borrowEnvironmentFieldPreviewEndpoint() {
        return requirePreviewBorrow(environmentFieldPreviewEndpoint);
    }

    public long environmentFieldPreviewEndpointGeneration() {
        NativePreviewEndpointOwner endpoint = environmentFieldPreviewEndpoint;
        return endpoint == null ? 0L : endpoint.getGeneration();
    }

    public void onPreviewSurfaceCreated(NativeRenderEndpointOwner owner) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            long handle = endpoint.getHandle();
            long genomeGeneration = nativeGenomePreviewSurfaceCreated(handle);
            long substrateGeneration = nativeSubstratePreviewSurfaceCreated(handle);
            if (genomeGeneration == 0L || substrateGeneration == 0L) {
                if (genomeGeneration != 0L) {
                    nativeGenomePreviewSurfaceDestroyed(handle, genomeGeneration);
                }
                if (substrateGeneration != 0L) {
                    nativeSubstratePreviewSurfaceDestroyed(handle, substrateGeneration);
                }
                throw new IllegalStateException(
                        "Native preview surface attachment is unavailable");
            }
            genomePreviewAttachmentGeneration = genomeGeneration;
            substratePreviewAttachmentGeneration = substrateGeneration;
        }
    }

    public void onPreviewSurfaceDestroyed(NativeRenderEndpointOwner owner) {
        final long genomeGeneration = genomePreviewAttachmentGeneration;
        final long substrateGeneration = substratePreviewAttachmentGeneration;
        genomePreviewAttachmentGeneration = 0L;
        substratePreviewAttachmentGeneration = 0L;
        NativeRenderEndpointOwner.Borrow endpoint = owner.borrow();
        if (endpoint == null) return;
        try (endpoint) {
            nativeGenomePreviewSurfaceDestroyed(
                    endpoint.getHandle(), genomeGeneration);
            nativeSubstratePreviewSurfaceDestroyed(
                    endpoint.getHandle(), substrateGeneration);
        }
    }

    public synchronized long drawEnvironmentFieldPreview() {
        long handle = requireHandle();
        recordBoundaryJniCall(RenderPerformanceTelemetry.JNI_DRAW_ENVIRONMENT_PREVIEW);
        return nativeDrawEnvironmentFieldPreview(handle);
    }

    public void publishEnvironmentFieldPreviewState(
            int targetWire,
            int fieldTypeWire,
            double base,
            double strength,
            double angle,
            double offset,
            double softness,
            ByteBuffer resultBuffer) {
        try (NativePreviewEndpointOwner.Borrow endpoint =
                     requirePreviewBorrow(environmentFieldPreviewEndpoint)) {
            EnvironmentFieldPreviewBridge.publishEnvironmentFieldPreviewState(
                    endpoint.getHandle(),
                    endpoint.getGeneration(),
                    targetWire,
                    fieldTypeWire,
                    base,
                    strength,
                    angle,
                    offset,
                    softness,
                    resultBuffer);
        }
    }

    long drawMicroscope(
            NativeRenderEndpointOwner owner,
            long surfaceToken) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            recordBoundaryJniCall(RenderPerformanceTelemetry.JNI_DRAW_MICROSCOPE);
            return nativeDrawMicroscope(endpoint.getHandle(), surfaceToken);
        }
    }

    long clearMicroscopeNoRender(
            NativeRenderEndpointOwner owner,
            long surfaceToken) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            recordBoundaryJniCall(RenderPerformanceTelemetry.JNI_CLEAR_MICROSCOPE);
            return nativeClearMicroscopeNoRender(
                    endpoint.getHandle(), surfaceToken);
        }
    }

    public void startMicroscopeRuntime() {
        withHandleAction(NativeCellularityControllerBridge::nativeStartMicroscopeRuntime);
    }

    public void setMicroscopeLifecyclePaused(boolean paused) {
        withHandleAction(handle -> nativeSetMicroscopeLifecyclePaused(handle, paused));
    }

    public static long microscopeRuntimePublicationIntervalMillis() {
        return nativeMicroscopeRuntimePublicationIntervalMillis();
    }

    public NativeMicroscopeRuntimePublication pollMicroscopeRuntimePublication(
            long lastRevision,
            long lastWorldEpoch,
            long lastEnvironmentRevision) {
        NativeMicroscopeRuntimePublication publication = withHandle(handle -> {
            recordBoundaryJniCall(RenderPerformanceTelemetry.JNI_RUNTIME_PUBLICATION_POLL);
            return nativePollMicroscopeRuntimePublication(
                        handle,
                        lastRevision,
                        lastWorldEpoch,
                        lastEnvironmentRevision);
        });
        if (BuildConfig.ENABLE_RENDER_PROFILING) {
            RenderPerformanceTelemetry.recordRuntimePublicationPoll(publication != null);
        }
        return publication;
    }

    public long[] microscopeRuntimeCurrentStats() {
        return withHandle(
                NativeCellularityControllerBridge::nativeMicroscopeRuntimeCurrentStats);
    }

    public byte[] microscopeRuntimeSelectedGenomeDraft() {
        return withHandle(
                NativeCellularityControllerBridge::nativeMicroscopeRuntimeSelectedGenomeDraft);
    }

    void setMicroscopeTemperatureMode(
            NativeRenderEndpointOwner owner,
            int temperatureMode) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            nativeSetMicroscopeTemperatureMode(
                    endpoint.getHandle(), temperatureMode);
        }
    }

    void setMicroscopeToolOrdinal(
            NativeRenderEndpointOwner owner,
            int toolOrdinal) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            nativeSetMicroscopeToolOrdinal(endpoint.getHandle(), toolOrdinal);
        }
    }

    void setMicroscopeSlowMotionSpeed(
            NativeRenderEndpointOwner owner,
            float slowMotionSpeed) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            nativeSetMicroscopeSlowMotionSpeed(
                    endpoint.getHandle(), slowMotionSpeed);
        }
    }

    void setMicroscopeIncubateTargetUps(
            NativeRenderEndpointOwner owner,
            double targetUps) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            nativeSetMicroscopeIncubateTargetUps(
                    endpoint.getHandle(), targetUps);
        }
    }

    void setMicroscopePerformanceHudEnabled(
            NativeRenderEndpointOwner owner,
            boolean enabled) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            nativeSetMicroscopePerformanceHudEnabled(
                    endpoint.getHandle(), enabled);
        }
    }

    long publishMicroscopePlacementGenome(
            NativeRenderEndpointOwner owner,
            byte[] genomeDraftBytes) {
        if (genomeDraftBytes == null) {
            throw new IllegalArgumentException(
                    "Microscope placement genome is required");
        }
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            return nativePublishMicroscopePlacementGenome(
                    endpoint.getHandle(), genomeDraftBytes);
        }
    }

    void submitMicroscopeInputBatch(
            NativeRenderEndpointOwner owner,
            long surfaceToken,
            ByteBuffer input,
            ByteBuffer result,
            long expectedPlacementGenomeRevision) {
        if (result == null || !result.isDirect() || result.isReadOnly()
                || result.capacity() < 320) {
            throw new IllegalArgumentException(
                    "Microscope input result buffer must be writable, direct, and at least 320 bytes");
        }
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            recordBoundaryJniCall(
                    RenderPerformanceTelemetry.JNI_MICROSCOPE_INPUT_COMMAND);
            nativeSubmitMicroscopeInputBatch(
                    endpoint.getHandle(),
                    surfaceToken,
                    input,
                    result,
                    expectedPlacementGenomeRevision);
        }
    }

    void readMicroscopePresentationMotionSnapshot(
            NativeRenderEndpointOwner owner,
            ByteBuffer result) {
        if (result == null || !result.isDirect() || result.isReadOnly()
                || result.capacity() < 32) {
            throw new IllegalArgumentException(
                    "Presentation motion result buffer must be writable, direct, and at least 32 bytes");
        }
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            nativeReadMicroscopePresentationMotionSnapshot(
                    endpoint.getHandle(), result);
        }
    }

    boolean requestMicroscopeInputResync(
            NativeRenderEndpointOwner owner,
            long surfaceToken) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            return nativeRequestMicroscopeInputResync(
                    endpoint.getHandle(), surfaceToken);
        }
    }

    long microscopeActiveInputEpoch(
            NativeRenderEndpointOwner owner,
            long surfaceToken) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            return nativeMicroscopeActiveInputEpoch(
                    endpoint.getHandle(), surfaceToken);
        }
    }

    long completeMicroscopeInputResync(
            NativeRenderEndpointOwner owner,
            long surfaceToken, boolean activePointersDrained) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            return nativeCompleteMicroscopeInputResync(
                    endpoint.getHandle(), surfaceToken,
                    activePointersDrained);
        }
    }

    int microscopeActivePointerGenerationCount(
            NativeRenderEndpointOwner owner) {
        try (NativeRenderEndpointOwner.Borrow endpoint =
                     requireRenderBorrow(owner)) {
            return nativeMicroscopeActivePointerGenerationCount(
                    endpoint.getHandle());
        }
    }

    public synchronized double time() {
        return nativeTime(requireHandle());
    }

    public synchronized long completedUpdateCount() {
        return nativeCompletedUpdateCount(requireHandle());
    }

    public double radius() {
        return radius;
    }

    public synchronized byte[] environmentStateBytes() {
        long handle = requireHandle();
        recordBoundaryJniCall(RenderPerformanceTelemetry.JNI_ENVIRONMENT_SNAPSHOT);
        return nativeEnvironmentState(handle);
    }

    public synchronized byte[] applyEnvironmentPatch(byte[] patchBytes) {
        if (patchBytes == null) {
            throw new IllegalArgumentException("Environment patch bytes are required");
        }
        long handle = requireHandle();
        recordBoundaryJniCall(RenderPerformanceTelemetry.JNI_APPLY_ENVIRONMENT_PATCH);
        return nativeApplyEnvironmentPatch(handle, patchBytes);
    }

    public synchronized long writeSubstratePath(String substratePath) {
        return nativeWriteSubstratePath(requireHandle(), substratePath);
    }

    public void discardSubstratePreviewFrame(long token) {
        if (token != 0L) {
            withHandleAction(handle -> nativeDiscardSubstratePreviewFrame(handle, token));
        }
    }

    public static void attachSubstratePreviewPath(
            String substratePath,
            byte[] webpBytes) {
        if (webpBytes == null) {
            throw new IllegalArgumentException("Substrate preview bytes are required");
        }
        nativeAttachSubstratePreviewPath(substratePath, webpBytes);
    }

    public static byte[] readSubstratePreviewPath(String substratePath) {
        return nativeReadSubstratePreviewPath(substratePath);
    }

    public synchronized int cellCount() {
        return nativeCellCount(requireHandle());
    }

    public synchronized double foodMass() {
        return nativeFoodMass(requireHandle());
    }

    public synchronized void reset() {
        nativeReset(requireHandle());
    }

    public synchronized byte[] cellSnapshotBytes(int cellId) {
        return nativeCellSnapshot(requireHandle(), cellId);
    }

    @Override
    public void close() {
        detachForClose().close();
    }

    private <T> T withHandle(HandleCall<T> call) {
        ReentrantReadWriteLock.ReadLock lock = lifetimeLock.readLock();
        lock.lock();
        try {
            return call.invoke(requireHandle());
        } finally {
            lock.unlock();
        }
    }

    private void withHandleAction(HandleAction action) {
        ReentrantReadWriteLock.ReadLock lock = lifetimeLock.readLock();
        lock.lock();
        try {
            action.invoke(requireHandle());
        } finally {
            lock.unlock();
        }
    }

    private void withHandleIfOpen(HandleAction action) {
        ReentrantReadWriteLock.ReadLock lock = lifetimeLock.readLock();
        lock.lock();
        try {
            if (nativeHandle != 0) {
                action.invoke(nativeHandle);
            }
        } finally {
            lock.unlock();
        }
    }

    private interface HandleCall<T> {
        T invoke(long handle);
    }

    private interface HandleAction {
        void invoke(long handle);
    }

    private long requireHandle() {
        if (nativeHandle == 0) {
            throw new IllegalStateException("NativeCellularityControllerBridge is closed");
        }
        return nativeHandle;
    }

    private static long requireGenomePreviewEndpointGeneration(
            long controllerHandle) {
        long generation =
                nativeResolveGenomePreviewEndpointGeneration(controllerHandle);
        if (generation == 0L) {
            throw new IllegalStateException(
                    "Genome Preview endpoint generation is unavailable");
        }
        return generation;
    }

    private static void requireGenomePreviewResultBuffer(ByteBuffer resultBuffer) {
        if (resultBuffer == null
                || !resultBuffer.isDirect()
                || resultBuffer.isReadOnly()
                || resultBuffer.capacity() < 24) {
            throw new IllegalArgumentException(
                    "Genome Preview result buffer must be writable, direct, and at least 24 bytes");
        }
    }

    private static void requireGenomePreviewAcknowledgmentBuffer(
            ByteBuffer acknowledgmentBuffer) {
        if (acknowledgmentBuffer == null
                || !acknowledgmentBuffer.isDirect()
                || acknowledgmentBuffer.isReadOnly()
                || acknowledgmentBuffer.capacity() < 112) {
            throw new IllegalArgumentException(
                    "Genome Preview acknowledgment buffer must be writable, direct, and at least 112 bytes");
        }
    }

    private static NativePreviewEndpointOwner.Borrow requirePreviewBorrow(
            NativePreviewEndpointOwner owner) {
        if (owner == null) {
            throw new IllegalStateException(
                    "Native preview endpoint owner is closed");
        }
        NativePreviewEndpointOwner.Borrow borrow = owner.borrow();
        if (borrow == null) {
            throw new IllegalStateException("Native preview endpoint is closed");
        }
        return borrow;
    }

    private static NativeRenderEndpointOwner.Borrow requireRenderBorrow(
            NativeRenderEndpointOwner owner) {
        if (owner == null) {
            throw new IllegalStateException(
                    "Native microscope render endpoint owner is closed");
        }
        NativeRenderEndpointOwner.Borrow borrow = owner.borrow();
        if (borrow == null) {
            throw new IllegalStateException(
                    "Native microscope render endpoint is closed");
        }
        return borrow;
    }

    private static void recordBoundaryJniCall(int kind) {
        if (BuildConfig.ENABLE_RENDER_PROFILING) {
            RenderPerformanceTelemetry.recordBoundaryJniCall(kind);
        }
    }

    private static native long nativeCreate(boolean previewMode,
                                            int maxCellCount,
                                            int maxFoodCount,
                                            double radius,
                                            double innerRadius,
                                            long createdAtUnixMillis);

    private static native long[] nativeRuntimeLimits();

    private static native boolean nativeIsConfigurationSupported(int maxCellCount,
                                                                  int maxFoodCount,
                                                                  double radius);

    private static native long nativeEstimateWorldAllocationBytes(int maxCellCount,
                                                                   int maxFoodCount,
                                                                   double radius);

    private static native long nativeCreateFromSubstratePath(boolean previewMode,
                                                              String substratePath,
                                                              int fallbackMaxCellCount,
                                                              int fallbackMaxFoodCount,
                                                              double fallbackRadius,
                                                              long fallbackCreatedAtUnixMillis);

    private static native byte[] nativeInspectSubstratePath(String substratePath,
                                                            boolean previewMode,
                                                            int fallbackMaxCellCount,
                                                            int fallbackMaxFoodCount,
                                                            double fallbackRadius);

    private static native void nativeValidateSubstratePath(String substratePath,
                                                           boolean previewMode,
                                                           int fallbackMaxCellCount,
                                                           int fallbackMaxFoodCount,
                                                           double fallbackRadius);

    private static native void nativeValidateGenomeDraft(byte[] genomeDraftBytes);

    private static native byte[] nativeEncodeReleaseGenome(
            byte[] genomeDraftBytes,
            byte[] previewBytes,
            int previewCodec,
            int previewWidth,
            int previewHeight);

    private static native byte[] nativeDecodeReleaseGenome(byte[] releaseGenomeBytes);

    private static native long nativeWriteSubstratePath(long handle, String substratePath);

    private static native void nativeDiscardSubstratePreviewFrame(long handle, long token);

    private static native void nativeAttachSubstratePreviewPath(
            String substratePath,
            byte[] webpBytes);

    private static native byte[] nativeReadSubstratePreviewPath(String substratePath);

    private static native void nativeDestroy(long handle);

    private static native long nativeControllerGeneration(long handle);

    private static native long nativeCommitControllerReplacement(
            long liveHandle,
            long candidateHandle,
            long expectedControllerGeneration);

    private static native long nativeDetachControllerForClose(long handle);

    private static native void nativeCloseRetiredController(long handle);

    private static native void nativeStartMicroscopeRuntime(long handle);

    private static native void nativeSetMicroscopeLifecyclePaused(long handle, boolean paused);

    private static native long nativeMicroscopeRuntimePublicationIntervalMillis();

    private static native NativeMicroscopeRuntimePublication
    nativePollMicroscopeRuntimePublication(
            long handle,
            long lastRevision,
            long lastWorldEpoch,
            long lastEnvironmentRevision);

    private static native long[] nativeMicroscopeRuntimeCurrentStats(long handle);

    private static native byte[] nativeMicroscopeRuntimeSelectedGenomeDraft(long handle);

    private static native void nativeAttachRenderAssetsLease(
            long handle, long assetsLeaseHandle);

    private static native long nativeAuthorizeRenderWorker(
            long handle,
            long surfaceToken,
            long workerIdentity);

    private static native long[] nativeActivateRenderSurface(
            long handle,
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration,
            long platformSurfaceGeneration,
            int width,
            int height);

    private static native int nativeUpdateRenderSurfaceDimensions(
            long handle,
            long surfaceToken,
            int width,
            int height);

    private static native int nativeDeactivateRenderSurface(
            long handle,
            long surfaceToken);

    private static native int nativeMarkRenderWorkerClosing(
            long handle,
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration);

    private static native int nativeFinishRenderWorkerClose(
            long handle,
            long surfaceToken,
            long workerIdentity,
            long workerBindingGeneration);

    private static native void nativePreparePreviewSurfaceResources(
            long endpointHandle);

    private static native void nativeSurfaceDestroyed(long endpointHandle);

    private static native void nativeSetViewportSurfaceSize(long handle, int width, int height);

    private static native void nativeSetViewportCamera(long handle, float cameraX, float cameraY, float zoom);

    private static native void nativeMoveViewportByScreenDelta(long handle, float dx, float dy);

    private static native void nativeZoomViewportAt(long handle, float factor, float screenX, float screenY);

    private static native float[] nativeViewportState(long handle);

    private static native long nativeDrawGenomePreview(
            long handle,
            ByteBuffer acknowledgmentBuffer);

    private static native boolean nativeReadGenomePreviewAcknowledgment(
            long endpointHandle,
            long endpointGeneration,
            ByteBuffer acknowledgmentBuffer);

    private static native long nativeResolveGenomePreviewEndpointGeneration(
            long handle);

    private static native long nativeGenomePreviewSurfaceCreated(
            long endpointHandle);

    private static native void nativeGenomePreviewSurfaceDestroyed(
            long endpointHandle,
            long attachmentGeneration);

    private static native long nativeSubstratePreviewSurfaceCreated(
            long endpointHandle);

    private static native void nativeSubstratePreviewSurfaceDestroyed(
            long endpointHandle,
            long attachmentGeneration);

    private static native void nativePublishGenomePreviewContent(
            long handle,
            long endpointGeneration,
            byte[] genomeDraftBytes,
            double simulationTimeHours,
            long producerSequence,
            ByteBuffer resultBuffer);

    private static native void nativePublishGenomePreviewViewState(
            long handle,
            long endpointGeneration,
            int selectedGeneIndex,
            boolean gridEnabled,
            boolean treeEnabled,
            int viewportWidth,
            int viewportHeight,
            long producerSequence,
            ByteBuffer resultBuffer);

    private static native int nativeEnqueueGenomePreviewInteraction(
            long handle,
            long endpointGeneration,
            long attachmentGeneration,
            long completionToken,
            int kind,
            float first,
            float second,
            float third);

    private static native long nativeDrawEnvironmentFieldPreview(long handle);

    private static native long nativeDrawMicroscope(long handle, long surfaceToken);

    private static native long nativeClearMicroscopeNoRender(
            long handle, long surfaceToken);

    private static native void nativeSetMicroscopeTemperatureMode(long handle, int temperatureMode);

    private static native void nativeSetMicroscopeToolOrdinal(long handle, int toolOrdinal);

    private static native void nativeSetMicroscopeSlowMotionSpeed(long handle, float slowMotionSpeed);

    private static native void nativeSetMicroscopeIncubateTargetUps(long handle, double targetUps);

    private static native void nativeSetMicroscopePerformanceHudEnabled(long handle, boolean enabled);

    private static native long nativePublishMicroscopePlacementGenome(
            long endpointHandle,
            byte[] genomeDraftBytes);

    private static native void nativeSubmitMicroscopeInputBatch(
            long endpointHandle,
            long surfaceToken,
            ByteBuffer input,
            ByteBuffer result,
            long expectedPlacementGenomeRevision);

    private static native void nativeReadMicroscopePresentationMotionSnapshot(
            long handle,
            ByteBuffer result);

    private static native boolean nativeRequestMicroscopeInputResync(
            long endpointHandle,
            long surfaceToken);

    private static native long nativeMicroscopeActiveInputEpoch(
            long endpointHandle,
            long surfaceToken);

    private static native long nativeCompleteMicroscopeInputResync(
            long handle, long surfaceToken, boolean activePointersDrained);

    private static native int nativeMicroscopeActivePointerGenerationCount(long handle);

    private static native double nativeTime(long handle);

    private static native long nativeCompletedUpdateCount(long handle);

    private static native double nativeRadius(long handle);

    private static native byte[] nativeEnvironmentState(long handle);

    private static native byte[] nativeApplyEnvironmentPatch(long handle, byte[] patchBytes);

    private static native int nativeCellCount(long handle);

    private static native double nativeFoodMass(long handle);

    private static native void nativeReset(long handle);

    private static native byte[] nativeCellSnapshot(long handle, int cellId);
}
