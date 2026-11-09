package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import androidx.lifecycle.ViewModel
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorDefaults
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorEffect
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorReducer
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorState
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
        fun createRandomizedEditorGene(geneIndex: Int): GeneDraft =
            GenomeEditorDefaults.createRandomizedEditorGene(geneIndex)

        fun createRandomizedEditorGenome(): GenomeDraft =
            GenomeEditorDefaults.createRandomizedEditorGenome()
    }
}
