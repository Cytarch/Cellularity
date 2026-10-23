package com.cytarch.cellularity;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;

import org.junit.Test;

public final class NativeCellularityLibraryLoaderTest {
    @Test
    public void unavailableMainDsoRetainsOneSanitizedFailureWithoutRetry() {
        if (NativeCellularityLibraryLoader.isAvailable()) {
            assertEquals("", NativeCellularityLibraryLoader.failureDescription());
            NativeCellularityLibraryLoader.requireLoaded();
            return;
        }

        String firstDescription =
                NativeCellularityLibraryLoader.failureDescription();
        IllegalStateException first = assertThrows(
                IllegalStateException.class,
                NativeCellularityLibraryLoader::requireLoaded);
        IllegalStateException second = assertThrows(
                IllegalStateException.class,
                NativeCellularityLibraryLoader::requireLoaded);

        assertNotNull(first.getCause());
        assertSame(first.getCause(), second.getCause());
        assertEquals(firstDescription,
                NativeCellularityLibraryLoader.failureDescription());
        assertFalse(firstDescription.contains("\n"));
        assertFalse(firstDescription.contains("\r"));
    }
}
