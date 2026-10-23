package com.cytarch.cellularity.platform.render;

import androidx.annotation.Keep;
import com.cytarch.cellularity.NativeCellularityLibraryLoader;
import java.nio.ByteBuffer;

/** Cold process-wide bridge for native Vulkan profiling state. */
@Keep
final class NativeRenderProfilingBridge {
  private NativeRenderProfilingBridge() {}

  static int publishRenderProfilingConfiguration(boolean enabled,
                                                 long resetGeneration) {
    NativeCellularityLibraryLoader.requireLoaded();
    return nativePublishRenderProfilingConfiguration(enabled, resetGeneration);
  }

  static int readRenderProfilingSnapshot(ByteBuffer destination) {
    NativeCellularityLibraryLoader.requireLoaded();
    return nativeReadRenderProfilingSnapshot(destination);
  }

  private static native int
  nativePublishRenderProfilingConfiguration(boolean enabled,
                                            long resetGeneration);

  private static native int
  nativeReadRenderProfilingSnapshot(ByteBuffer destination);
}
