package com.cytarch.cellularity;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import org.junit.Test;

public final class EnvironmentNativeContractTest {
  @Test
  public void runtimeEnvironmentContractUsesExactlyTwoBatchNativeMethods()
      throws Exception {
    Method snapshot = NativeCellularityControllerBridge.class.getDeclaredMethod(
        "nativeEnvironmentState", long.class);
    Method apply = NativeCellularityControllerBridge.class.getDeclaredMethod(
        "nativeApplyEnvironmentPatch", long.class, byte[].class);

    assertTrue(Modifier.isNative(snapshot.getModifiers()));
    assertTrue(Modifier.isNative(apply.getModifiers()));
    assertEquals(byte[].class, snapshot.getReturnType());
    assertEquals(byte[].class, apply.getReturnType());
  }

  @Test
  public void publicBatchWrappersHoldTheLifecycleMonitor() throws Exception {
    Method snapshot = NativeCellularityControllerBridge.class.getDeclaredMethod(
        "environmentStateBytes");
    Method apply = NativeCellularityControllerBridge.class.getDeclaredMethod(
        "applyEnvironmentPatch", byte[].class);

    assertTrue(Modifier.isSynchronized(snapshot.getModifiers()));
    assertTrue(Modifier.isSynchronized(apply.getModifiers()));
    assertEquals(byte[].class, snapshot.getReturnType());
    assertEquals(byte[].class, apply.getReturnType());
  }

  @Test
  public void legacyPerPropertyNativeAccessIsAbsent() {
    assertThrows(
        ClassNotFoundException.class,
        () -> Class.forName(
            "com.cytarch.cellularity.NativeEnvironmentAccess",
            false,
            EnvironmentNativeContractTest.class.getClassLoader()));
  }
}
