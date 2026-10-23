package com.cytarch.cellularity;

import android.content.Context;
import com.cytarch.cellularity.platform.render.AndroidRenderAssetsOwner;
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest;
import com.cytarch.cellularity.platform.render.preview.NativePreviewEndpointOwner;
import com.cytarch.cellularity.shared.model.SimulationConfiguration;
import com.cytarch.cellularity.shared.model.SimulationStats;
import java.nio.ByteBuffer;

public final class NativeSimulationFacade
    implements AutoCloseable {
  private final Context context;
  private final boolean previewMode;
  private final SimulationConfiguration fallbackConfiguration;
  private final AndroidRenderAssetsOwner renderAssetsOwner;
  private volatile boolean microscopeRuntimeStarted;
  private volatile long microscopePublicationSourceGeneration = 1L;
  private boolean closed;
  private final NativeCellularityControllerBridge bridge;
  private final NativeMicroscopeRenderEndpoint microscopeRenderEndpoint;

  public static final class PreparedControllerReplacement implements AutoCloseable {
    private NativeCellularityControllerBridge.PreparedControllerReplacement delegate;

    private PreparedControllerReplacement(
        NativeCellularityControllerBridge.PreparedControllerReplacement delegate) {
      this.delegate = delegate;
    }

    private synchronized NativeCellularityControllerBridge.PreparedControllerReplacement
        requireDelegate() {
      if (delegate == null) {
        throw new IllegalStateException("Prepared controller replacement was consumed");
      }
      return delegate;
    }

    private synchronized void markCommitted() {
      delegate = null;
    }

    @Override
    public void close() {
      NativeCellularityControllerBridge.PreparedControllerReplacement current;
      synchronized (this) {
        current = delegate;
        delegate = null;
      }
      if (current != null) current.close();
    }
  }

  public static final class RetiredController implements AutoCloseable {
    private NativeCellularityControllerBridge.RetiredController delegate;
    private AndroidRenderAssetsOwner terminalAssetsOwner;

    private RetiredController(
        NativeCellularityControllerBridge.RetiredController delegate,
        AndroidRenderAssetsOwner terminalAssetsOwner) {
      this.delegate = delegate;
      this.terminalAssetsOwner = terminalAssetsOwner;
    }

    private synchronized void initialize(
        NativeCellularityControllerBridge.RetiredController retired) {
      initialize(retired, null);
    }

    private synchronized void initialize(
        NativeCellularityControllerBridge.RetiredController retired,
        AndroidRenderAssetsOwner assetsOwner) {
      if (retired == null || delegate != null || terminalAssetsOwner != null) {
        throw new IllegalStateException("Retired controller was already initialized");
      }
      delegate = retired;
      terminalAssetsOwner = assetsOwner;
    }

    @Override
    public void close() {
      NativeCellularityControllerBridge.RetiredController current;
      AndroidRenderAssetsOwner assets;
      synchronized (this) {
        current = delegate;
        assets = terminalAssetsOwner;
        delegate = null;
        terminalAssetsOwner = null;
      }
      try {
        if (current != null) current.close();
      } finally {
        if (assets != null) assets.close();
      }
    }
  }

  public NativeSimulationFacade(Context context, boolean previewMode,
                                int maxCellCount, int maxFoodCount,
                                double radius) {
    this(context, previewMode, maxCellCount, maxFoodCount, radius, 0.0);
  }

  public NativeSimulationFacade(Context context, boolean previewMode,
                                int maxCellCount, int maxFoodCount,
                                double radius, double innerRadius) {
    this.context = context.getApplicationContext();
    this.previewMode = previewMode;
    this.fallbackConfiguration = new SimulationConfiguration(
        maxCellCount, maxFoodCount, radius, innerRadius);
    this.renderAssetsOwner = new AndroidRenderAssetsOwner(
        this.context.getAssets());
    NativeCellularityControllerBridge createdBridge = null;
    NativeMicroscopeRenderEndpoint createdRenderEndpoint = null;
    try {
      createdBridge = new NativeCellularityControllerBridge(
          this.context, previewMode, maxCellCount, maxFoodCount, radius, innerRadius);
      this.renderAssetsOwner.attachToController(createdBridge);
      createdRenderEndpoint = createdBridge.createMicroscopeRenderEndpoint();
    } catch (Throwable failure) {
      if (createdRenderEndpoint != null) createdRenderEndpoint.close();
      if (createdBridge != null) createdBridge.close();
      this.renderAssetsOwner.close();
      throw failure;
    }
    this.bridge = createdBridge;
    this.microscopeRenderEndpoint = createdRenderEndpoint;
  }

  public NativeMicroscopeRenderEndpoint microscopeRenderEndpoint() {
    return microscopeRenderEndpoint;
  }

  public synchronized long drawGenomePreview(ByteBuffer acknowledgmentBuffer) {
    return bridge.drawGenomePreview(acknowledgmentBuffer);
  }

  public boolean readGenomePreviewAcknowledgment(
      ByteBuffer acknowledgmentBuffer) {
    return bridge.readGenomePreviewAcknowledgment(acknowledgmentBuffer);
  }

  public void publishGenomePreviewContent(
      byte[] genomeDraftBytes, double simulationTimeHours,
      long producerSequence, ByteBuffer resultBuffer) {
    bridge.publishGenomePreviewContent(
        genomeDraftBytes, simulationTimeHours, producerSequence, resultBuffer);
  }

  public void publishGenomePreviewViewState(
      int selectedGeneIndex, boolean gridEnabled, boolean treeEnabled,
      int viewportWidth, int viewportHeight,
      long producerSequence, ByteBuffer resultBuffer) {
    bridge.publishGenomePreviewViewState(
        selectedGeneIndex, gridEnabled, treeEnabled,
        viewportWidth, viewportHeight, producerSequence, resultBuffer);
  }

  public int enqueueGenomePreviewInteraction(
      long completionToken, int kind, float first, float second, float third) {
    return bridge.enqueueGenomePreviewInteraction(
        completionToken, kind, first, second, third);
  }

  public NativePreviewCaptureRequest enqueueGenomePreviewCapture(
      long requestId, byte[] genomeDraftBytes, double maximumSimulationTime) {
    return bridge.enqueueGenomePreviewCapture(
        requestId, genomeDraftBytes, maximumSimulationTime);
  }

  public long genomePreviewEndpointGeneration() {
    return bridge.genomePreviewEndpointGeneration();
  }

  public NativePreviewEndpointOwner.Borrow borrowGenomePreviewEndpoint() {
    return bridge.borrowGenomePreviewEndpoint();
  }

  public NativePreviewEndpointOwner.Borrow borrowEnvironmentFieldPreviewEndpoint() {
    return bridge.borrowEnvironmentFieldPreviewEndpoint();
  }

  public long environmentFieldPreviewEndpointGeneration() {
    return bridge.environmentFieldPreviewEndpointGeneration();
  }

  public synchronized long drawEnvironmentFieldPreview() {
    return bridge.drawEnvironmentFieldPreview();
  }

  public synchronized void publishEnvironmentFieldPreviewState(
      int targetWire, int fieldTypeWire, double base, double strength,
      double angle, double offset, double softness, ByteBuffer resultBuffer) {
    bridge.publishEnvironmentFieldPreviewState(
        targetWire, fieldTypeWire, base, strength, angle, offset, softness,
        resultBuffer);
  }

  public void startMicroscopeRuntime() {
    bridge.startMicroscopeRuntime();
    microscopeRuntimeStarted = true;
  }

  public void setMicroscopeLifecyclePaused(boolean paused) {
    bridge.setMicroscopeLifecyclePaused(paused);
  }

  public long microscopeRuntimePublicationIntervalMillis() {
    return NativeCellularityControllerBridge.microscopeRuntimePublicationIntervalMillis();
  }

  public NativeMicroscopeRuntimePublication pollMicroscopeRuntimePublication(
      long lastRevision,
      long lastWorldEpoch,
      long lastEnvironmentRevision) {
    return bridge.pollMicroscopeRuntimePublication(
        lastRevision, lastWorldEpoch, lastEnvironmentRevision);
  }

  public long microscopePublicationSourceGeneration() {
    return microscopePublicationSourceGeneration;
  }

  public synchronized double radius() {
    return bridge.radius();
  }

  public synchronized byte[] environmentStateBytes() {
    return bridge.environmentStateBytes();
  }

  public synchronized byte[] applyEnvironmentPatch(byte[] patchBytes) {
    return bridge.applyEnvironmentPatch(patchBytes);
  }

  public synchronized SimulationStats stats() {
    NativeCellularityControllerBridge current = bridge;
    if (microscopeRuntimeStarted) {
      long[] values = current.microscopeRuntimeCurrentStats();
      if (values == null || values.length != 4) {
        throw new IllegalStateException("Native microscope stats are unavailable");
      }
      return new SimulationStats(
          (int) values[0], Double.longBitsToDouble(values[1]),
          Double.longBitsToDouble(values[2]), values[3]);
    }
    synchronized (current) {
      return new SimulationStats(
          current.cellCount(), current.foodMass(), current.time(),
          current.completedUpdateCount());
    }
  }

  public synchronized void reset() {
    bridge.reset();
  }

  public PreparedControllerReplacement prepareSubstrateReplacement(
      String substratePath) {
    synchronized (this) {
      ensureOpen();
    }
    NativeCellularityControllerBridge.PreparedControllerReplacement candidate =
        bridge.prepareSubstrateReplacement(
            previewMode, substratePath, fallbackConfiguration);
    boolean prepared = false;
    try {
      renderAssetsOwner.attachToPreparedController(candidate);
      synchronized (this) {
        ensureOpen();
      }
      prepared = true;
      return new PreparedControllerReplacement(candidate);
    } finally {
      if (!prepared) candidate.close();
    }
  }

  public synchronized RetiredController commitSubstrateReplacement(
      PreparedControllerReplacement prepared) {
    ensureOpen();
    if (microscopePublicationSourceGeneration == Long.MAX_VALUE) {
      throw new IllegalStateException(
          "Microscope publication source generation is exhausted");
    }
    long nextPublicationSourceGeneration =
        microscopePublicationSourceGeneration + 1L;
    RetiredController result = new RetiredController(null, null);
    NativeCellularityControllerBridge.PreparedControllerReplacement candidate =
        prepared.requireDelegate();
    NativeCellularityControllerBridge.RetiredController retired =
        bridge.commitReplacement(candidate);
    result.initialize(retired);
    boolean committed = false;
    try {
      prepared.markCommitted();
      microscopeRenderEndpoint.onControllerReplaced();
      microscopePublicationSourceGeneration = nextPublicationSourceGeneration;
      committed = true;
      return result;
    } finally {
      if (!committed) result.close();
    }
  }

  public void loadSubstratePath(String substratePath) {
    try (PreparedControllerReplacement prepared =
             prepareSubstrateReplacement(substratePath)) {
      try (RetiredController retired =
               commitSubstrateReplacement(prepared)) {
        // Compatibility path: normal gateway reload closes the retirement
        // owner explicitly after releasing its commit locks.
      }
    }
  }

  public synchronized long writeSubstratePath(String substratePath) {
    return bridge.writeSubstratePath(substratePath);
  }

  public void discardSubstratePreviewFrame(long token) {
    bridge.discardSubstratePreviewFrame(token);
  }

  public void attachSubstratePreviewPath(String substratePath, byte[] webpBytes) {
    NativeCellularityControllerBridge.attachSubstratePreviewPath(
        substratePath, webpBytes);
  }

  public synchronized byte[] cellSnapshotBytes(int cellId) {
    return bridge.cellSnapshotBytes(cellId);
  }

  public synchronized byte[] selectedGenomeDraftBytes() {
    return bridge.microscopeRuntimeSelectedGenomeDraft();
  }

  public synchronized RetiredController detachForClose() {
    if (closed) return new RetiredController(null, null);
    RetiredController result = new RetiredController(null, null);
    closed = true;
    NativeCellularityControllerBridge.RetiredController retired = null;
    Throwable failure = null;
    try {
      microscopeRenderEndpoint.close();
    } catch (Throwable endpointFailure) {
      failure = endpointFailure;
    }
    try {
      retired = bridge.detachForClose();
    } catch (Throwable bridgeFailure) {
      if (failure == null) {
        failure = bridgeFailure;
      } else if (failure != bridgeFailure) {
        failure.addSuppressed(bridgeFailure);
      }
    }
    if (failure == null) {
      result.initialize(retired, renderAssetsOwner);
      return result;
    }
    if (retired != null) {
      try {
        retired.close();
      } catch (Throwable cleanupFailure) {
        failure.addSuppressed(cleanupFailure);
      }
    }
    try {
      renderAssetsOwner.close();
    } catch (Throwable cleanupFailure) {
      failure.addSuppressed(cleanupFailure);
    }
    rethrowUnchecked(failure);
    throw new AssertionError("Unreachable");
  }

  @Override
  public void close() {
    detachForClose().close();
  }

  private void ensureOpen() {
    if (closed)
      throw new IllegalStateException("NativeSimulationFacade is closed");
  }

  private static void rethrowUnchecked(Throwable failure) {
    if (failure instanceof RuntimeException) {
      throw (RuntimeException) failure;
    }
    if (failure instanceof Error) throw (Error) failure;
    throw new IllegalStateException("Native simulation shutdown failed", failure);
  }

}
