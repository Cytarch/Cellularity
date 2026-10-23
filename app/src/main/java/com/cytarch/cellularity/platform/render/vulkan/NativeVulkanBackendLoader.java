package com.cytarch.cellularity.platform.render.vulkan;

import androidx.annotation.Keep;

/** Owns the single process-wide load and API-table lookup for the optional Vulkan DSO. */
@Keep
public final class NativeVulkanBackendLoader {
    private static final LoadResult LOAD_RESULT = load();

    private NativeVulkanBackendLoader() {}

    public static boolean isAvailable() {
        return LOAD_RESULT.available;
    }

    public static String failureDescription() {
        return LOAD_RESULT.failure;
    }

    public static long backendApiV1Address() {
        return LOAD_RESULT.apiAddress;
    }

    private static LoadResult load() {
        try {
            System.loadLibrary("cellularity_vulkan");
            long address = nativeBackendApiV1Address();
            if (address == 0L) {
                return new LoadResult(false, 0L,
                        "Vulkan backend returned a null API V1 address");
            }
            return new LoadResult(true, address, "");
        } catch (UnsatisfiedLinkError | SecurityException error) {
            return new LoadResult(false, 0L, describe(error));
        }
    }

    private static String describe(Throwable throwable) {
        String message = throwable.getMessage();
        String type = throwable.getClass().getName();
        if (message == null) return type;
        message = message.replace('\r', ' ').replace('\n', ' ').trim();
        return message.isEmpty() ? type : type + ": " + message;
    }

    @Keep
    private static native long nativeBackendApiV1Address();

    private static final class LoadResult {
        final boolean available;
        final long apiAddress;
        final String failure;

        LoadResult(boolean available, long apiAddress, String failure) {
            this.available = available;
            this.apiAddress = apiAddress;
            this.failure = failure;
        }
    }
}
