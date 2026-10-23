package com.cytarch.cellularity.platform.render.vulkan;

import android.view.Surface;
import androidx.annotation.Keep;
import com.cytarch.cellularity.NativeCellularityLibraryLoader;
import java.nio.ByteBuffer;

/** Stable JNI names for the native-owned Vulkan render worker. */
@Keep
final class NativeVulkanRenderWorkerJni {
  static { NativeCellularityLibraryLoader.requireLoaded(); }

  private NativeVulkanRenderWorkerJni() {}

  static native long nativeCreateWorkerControlBlock(
      long assetLeaseHandle, long apiTableAddress, long contentEndpointHandle,
      long contentGeneration, int contentKind, int initialFramePacingMode,
      NativeVulkanWorkerStatusListener statusListener, long listenerGeneration);

  static native int nativeAuthorizeMicroscopeWorker(long workerHandle,
                                                    long attachmentLeaseHandle,
                                                    long surfaceToken);

  static native int nativeStartWorker(long workerHandle);

  static native int nativeAttachSurface(long workerHandle, Surface surface,
                                        int width, int height, long generation);

  static native int nativeResizeSurface(long workerHandle, int width,
                                        int height, long generation);

  static native int nativeDetachSurface(long workerHandle, long generation);

  static native int nativeResume(long workerHandle);

  static native int nativePause(long workerHandle);

  static native int nativeRequestFrame(long workerHandle);

  static native int nativeSetFramePacingMode(long workerHandle, int mode);

  static native int nativeSetDemoPerformanceHudEnabled(long workerHandle,
                                                       boolean enabled);

  static native int nativeReadCompactStatus(long workerHandle,
                                            ByteBuffer destination);

  static native int nativeRequestDiagnostics(long workerHandle, long requestId);

  static native int nativeReadDiagnosticsChunk(long workerHandle,
                                               long requestId,
                                               long expectedRevision,
                                               long offset,
                                               ByteBuffer destination);

  static native int nativeReleaseDiagnostics(long workerHandle, long requestId,
                                             long expectedRevision);

  static native void nativeRequestStop(long workerHandle);

  static native int nativeAwaitStopped(long workerHandle, long timeoutMillis);

  static native boolean nativeFinalizeWorkerBinding(long workerHandle);

  static native void nativeDestroyStoppedWorker(long workerHandle);
}
