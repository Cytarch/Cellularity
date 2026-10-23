package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GenomeEditorLayoutTest {
    @Test
    fun consecutiveResizeDeltasAccumulateInsteadOfSnappingBack() {
        var extent = 250f

        extent = resizedPreviewExtent(extent, 40f, 20f, 800f)
        extent = resizedPreviewExtent(extent, 40f, 20f, 800f)

        assertEquals(330f, extent, 0f)
    }

    @Test
    fun resizedPreviewExtentIsClampedToAvailableBounds() {
        assertEquals(20f, resizedPreviewExtent(40f, -100f, 20f, 800f), 0f)
        assertEquals(800f, resizedPreviewExtent(780f, 100f, 20f, 800f), 0f)
        assertEquals(20f, resizedPreviewExtent(250f, 0f, 20f, 0f), 0f)
    }

    @Test
    fun compactHeightReservesUsableEditorArea() {
        assertEquals(
            360f,
            constrainedPreviewExtent(
                requestedExtentPx = 500f,
                parentExtentPx = 640f,
                minimumExtentPx = 40f,
                reservedSecondaryExtentPx = 280f
            ),
            0f
        )
    }

    @Test
    fun normalHeightKeepsRequestedPreviewSize() {
        assertEquals(
            500f,
            constrainedPreviewExtent(
                requestedExtentPx = 500f,
                parentExtentPx = 1200f,
                minimumExtentPx = 40f,
                reservedSecondaryExtentPx = 280f
            ),
            0f
        )
    }

    @Test
    fun expandedPreviewCanReachTheOuterPlayTabBar() {
        assertEquals(
            640f,
            constrainedPreviewExtent(
                requestedExtentPx = 800f,
                parentExtentPx = 640f,
                minimumExtentPx = 20f,
                reservedSecondaryExtentPx = 0f
            ),
            0f
        )
    }

    @Test
    fun landscapeUsesSideBySideEditorLayout() {
        assertTrue(useSideBySideGenomeEditorLayout(widthPx = 1200f, heightPx = 700f))
        assertFalse(useSideBySideGenomeEditorLayout(widthPx = 700f, heightPx = 1200f))
        assertFalse(useSideBySideGenomeEditorLayout(widthPx = 800f, heightPx = 800f))
    }

    @Test
    fun landscapePreviewWidthCanReachEitherAllowedBoundary() {
        assertEquals(280f, resizedPreviewExtent(500f, -400f, 280f, 1200f), 0f)
        assertEquals(1200f, resizedPreviewExtent(900f, 400f, 280f, 1200f), 0f)
    }

    @Test
    fun dividerChangesOnlyTheLogicalViewportOfTheStableBackingSurface() {
        val narrowLandscape = genomePreviewSurfaceGeometry(
            visibleWidthPx = 360f,
            visibleHeightPx = 700f,
            parentWidthPx = 1200f,
            parentHeightPx = 700f
        )
        val wideLandscape = genomePreviewSurfaceGeometry(
            visibleWidthPx = 840f,
            visibleHeightPx = 700f,
            parentWidthPx = 1200f,
            parentHeightPx = 700f
        )
        val shortPortrait = genomePreviewSurfaceGeometry(
            visibleWidthPx = 700f,
            visibleHeightPx = 360f,
            parentWidthPx = 700f,
            parentHeightPx = 1200f
        )
        val tallPortrait = genomePreviewSurfaceGeometry(
            visibleWidthPx = 700f,
            visibleHeightPx = 840f,
            parentWidthPx = 700f,
            parentHeightPx = 1200f
        )

        assertEquals(360, narrowLandscape.viewportWidth)
        assertEquals(840, wideLandscape.viewportWidth)
        assertEquals(1200, narrowLandscape.backingWidth)
        assertEquals(narrowLandscape.backingWidth, wideLandscape.backingWidth)
        assertEquals(360, shortPortrait.viewportHeight)
        assertEquals(840, tallPortrait.viewportHeight)
        assertEquals(1200, shortPortrait.backingHeight)
        assertEquals(shortPortrait.backingHeight, tallPortrait.backingHeight)
    }

    @Test
    fun previewSurfaceGeometryNeverPublishesZeroSizedNativeDimensions() {
        assertEquals(
            GenomePreviewSurfaceGeometry(1, 1, 1, 1),
            genomePreviewSurfaceGeometry(
                visibleWidthPx = 0f,
                visibleHeightPx = 0f,
                parentWidthPx = 0f,
                parentHeightPx = 0f
            )
        )
    }

    @Test
    fun minimumPreviewExtentNeverExceedsACompactWindow() {
        assertEquals(
            200f,
            constrainedPreviewExtent(
                requestedExtentPx = 280f,
                parentExtentPx = 200f,
                minimumExtentPx = 280f,
                reservedSecondaryExtentPx = 0f
            ),
            0f
        )
    }
}
