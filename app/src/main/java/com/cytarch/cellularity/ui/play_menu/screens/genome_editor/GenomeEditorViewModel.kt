package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import androidx.lifecycle.ViewModel
import com.cytarch.cellularity.shared.model.EditorGeneTemplate
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeMetadata
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffect
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorReducer
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorState
import kotlin.random.Random
import kotlin.math.sqrt
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow

/** Android store for the common, pure Genome Editor reducer. */
class GenomeEditorViewModel(
    initialGenome: GenomeDraft = createRandomizedEditorGenome()
) : ViewModel() {
    private val mutableState = MutableStateFlow(GenomeEditorState.initial(initialGenome))
    private val effectChannel = Channel<GenomeEditorEffect>(Channel.UNLIMITED)

    val state: StateFlow<GenomeEditorState> = mutableState.asStateFlow()
    val effects = effectChannel.receiveAsFlow()

    val applicableGenome: GenomeDraft
        get() = mutableState.value.preview.snapshot

    @Synchronized
    fun dispatch(action: GenomeEditorAction) {
        val transition = GenomeEditorReducer.reduce(mutableState.value, action)
        mutableState.value = transition.state
        transition.effects.forEach { effect ->
            check(effectChannel.trySend(effect).isSuccess) {
                "Genome Editor effect channel is closed"
            }
        }
    }

    override fun onCleared() {
        effectChannel.close()
        super.onCleared()
    }

    companion object {
        fun createRandomizedEditorGene(geneIndex: Int): GeneDraft = EditorGeneTemplate.create(
            geneIndex = geneIndex,
            color = randomNormalizedColor(),
            metadata = GenomeMetadata(initialGeneIndex = 0)
        )

        fun createRandomizedEditorGenome(): GenomeDraft = GenomeDraft(
            List(GenomeContract.MIN_GENE_COUNT, ::createRandomizedEditorGene)
        )

        private fun randomNormalizedColor(): List<Float> {
            val color = FloatArray(GenomeContract.COLOR_COUNT) {
                Random.Default.nextFloat()
            }
            val magnitude = sqrt(color.sumOf { (it * it).toDouble() }).toFloat()
            return if (magnitude > 0.0f && magnitude.isFinite()) {
                color.map { it / magnitude }
            } else {
                listOf(1.0f, 0.0f, 0.0f)
            }
        }
    }
}
