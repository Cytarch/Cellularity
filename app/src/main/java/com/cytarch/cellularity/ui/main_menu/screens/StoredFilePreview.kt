package com.cytarch.cellularity.ui.main_menu.screens

import android.graphics.BitmapFactory
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.ColorMatrix
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import com.cytarch.cellularity.platform.files.DecodedEmbeddedFilePreview
import com.cytarch.cellularity.shared.files.release.EmbeddedPreviewFormat
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Loads and decodes an embedded image only while its lazy collection item is composed. */
@Composable
internal fun StoredFilePreview(
    previewKey: Any,
    previewAvailable: Boolean,
    modifier: Modifier = Modifier,
    clipShape: Shape? = null,
    ignoreEmbeddedAlpha: Boolean = false,
    placeholderContent: @Composable BoxScope.() -> Unit = {},
    loadPreview: suspend () -> DecodedEmbeddedFilePreview?
) {
    val dimensions = CellularityDimensions.current
    val currentLoader by rememberUpdatedState(loadPreview)
    var image by remember(previewKey, previewAvailable) {
        mutableStateOf<ImageBitmap?>(null)
    }
    LaunchedEffect(previewKey, previewAvailable) {
        image = null
        if (!previewAvailable) return@LaunchedEffect
        image = currentLoader()?.let { preview ->
            withContext(Dispatchers.Default) { decodeEmbeddedPreview(preview) }
        }
    }

    val shape = clipShape ?: RoundedCornerShape(dimensions.cardCorner)
    val imageAlpha by animateFloatAsState(
        targetValue = if (image == null) 0f else 1f,
        animationSpec = tween(durationMillis = PREVIEW_FADE_DURATION_MILLIS),
        label = "stored-file-preview"
    )
    Box(
        modifier = modifier
            .clip(shape)
            .background(MaterialTheme.colorScheme.primaryContainer)
    ) {
        val preview = image
        if (preview == null) {
            placeholderContent()
        } else {
            Image(
                bitmap = preview,
                contentDescription = null,
                contentScale = ContentScale.Fit,
                colorFilter = if (ignoreEmbeddedAlpha) {
                    IGNORE_ALPHA_COLOR_FILTER
                } else {
                    null
                },
                modifier = Modifier
                    .fillMaxSize()
                    .graphicsLayer { alpha = imageAlpha }
            )
        }
    }
}

private fun decodeEmbeddedPreview(preview: DecodedEmbeddedFilePreview): ImageBitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeByteArray(preview.bytes, 0, preview.bytes.size, bounds)
    val expectedMimeType = when (preview.format) {
        EmbeddedPreviewFormat.PNG -> "image/png"
        EmbeddedPreviewFormat.JPEG -> "image/jpeg"
        EmbeddedPreviewFormat.WEBP_LOSSY,
        EmbeddedPreviewFormat.WEBP_LOSSLESS -> "image/webp"
        EmbeddedPreviewFormat.NONE,
        EmbeddedPreviewFormat.UNKNOWN -> return null
    }
    if (
        bounds.outMimeType != expectedMimeType ||
        bounds.outWidth != preview.pixelWidth ||
        bounds.outHeight != preview.pixelHeight ||
        bounds.outWidth <= 0 || bounds.outHeight <= 0
    ) {
        return null
    }

    var sampleSize = 1
    while (
        bounds.outWidth / sampleSize > PREVIEW_DECODE_EDGE_PIXELS ||
        bounds.outHeight / sampleSize > PREVIEW_DECODE_EDGE_PIXELS
    ) {
        sampleSize *= 2
    }
    val options = BitmapFactory.Options().apply {
        inSampleSize = sampleSize
        inPreferredConfig = android.graphics.Bitmap.Config.ARGB_8888
    }
    return BitmapFactory.decodeByteArray(
        preview.bytes,
        0,
        preview.bytes.size,
        options
    )?.asImageBitmap()
}

private const val PREVIEW_DECODE_EDGE_PIXELS = 512
private const val PREVIEW_FADE_DURATION_MILLIS = 160

// ColorMatrix offsets use byte-channel units; 1f would leave the image at 1/255 alpha.
private val IGNORE_ALPHA_COLOR_FILTER = ColorFilter.colorMatrix(
    ColorMatrix(
        floatArrayOf(
            1f, 0f, 0f, 0f, 0f,
            0f, 1f, 0f, 0f, 0f,
            0f, 0f, 1f, 0f, 0f,
            0f, 0f, 0f, 0f, 255f
        )
    )
)
