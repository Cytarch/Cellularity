package com.cytarch.cellularity;

/** Owns the single process/classloader attempt to load the required main DSO. */
public final class NativeCellularityLibraryLoader {
    private static final Throwable LOAD_FAILURE;

    static {
        Throwable failure = null;
        try {
            System.loadLibrary("cellularity");
        } catch (UnsatisfiedLinkError | SecurityException error) {
            failure = error;
        }
        LOAD_FAILURE = failure;
    }

    private NativeCellularityLibraryLoader() {}

    public static boolean isAvailable() {
        return LOAD_FAILURE == null;
    }

    public static String failureDescription() {
        if (LOAD_FAILURE == null) {
            return "";
        }
        String message = LOAD_FAILURE.getMessage();
        if (message == null) {
            return LOAD_FAILURE.getClass().getName();
        }
        message = message.replace('\r', ' ').replace('\n', ' ').trim();
        if (message.isEmpty()) {
            return LOAD_FAILURE.getClass().getName();
        }
        return LOAD_FAILURE.getClass().getName() + ": " + message;
    }

    public static void requireLoaded() {
        if (LOAD_FAILURE != null) {
            throw new IllegalStateException(
                    "Required native library cellularity is unavailable",
                    LOAD_FAILURE);
        }
    }
}
