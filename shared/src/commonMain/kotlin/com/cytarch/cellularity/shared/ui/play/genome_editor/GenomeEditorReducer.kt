package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft

/** Deterministic state transition for every Genome Editor action. */
object GenomeEditorReducer {
    fun reduce(state: GenomeEditorState, action: GenomeEditorAction): GenomeEditorTransition =
        when (action) {
            is GenomeEditorAction.SelectSection -> transition(
                state.copy(selectedSection = action.section)
            )
            is GenomeEditorAction.SelectGene -> transition(
                state.copy(
                    selectedGene = action.gene,
                    preview = state.preview.copy(
                        selectedGene = action.gene,
                        revision = state.preview.revision + 1
                    )
                )
            )
            is GenomeEditorAction.SetPreviewTime -> setPreviewTime(state, action.timeHours)
            is GenomeEditorAction.SetPreviewOption -> setPreviewOption(state, action)
            is GenomeEditorAction.SetSnapValues -> setSnapValues(state, action.enabled)
            is GenomeEditorAction.SetCellType -> editSelected(state) {
                it.copy(cellType = action.value)
            }
            is GenomeEditorAction.SetColor -> editSelected(state, snap = true) {
                it.withColor(action.red, action.green, action.blue)
            }
            is GenomeEditorAction.SetDivisionEnabled -> editSelected(state) {
                it.copy(
                    splitMass = when {
                        !action.enabled -> CellDataConstants.MAX_SPLIT_MASS
                        it.splitMass >= CellDataConstants.MAX_MASS.toFloat() ->
                            CellDataConstants.MAX_SPLITTABLE_MASS
                        else -> it.splitMass
                    }
                )
            }
            is GenomeEditorAction.SetScalar -> editSelected(state, snap = true) {
                it.withScalar(action.field, action.value)
            }
            is GenomeEditorAction.SetBoolean -> editSelected(state) {
                it.withBoolean(action.field, action.value)
            }
            is GenomeEditorAction.SetChildGene -> editSelected(state) {
                when (action.child) {
                    GenomeChild.FIRST -> it.copy(child1GeneIndex = action.gene.value)
                    GenomeChild.SECOND -> it.copy(child2GeneIndex = action.gene.value)
                }
            }
            is GenomeEditorAction.SetInteger -> editSelected(state) {
                it.withIntegerValue(action.id.value, action.value)
            }
            is GenomeEditorAction.SetFloat -> editSelected(state) {
                it.withFloatValue(action.type, action.value)
            }
            is GenomeEditorAction.SetParametricValue -> editSelected(state) {
                it.withParametricValue(action.id.value, action.value)
            }
            is GenomeEditorAction.ApplyOscillatorPreset -> editSelected(state) {
                GenomeOscillatorPreset.applyTo(it, action.configuration)
            }
            is GenomeEditorAction.ReplaceSelectedGene -> editSelected(state) { action.value }
            is GenomeEditorAction.ResetGene -> applyGenome(
                state = state,
                genome = state.genome.withGene(
                    action.gene.value,
                    action.value.copy(initial = state.genome.genes[action.gene.value].initial)
                ),
                dirty = true
            )
            is GenomeEditorAction.CopyGene -> applyGenome(
                state = state,
                genome = state.genome.copyGene(action.from.value, action.to.value),
                dirty = true
            )
            is GenomeEditorAction.AddGene -> {
                val insertionIndex = action.after.value + 1
                applyGenome(
                    state = state,
                    genome = state.genome.insertGene(insertionIndex, action.value),
                    dirty = true,
                    selectedGene = GeneIndex(insertionIndex)
                )
            }
            is GenomeEditorAction.AddChildGene -> addChildGene(state, action)
            is GenomeEditorAction.DuplicateGene -> {
                val insertionIndex = action.gene.value + 1
                applyGenome(
                    state = state,
                    genome = state.genome.duplicateGene(action.gene.value),
                    dirty = true,
                    selectedGene = GeneIndex(insertionIndex)
                )
            }
            is GenomeEditorAction.DeleteGene -> {
                val selectedIndex = state.genome.indexAfterDeletion(
                    state.selectedGene.value,
                    action.gene.value,
                    action.replacement.value
                )
                applyGenome(
                    state = state,
                    genome = state.genome.deleteGene(
                        action.gene.value,
                        action.replacement.value
                    ),
                    dirty = true,
                    selectedGene = GeneIndex(selectedIndex)
                )
            }
            is GenomeEditorAction.MoveGene -> {
                val selectedIndex = state.genome.indexAfterMove(
                    state.selectedGene.value,
                    action.from.value,
                    action.to.value
                )
                applyGenome(
                    state = state,
                    genome = state.genome.moveGene(action.from.value, action.to.value),
                    dirty = true,
                    selectedGene = GeneIndex(selectedIndex)
                )
            }
            is GenomeEditorAction.MakeInitial -> applyGenome(
                state = state,
                genome = state.genome.withOnlyInitial(action.gene.value),
                dirty = true
            )
            GenomeEditorAction.RequestLoad -> requestLoad(state)
            is GenomeEditorAction.LoadChoicesReady -> loadChoicesReady(state, action)
            GenomeEditorAction.DismissLoad -> transition(
                if (state.fileState is GenomeFileState.Choosing) {
                    state.copy(fileState = GenomeFileState.Idle)
                } else {
                    state
                }
            )
            is GenomeEditorAction.ChooseLoad -> chooseLoad(state, action.choice)
            is GenomeEditorAction.LoadSucceeded -> loadSucceeded(state, action)
            is GenomeEditorAction.LoadFailed -> transition(
                state.copy(fileState = GenomeFileState.Failed(action.reason, action.detail))
            )
            GenomeEditorAction.RequestSave -> transition(
                if (state.fileState.isBusy()) state
                else state.copy(fileState = GenomeFileState.EnteringSaveName)
            )
            GenomeEditorAction.DismissSave -> transition(
                if (state.fileState == GenomeFileState.EnteringSaveName) {
                    state.copy(fileState = GenomeFileState.Idle)
                } else {
                    state
                }
            )
            is GenomeEditorAction.ConfirmSave -> confirmSave(state, action.name)
            is GenomeEditorAction.SaveSucceeded -> {
                val saving = state.fileState as? GenomeFileState.Saving
                transition(if (saving != null && saving.name == action.name) {
                    state.copy(
                        dirty = state.genome !== saving.snapshot,
                        genomeName = action.name,
                        fileState = GenomeFileState.Saved(action.name)
                    )
                } else {
                    state
                })
            }
            is GenomeEditorAction.SaveFailed -> transition(
                state.copy(fileState = GenomeFileState.Failed(action.reason, action.detail))
            )
            GenomeEditorAction.ClearFileFeedback -> transition(
                if (state.fileState.isTerminal()) state.copy(fileState = GenomeFileState.Idle)
                else state
            )
        }

    private fun setPreviewTime(
        state: GenomeEditorState,
        requestedTime: Double
    ): GenomeEditorTransition {
        if (!requestedTime.isFinite()) return transition(state)
        val time = requestedTime.coerceIn(0.0, GenomeEditorConfig.PREVIEW_MAX_TIME_HOURS)
        return transition(
            state.copy(
                preview = state.preview.copy(
                    timeHours = time,
                    revision = state.preview.revision + 1
                )
            )
        )
    }

    private fun setPreviewOption(
        state: GenomeEditorState,
        action: GenomeEditorAction.SetPreviewOption
    ): GenomeEditorTransition {
        val preview = when (action.option) {
            GenomePreviewOption.GRID -> state.preview.copy(gridEnabled = action.enabled)
            GenomePreviewOption.TREE -> state.preview.copy(treeEnabled = action.enabled)
            GenomePreviewOption.GHOST -> state.preview.copy(ghostEnabled = action.enabled)
        }.let { it.copy(revision = state.preview.revision + 1) }
        return transition(state.copy(preview = preview))
    }

    private fun setSnapValues(
        state: GenomeEditorState,
        enabled: Boolean
    ): GenomeEditorTransition {
        if (state.snapValues == enabled) return transition(state)
        if (!enabled) return transition(state.copy(snapValues = false))
        val snapped = state.genome.withGene(
            state.selectedGene.value,
            GenomeEditorMath.snapGene(state.selectedGeneDraft)
        )
        return applyGenome(state.copy(snapValues = true), snapped, dirty = true)
    }

    private fun editSelected(
        state: GenomeEditorState,
        snap: Boolean = false,
        transform: (GeneDraft) -> GeneDraft
    ): GenomeEditorTransition {
        var edited = transform(state.selectedGeneDraft)
        if (snap && state.snapValues) edited = GenomeEditorMath.snapGene(edited)
        return applyGenome(
            state = state,
            genome = state.genome.withGene(state.selectedGene.value, edited),
            dirty = true
        )
    }

    private fun addChildGene(
        state: GenomeEditorState,
        action: GenomeEditorAction.AddChildGene
    ): GenomeEditorTransition {
        if (state.genome.genes.size >= GenomeContract.MAX_GENE_COUNT) {
            return transition(state)
        }
        val parentIndex = state.selectedGene.value
        val insertionIndex = parentIndex + 1
        val insertedGenome = state.genome.insertGene(insertionIndex, action.value)
        val parent = insertedGenome.genes[parentIndex]
        val linkedParent = when (action.child) {
            GenomeChild.FIRST -> parent.copy(child1GeneIndex = insertionIndex)
            GenomeChild.SECOND -> parent.copy(child2GeneIndex = insertionIndex)
        }
        return applyGenome(
            state = state,
            genome = insertedGenome.withGene(parentIndex, linkedParent),
            dirty = true
        )
    }

    private fun applyGenome(
        state: GenomeEditorState,
        genome: GenomeDraft,
        dirty: Boolean,
        selectedGene: GeneIndex = state.selectedGene
    ): GenomeEditorTransition {
        val validation = GenomeDraftValidator.validate(genome)
        val preview = state.preview.copy(
            snapshot = if (validation.isValid) genome else state.preview.snapshot,
            selectedGene = selectedGene,
            revision = state.preview.revision + 1,
            sourceIsValid = validation.isValid
        )
        return transition(
            state.copy(
                genome = genome,
                selectedGene = selectedGene,
                dirty = dirty,
                validation = validation,
                preview = preview
            )
        )
    }

    private fun requestLoad(state: GenomeEditorState): GenomeEditorTransition =
        if (state.fileState.isBusy()) {
            transition(state)
        } else {
            GenomeEditorTransition(
                state.copy(fileState = GenomeFileState.Listing),
                listOf(GenomeEditorEffect.ListStoredGenomes)
            )
        }

    private fun loadChoicesReady(
        state: GenomeEditorState,
        action: GenomeEditorAction.LoadChoicesReady
    ): GenomeEditorTransition {
        if (state.fileState != GenomeFileState.Listing) return transition(state)
        val choices = buildList {
            add(GenomeLoadChoice.Microscope)
            add(GenomeLoadChoice.DefaultGenome)
            action.storedNames.distinct().forEach { add(GenomeLoadChoice.Stored(it)) }
        }
        return transition(state.copy(fileState = GenomeFileState.Choosing(choices)))
    }

    private fun chooseLoad(
        state: GenomeEditorState,
        choice: GenomeLoadChoice
    ): GenomeEditorTransition {
        val choosing = state.fileState as? GenomeFileState.Choosing ?: return transition(state)
        if (choice !in choosing.choices) return transition(state)
        return GenomeEditorTransition(
            state.copy(fileState = GenomeFileState.Loading(choice)),
            listOf(GenomeEditorEffect.LoadGenome(choice))
        )
    }

    private fun loadSucceeded(
        state: GenomeEditorState,
        action: GenomeEditorAction.LoadSucceeded
    ): GenomeEditorTransition {
        if (state.fileState !is GenomeFileState.Loading) return transition(state)
        val candidate = if (action.origin == GenomeLoadOrigin.DEFAULT_GENOME) {
            GenomeEditorMath.snapGenome(action.genome)
        } else {
            action.genome
        }
        val validation = GenomeDraftValidator.validate(candidate)
        if (!validation.isValid) {
            return transition(
                state.copy(
                    fileState = GenomeFileState.Failed(GenomeFileFailureReason.INVALID_GENOME)
                )
            )
        }
        val selected = GeneIndex(
            state.selectedGene.value.coerceIn(0, candidate.genes.lastIndex)
        )
        val name = if (action.origin == GenomeLoadOrigin.STORED_FILE) action.name.orEmpty() else ""
        return transition(
            state.copy(
                genome = candidate,
                selectedGene = selected,
                snapValues = GenomeEditorMath.isSnapped(candidate),
                dirty = action.origin != GenomeLoadOrigin.STORED_FILE,
                validation = validation,
                preview = state.preview.copy(
                    snapshot = candidate,
                    selectedGene = selected,
                    revision = state.preview.revision + 1,
                    sourceIsValid = true
                ),
                fileState = GenomeFileState.Loaded(action.name),
                genomeName = name
            )
        )
    }

    private fun confirmSave(
        state: GenomeEditorState,
        requestedName: String
    ): GenomeEditorTransition {
        if (state.fileState != GenomeFileState.EnteringSaveName) return transition(state)
        val name = requestedName.trim()
        if (!CellularityFileNames.isValidDisplayName(name)) {
            return transition(
                state.copy(fileState = GenomeFileState.Failed(GenomeFileFailureReason.BAD_NAME))
            )
        }
        if (!state.validation.isValid) {
            return transition(
                state.copy(fileState = GenomeFileState.Failed(GenomeFileFailureReason.INVALID_GENOME))
            )
        }
        return GenomeEditorTransition(
            state.copy(fileState = GenomeFileState.Saving(name, state.genome)),
            listOf(GenomeEditorEffect.SaveGenome(name, state.genome))
        )
    }

    private fun GeneDraft.withScalar(field: GenomeScalarField, value: Float): GeneDraft =
        when (field) {
            GenomeScalarField.SPLIT_MASS -> copy(splitMass = value)
            GenomeScalarField.SPLIT_RATIO -> copy(splitRatio = value)
            GenomeScalarField.SPLIT_ANGLE -> copy(splitAngle = value)
            GenomeScalarField.CHILD_1_ANGLE -> copy(child1Angle = value)
            GenomeScalarField.CHILD_2_ANGLE -> copy(child2Angle = value)
            GenomeScalarField.MASS_FLOW_PRIORITY -> copy(massFlowPriority = value)
            GenomeScalarField.LINK_STRENGTH -> copy(linkStrength = value)
        }

    private fun GeneDraft.withBoolean(field: GenomeBooleanField, value: Boolean): GeneDraft =
        when (field) {
            GenomeBooleanField.MAKE_LINK -> copy(makeLink = value)
            GenomeBooleanField.CHILD_1_KEEP_LINKS -> copy(child1KeepLinks = value)
            GenomeBooleanField.CHILD_2_KEEP_LINKS -> copy(child2KeepLinks = value)
            GenomeBooleanField.STAY_ALIVE -> copy(stayAlive = value)
            GenomeBooleanField.CHILD_1_MIRROR -> copy(child1Mirror = value)
            GenomeBooleanField.CHILD_2_MIRROR -> copy(child2Mirror = value)
        }

    private fun transition(state: GenomeEditorState): GenomeEditorTransition =
        GenomeEditorTransition(state)

    private fun GenomeFileState.isBusy(): Boolean =
        this == GenomeFileState.Listing || this is GenomeFileState.Loading || this is GenomeFileState.Saving

    private fun GenomeFileState.isTerminal(): Boolean =
        this is GenomeFileState.Loaded || this is GenomeFileState.Saved || this is GenomeFileState.Failed

}
