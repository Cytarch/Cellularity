/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.asComposeImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.plate
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.jetbrains.compose.resources.painterResource
import org.jetbrains.skia.Bitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorSpace
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.ImageInfo
import org.jetbrains.skia.Image as SkiaImage

/**
 * The picture on a plate's card, as the plate itself carries it.
 *
 * The phone decodes the container's embedded preview with the platform's own bitmap reader; the
 * desktop has no such reader, so it decodes with the Skia the interface is drawn with. What is
 * decoded is the same bytes either way, which matters more than it sounds: a card that showed a
 * stale thumbnail -- or the wrong plate's picture -- would be a description of a file nobody can
 * check, and the preview is the one part of a plate a person recognises at a glance.
 *
 * Absence is drawn as the plate placeholder rather than as an empty box, because "this plate has no
 * picture" and "this card failed to load" look the same when the answer is nothing at all.
 */
@Composable
fun DesktopStoredSubstratePreview(
    id: StoredSubstrateId,
    previewAvailable: Boolean,
    repository: DesktopCellularityFileRepository,
    modifier: Modifier = Modifier,
    clipShape: Shape? = null
) {
    val currentRepository by rememberUpdatedState(repository)
    var image by remember(id, previewAvailable) { mutableStateOf<ImageBitmap?>(null) }
    LaunchedEffect(id, previewAvailable) {
        image = null
        if (!previewAvailable) return@LaunchedEffect
        val bytes = when (val result = currentRepository.readSubstratePreview(id)) {
            is StorageResult.Success -> result.value
            is StorageResult.Failure -> null
        } ?: return@LaunchedEffect
        image = withContext(Dispatchers.Default) { decodePreview(bytes) }
    }

    val shape = clipShape ?: RoundedCornerShape(12.dp)
    Box(
        modifier = modifier
            .clip(shape)
            .background(MaterialTheme.colorScheme.primaryContainer)
    ) {
        val picture = image
        if (picture == null) {
            Icon(
                painter = painterResource(Res.drawable.plate),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier
                    .align(Alignment.Center)
                    .size(48.dp)
                    .alpha(0.55f)
            )
        } else {
            Image(
                bitmap = picture,
                contentDescription = null,
                contentScale = ContentScale.Fit,
                modifier = Modifier.fillMaxSize()
            )
        }
    }
}

/**
 * Decodes the container's preview bytes, or null when they are not a picture.
 *
 * Skia answers a format it has no decoder for with nothing, so a preview written by a build with a
 * codec this one lacks -- or a corrupt one -- is an absent picture rather than a fault. The
 * alternative would be a card that fails to draw because an ornament could not be read.
 *
 * The pixels are copied into a bitmap of this side's own rather than drawn from the decoded image
 * directly: the compressed bytes are freed with the image, and the card keeps its picture for as
 * long as the card is on screen. A successful conversion leaves the bitmap alive inside the Compose
 * wrapper; closing it here would leave that wrapper with a null native pointer on its first draw.
 */
private fun decodePreview(bytes: ByteArray): ImageBitmap? {
    val decoded = try {
        SkiaImage.makeFromEncoded(bytes)
    } catch (failure: RuntimeException) {
        return null
    }
    if (decoded.width <= 0 || decoded.height <= 0) return null
    val bitmap = Bitmap()
    return try {
        bitmap.allocPixels(
            ImageInfo(
                decoded.width,
                decoded.height,
                ColorType.RGBA_8888,
                ColorAlphaType.PREMUL,
                ColorSpace.sRGB
            )
        )
        if (!decoded.readPixels(bitmap, 0, 0)) {
            bitmap.close()
            null
        } else {
            bitmap.asComposeImageBitmap()
        }
    } catch (failure: RuntimeException) {
        bitmap.close()
        null
    } finally {
        decoded.close()
    }
}
