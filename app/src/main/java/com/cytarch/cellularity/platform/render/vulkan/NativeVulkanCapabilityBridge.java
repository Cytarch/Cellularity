package com.cytarch.cellularity.platform.render.vulkan;

import android.view.Surface;

import androidx.annotation.Keep;

import java.nio.charset.StandardCharsets;

/** Loads and invokes the optional Vulkan probe without affecting the GLES runtime. */
@Keep
public final class NativeVulkanCapabilityBridge {
    private NativeVulkanCapabilityBridge() {
    }

    public static String probe(Surface surface) {
        if (!NativeVulkanBackendLoader.isAvailable()) {
            return failureReport(false, "Could not load Vulkan probe library: "
                    + NativeVulkanBackendLoader.failureDescription());
        }
        if (surface == null || !surface.isValid()) {
            return failureReport(true, "Android probe surface is unavailable");
        }

        try {
            byte[] report = nativeProbe(surface);
            if (report == null) {
                return failureReport(true, "Native Vulkan probe returned no report");
            }
            return new String(report, StandardCharsets.UTF_8);
        } catch (RuntimeException | LinkageError throwable) {
            return failureReport(true, "Vulkan probe invocation failed: "
                    + describe(throwable));
        }
    }

    private static String failureReport(boolean loaderAvailable, String message) {
        return "schema=1\n"
                + "status=failed\n"
                + "loader_available=" + loaderAvailable + "\n"
                + "requested_api_version=1.0.0\n"
                + "instance_api_version_raw=0\n"
                + "instance_api_version=unknown\n"
                + "instance_version_query_result=VK_RESULT_UNKNOWN\n"
                + "instance_surface_extension=false\n"
                + "android_surface_extension=false\n"
                + "physical_device_count=0\n"
                + "error=" + sanitize(message) + "\n"
                + "live_vulkan_objects_after_probe=0\n";
    }

    private static String describe(Throwable throwable) {
        String message = throwable.getMessage();
        return throwable.getClass().getSimpleName()
                + (message == null || message.trim().isEmpty() ? "" : ": " + message);
    }

    private static String sanitize(String value) {
        return value.replace('\n', ' ').replace('\r', ' ').replace('\t', ' ');
    }

    @Keep
    private static native byte[] nativeProbe(Surface surface);
}
