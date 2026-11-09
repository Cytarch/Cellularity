/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIndex
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorLayoutMemory
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorScreen
import org.jetbrains.compose.resources.StringResource
import org.jetbrains.compose.resources.getString

/**
 * The desktop's editor: the shared screen, plus the two things only a desktop can supply.
 *
 * The editor itself -- its sections, its pager, its gestures and its layout -- is the one in the
 * shared module, because it is the same editor the phone shows. What is left here is what a
 * desktop adds to it: a message shown in the window rather than in a toast, and the organism,
 * drawn into the scene instead of onto a platform surface.
 *
 * Both arrive at the shared screen as parameters rather than as branches inside it, so the editor
 * is one file for both platforms and each platform's part is one file of its own.
 */
@Composable
fun DesktopGenomeEditor(
    store: DesktopGenomeEditorStore,
    previewMaxCells: Int,
    previewCapture: DesktopGenomePreviewCapture? = null,
    modifier: Modifier = Modifier
) {
    val state = store.state
    // Where the person left the preview. Remembered for as long as the screen lives, which is what
    // the phone does too: the editor is left and returned to, and its shape should survive that.
    val layout = remember { GenomeEditorLayoutMemory() }
    val notices = remember { SnackbarHostState() }

    // A message is held rather than shown where it arrives, because reading a string resource is
    // suspending and the editor asks for the message while composing.
    var pendingMessage by remember { mutableStateOf<StringResource?>(null) }
    LaunchedEffect(pendingMessage) {
        val resource = pendingMessage ?: return@LaunchedEffect
        notices.showSnackbar(getString(resource))
        pendingMessage = null
    }

    Box(modifier = modifier.fillMaxSize()) {
        GenomeEditorScreen(
            state = state,
            onAction = store::dispatch,
            defaultGeneFactory = DesktopGenomeEditorStore::createRandomizedEditorGene,
            layout = layout,
            onMessage = { resource -> pendingMessage = resource },
            preview = { geometry, handle, metrics, previewModifier ->
                DesktopGenomePreview(
                    previewMaxCells = previewMaxCells,
                    previewCapture = previewCapture,
                    preview = state.preview,
                    layout = layout,
                    // A tap on the organism chooses the gene of the cell it landed on, which is
                    // the editor's own action rather than something the preview keeps to itself:
                    // the same selection the gene dropdown makes, made by pointing at a cell. The
                    // number arrives from the renderer, so it is checked before a type that
                    // promises a gene index is built from it.
                    onGeneSelected = { index ->
                        if (index in 0 until GenomeContract.MAX_GENE_COUNT) {
                            store.dispatch(
                                GenomeEditorAction.SelectGene(GeneIndex(index))
                            )
                        }
                    },
                    handle = handle,
                    metrics = metrics,
                    geometry = geometry,
                    modifier = previewModifier
                )
            }
        )
        SnackbarHost(
            hostState = notices,
            modifier = Modifier.align(Alignment.BottomCenter)
        )
    }
}
